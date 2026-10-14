#include "Core/GoHomeGameMode.h"

#include "Core/ExpeditionTravelSubsystem.h"
#include "Core/GoHomeGameState.h"
#include "Core/GoHomePlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "TimerManager.h"

void AGoHomeGameMode::BeginPlay()
{
	Super::BeginPlay();
	const UExpeditionTravelSubsystem* S=GetGameInstance()->GetSubsystem<UExpeditionTravelSubsystem>();
	FString Package=GetWorld()->GetOutermost()->GetName();
	if (!GetWorld()->StreamingLevelsPrefix.IsEmpty()) Package.ReplaceInline(*GetWorld()->StreamingLevelsPrefix,TEXT(""));
	if (Package==LoadingMapPath && S && !S->GetPendingDestinationMap().IsEmpty())
	{
		GetWorldTimerManager().SetTimerForNextTick(this,&ThisClass::ContinuePendingTravel);
	}
}

void AGoHomeGameMode::ContinuePendingTravel()
{
	// A second ServerTravel must wait until the first seamless travel has left its transition phase.
	bool bPlayersStillLoading = NumTravellingPlayers > 0;
	for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
	{
		if (APlayerController* PC=It->Get()) bPlayersStillLoading |= !PC->HasClientLoadedCurrentWorld();
	}
	if (GetWorld()->IsInSeamlessTravel() || bPlayersStillLoading)
	{
		GetWorldTimerManager().SetTimer(LoadingTravelTimer,this,&ThisClass::ContinuePendingTravel,.01f,false);
		return;
	}
	if (const auto* S=GetGameInstance()->GetSubsystem<UExpeditionTravelSubsystem>())
	{
		if (!S->GetPendingDestinationMap().IsEmpty()) ServerTravelToMap(S->GetPendingDestinationMap());
	}
}

AGoHomeGameMode::AGoHomeGameMode()
{
	bUseSeamlessTravel = true;

	PlayerControllerClass = AGoHomePlayerController::StaticClass();
}

void AGoHomeGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!NewPlayer)
	{
		return;
	}

	APlayerState* PlayerState = NewPlayer->GetPlayerState<APlayerState>();
	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid() || !OnlineSubsystem)
	{
		return;
	}

	const IOnlineIdentityPtr Identity = OnlineSubsystem->GetIdentityInterface();
	if (!Identity.IsValid())
	{
		return;
	}

	FString Nickname = Identity->GetPlayerNickname(*PlayerState->GetUniqueId());
	Nickname.TrimStartAndEndInline();
	if (!Nickname.IsEmpty())
	{
		Nickname.ReplaceInline(TEXT("\r"), TEXT(""));
		Nickname.ReplaceInline(TEXT("\n"), TEXT(""));
		PlayerState->SetPlayerName(Nickname.Left(32));
	}
}

void AGoHomeGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
}

void AGoHomeGameMode::ServerTravelToMap(const FString& MapPath)
{
	// UE_LOG(LogTemp, Warning, TEXT("[GoHome] ServerTravelToMap called: %s"), *MapPath);

	if (AGoHomeGameState* GoHomeGameState = GetGameState<AGoHomeGameState>())
	{
		GoHomeGameState->SetState(EExpeditionState::Departure);
	}

	GetWorld()->ServerTravel(MapPath + TEXT("?listen"), /*bAbsolute=*/true);
}

void AGoHomeGameMode::ServerTravelViaLoadingScreen(const FString& FinalMapPath)
{
	if (bLoadingTravelPending || FinalMapPath.IsEmpty()) return;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UExpeditionTravelSubsystem* TravelSubsystem = GameInstance->GetSubsystem<UExpeditionTravelSubsystem>())
		{
			TravelSubsystem->SetPendingDestinationMap(FinalMapPath);
			bLoadingTravelPending=true;
			static int32 NextTravelId=0;
			const int32 Id=++NextTravelId;
			for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
			{
				if (auto* PC=Cast<AGoHomePlayerController>(It->Get())) PC->Client_BeginExpeditionTravel(FinalMapPath,FinalMapPath==LobbyMapPath,Id);
			}
			// Show enter on the source world before starting the actual load. No guessed load percentage or fixed loading-map wait.
			GetWorldTimerManager().SetTimer(LoadingTravelTimer,FTimerDelegate::CreateWeakLambda(this,[this]() { ServerTravelToMap(LoadingMapPath); }),.76f,false);
			return;
		}
	}

	ServerTravelToMap(LoadingMapPath);
}
