// 


#include "Core/ExpeditionTravelSubsystem.h"

#include "Core/ExplorationGameState.h"
#include "Core/GoHomeGameMode.h"
#include "Core/GoHomePlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "UI/Loading/GoHomeTravelScreen.h"
#include "UI/Loading/TravelPresentation.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogExpeditionTravel, Log, All);

static TAutoConsoleVariable<int32> CVarTravelReducedMotion(TEXT("GoHome.Travel.ReducedMotion"),0,
	TEXT("1: 로딩 화면의 이동/입자/순환을 생략하고 100ms fade만 사용한다."),ECVF_Default);

void UExpeditionTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PresentationTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickPresentation));
	if (GEngine)
	{
		GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
		GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
	}
}

void UExpeditionTravelSubsystem::Deinitialize()
{
	CancelPresentation();
	FTSTicker::GetCoreTicker().RemoveTicker(PresentationTicker);
	if (GEngine)
	{
		GEngine->OnTravelFailure().RemoveAll(this);
		GEngine->OnNetworkFailure().RemoveAll(this);
	}
	Super::Deinitialize();
}

void UExpeditionTravelSubsystem::BeginPresentation(const FString& Destination, bool bReturning, int32 TravelId)
{
	if (IsRunningDedicatedServer() || Destination.IsEmpty()) return;
	// 같은 RPC/트래블 ID로 진입을 다시 재생하지 않는다.
	if (Screen && !bDebugPreview && PresentationId == TravelId && PresentationDestination == Destination) return;
	CancelPresentation();
	Presentation = MakeShared<FTravelPresentation>();
	Presentation->Direction = bReturning ? -1 : 1;
	Presentation->bReducedMotion=CVarTravelReducedMotion.GetValueOnGameThread()!=0;
	PresentationDestination = Destination;
	PresentationId = TravelId;
	SourceWorld = GetWorld();
	EnsureScreen();
	UE_LOG(LogExpeditionTravel, Log, TEXT("Begin id=%d direction=%d destination=%s"), TravelId, Presentation->Direction, *Destination);
}

void UExpeditionTravelSubsystem::EnsureScreen()
{
	if (Screen || !Presentation || !FSlateApplication::IsInitialized()) return;
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	if (!Viewport) return;
	WhiteTexture = LoadObject<UTexture2D>(nullptr,TEXT("/Game/GoHome/UI/Announcer/T_Announcer_White.T_Announcer_White"));
	if (!WhiteTexture)
	{
		WhiteTexture = UTexture2D::CreateTransient(1,1,PF_B8G8R8A8);
		if (!WhiteTexture) return;
		void* Data = WhiteTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		*static_cast<FColor*>(Data) = FColor::White;
		WhiteTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		WhiteTexture->UpdateResource();
	}
	Screen = SNew(SGoHomeTravelScreen).Presentation(Presentation).WhiteTexture(WhiteTexture);
	ScreenViewport = Viewport;
	Viewport->AddViewportWidgetContent(Screen.ToSharedRef(),500);
	FSlateApplication::Get().SetAllUserFocus(Screen.ToSharedRef(),EFocusCause::SetDirectly);
}

void UExpeditionTravelSubsystem::CancelPresentation()
{
	if (Screen)
	{
		if (UGameViewportClient* Viewport = ScreenViewport.Get()) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
		if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
		if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->FlushPressedKeys();
		}
	}
	Screen.Reset(); Presentation.Reset(); ScreenViewport.Reset(); SourceWorld.Reset();
	PresentationDestination.Empty(); bDebugPreview=false; bDebugPaused=false; bDestinationSeen=false;
}

bool UExpeditionTravelSubsystem::TickPresentation(float DeltaSeconds)
{
	if (!Presentation) return true;
	EnsureScreen();
	if (!Screen) return true;
	UWorld* World = GetWorld();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (!bDebugPreview && PC)
	{
		PC->SetInputMode(FInputModeUIOnly().SetWidgetToFocus(Screen));
		PC->bShowMouseCursor = false;
	}
	FTravelPresentation& F=*Presentation;
	if (!bDebugPaused && F.Phase!=ETravelPresentationPhase::Error && F.Phase!=ETravelPresentationPhase::Covered)
	{
		const float DT=FMath::Clamp(DeltaSeconds,0.f,.1f);
		F.Elapsed+=DT;
		if (!F.bReducedMotion) F.MotionTime+=DT;
	}
	// Destination package, a different live world, player possession and the next Sequence's preparing data.
	// 중간 LV_Loading 도착이나 서버만의 로드 완료는 종료 조건이 아니다.
	if (!bDebugPreview && World && World!=SourceWorld.Get() && !World->bIsTearingDown && World->HasBegunPlay() &&
		!World->IsInSeamlessTravel() && PC && PC->GetWorld()==World && PC->GetPawn() && PC->GetPawn()->GetWorld()==World && World->GetGameState())
	{
		FString Package=World->GetOutermost()->GetName();
		const FString Prefix=World->StreamingLevelsPrefix;
		if (!Prefix.IsEmpty()) Package.ReplaceInline(*Prefix,TEXT(""));
		if (Package == PresentationDestination)
		{
			const AExplorationGameState* GS=World->GetGameState<AExplorationGameState>();
			if (!GS || GS->GetEntryState().OpeningDuration > 0.f || GS->HasExplorationStarted())
			{
				F.bReady=true;
				if (!bDestinationSeen) UE_LOG(LogExpeditionTravel,Log,TEXT("Destination ready id=%d package=%s"),PresentationId,*Package);
				bDestinationSeen=true;
			}
		}
		else if (bDestinationSeen)
		{
			CancelPresentation(); return true;
		}
	}
	if (F.Phase==ETravelPresentationPhase::Enter && F.Elapsed>=(F.bReducedMotion?.1f:FTravelPresentation::EnterSeconds)) F.Phase=ETravelPresentationPhase::Loop;
	if (F.Phase==ETravelPresentationPhase::Loop && F.bReady)
	{
		F.Phase=ETravelPresentationPhase::Exit; F.ExitStartedAt=F.Elapsed;
	}
	if (!bDebugPaused && F.Phase==ETravelPresentationPhase::Exit && F.Elapsed-F.ExitStartedAt>=(F.bReducedMotion?.1f:FTravelPresentation::ExitSeconds))
	{
		F.Phase=ETravelPresentationPhase::Covered;
		Screen->Invalidate(EInvalidateWidgetReason::Paint);
		return true;
	}
	if (F.Phase==ETravelPresentationPhase::Covered && !bDebugPreview)
	{
		// One full paint/frame of the final opaque cover before the next screen takes over.
		if (F.bCoverPainted)
		{
			UE_LOG(LogExpeditionTravel,Log,TEXT("Presentation complete id=%d"),PresentationId);
			CancelPresentation(); return true;
		}
	}
	Screen->Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

void UExpeditionTravelSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type, const FString& Error)
{
	if (World && World->GetGameInstance()!=GetGameInstance()) return;
	UE_LOG(LogExpeditionTravel,Warning,TEXT("Travel failure: %s"),*Error);
	// Engine/System recovery takes over immediately; never cover its UI or trap the player's input.
	CancelPresentation();
}

void UExpeditionTravelSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver*, ENetworkFailure::Type, const FString& Error)
{
	if (World && World->GetGameInstance()!=GetGameInstance()) return;
	UE_LOG(LogExpeditionTravel,Warning,TEXT("Network failure: %s"),*Error);
	CancelPresentation();
}

#if !UE_BUILD_SHIPPING
void UExpeditionTravelSubsystem::Preview(bool bReturning, float TimeMs, int32 Mode, bool bReduced)
{
	BeginPresentation(TEXT("Preview"),bReturning,-1);
	if (!Presentation) return;
	bDebugPreview=true; bDebugPaused=TimeMs>=0.f;
	FTravelPresentation& F=*Presentation;
	F.Elapsed=FMath::Max(TimeMs*.001f,0.f); F.MotionTime=bReduced?0:F.Elapsed; F.bReducedMotion=bReduced;
	F.Phase=F.Elapsed>=(bReduced?.1f:FTravelPresentation::EnterSeconds)?ETravelPresentationPhase::Loop:ETravelPresentationPhase::Enter;
	if (Mode==1) F.Phase=ETravelPresentationPhase::Error;
	if (Mode==2) { F.Phase=ETravelPresentationPhase::Exit; F.bReady=true; F.ExitStartedAt=F.Elapsed-.3f; }
	Screen->Invalidate(EInvalidateWidgetReason::Paint);
}

void UExpeditionTravelSubsystem::FinishPreview()
{
	if (bDebugPreview && Presentation) { bDebugPaused=false; Presentation->bReady=true; }
}

static FAutoConsoleCommandWithWorldAndArgs TravelPreviewCommand(
	TEXT("GoHome.Travel.Preview"),TEXT("down|up [ms=-1:재생] [mode=0:일반,1:오류,2:퇴장] [reduced=0]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
	{
		for (TObjectIterator<UExpeditionTravelSubsystem> It;It;++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) continue;
			It->Preview(Args.Num()>0&&Args[0]==TEXT("up"),Args.Num()>1?FCString::Atof(*Args[1]):-1.f,
				Args.Num()>2?FCString::Atoi(*Args[2]):0,Args.Num()>3&&FCString::Atoi(*Args[3])!=0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs TravelFinishCommand(TEXT("GoHome.Travel.Finish"),TEXT("미리보기 퇴장"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* World)
	{ for (TObjectIterator<UExpeditionTravelSubsystem> It;It;++It) if(!It->HasAnyFlags(RF_ClassDefaultObject)&&It->GetWorld()&&It->GetWorld()->IsGameWorld()) It->FinishPreview(); }));

static FAutoConsoleCommandWithWorldAndArgs TravelCloseCommand(TEXT("GoHome.Travel.Close"),TEXT("미리보기 정리"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&,UWorld* World)
	{ for (TObjectIterator<UExpeditionTravelSubsystem> It;It;++It) if(!It->HasAnyFlags(RF_ClassDefaultObject)&&It->GetWorld()&&It->GetWorld()->IsGameWorld()) It->CancelPresentation(); }));

static FAutoConsoleCommandWithWorldAndArgs TravelTestCommand(TEXT("GoHome.Travel.Test"),TEXT("최종 맵 패키지 경로: 실제 서버 트래블"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
	{
		if (Args.IsEmpty()) return;
		for (TObjectIterator<AGoHomeGameMode> It;It;++It)
		{
			UWorld* GameWorld=It->GetWorld();
			if (It->HasAnyFlags(RF_ClassDefaultObject) || !GameWorld || !GameWorld->IsGameWorld() || GameWorld->bIsTearingDown || GameWorld->IsInSeamlessTravel()) continue;
			TWeakObjectPtr<AGoHomeGameMode> GM=*It;
			const FString Map=Args[0];
			GameWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([GM,Map]() { if(GM.IsValid()) GM->ServerTravelViaLoadingScreen(Map); }));
			break;
		}
	}));
#endif

void UExpeditionTravelSubsystem::SetActiveZone(const UExpeditionZoneDataAsset* ZoneDataAsset, int32 ZoneNumber)
{
	ActiveZone = const_cast<UExpeditionZoneDataAsset*>(ZoneDataAsset);
	ActiveZoneNumber = ZoneDataAsset ? ZoneNumber : 0;
}
