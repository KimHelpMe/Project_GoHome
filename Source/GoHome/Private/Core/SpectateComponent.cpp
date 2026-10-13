#include "Core/SpectateComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/ExpeditionState.h"
#include "Core/ExplorationGameState.h"
#include "Core/GoHomeGameUserSettings.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputAction.h"
#include "Player/HealthComponent.h"
#include "Player/OxygenComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpectate, Log, All);

#if !UE_BUILD_SHIPPING
// 입력 없이 대상 전환 확인용: GoHome.Spectate.Step <1|-1> — 관전 중인 모든 로컬 컨트롤러(PIE 다중 창 포함)
static FAutoConsoleCommand GSpectateStepCommand(
	TEXT("GoHome.Spectate.Step"),
	TEXT("관전 대상을 다음(1)/이전(-1) 생존자로 바꾼다."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 Direction = Args.Num() > 0 && FCString::Atoi(*Args[0]) < 0 ? -1 : 1;
		for (TObjectIterator<USpectateComponent> It; It; ++It)
		{
			if (!It->IsTemplate() && It->GetView().Phase == ESpectatePhase::Spectating)
			{
				It->Step(Direction);
			}
		}
	}));
#endif

USpectateComponent::USpectateComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// 대상 폰 이동이 끝난 뒤 카메라를 맞춘다(한 프레임 늦은 추적 방지)
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// Server_SetSpectateTarget 용
	SetIsReplicatedByDefault(true);

	for (const TCHAR* Path : {
		TEXT("/Game/GoHome/UI/HUD/WBP_HUD.WBP_HUD_C"),								// 조준점·운반 역할
		TEXT("/Game/GoHome/Developers/LSA/UI/WB_HP_OxygenUI.WB_HP_OxygenUI_C"),		// HP·O₂ 링·LOAD
		TEXT("/Game/GoHome/UI/InventorySlot/WBP_InventorySlots.WBP_InventorySlots_C"),	// 핫바
		TEXT("/Game/GoHome/UI/WBP_InteractionPrompt.WBP_InteractionPrompt_C"),
		TEXT("/Game/GoHome/UI/WBP_DangerVignette.WBP_DangerVignette_C"),
		TEXT("/Game/GoHome/UI/Ink/WBP_InkSplatEffect.WBP_InkSplatEffect_C"),
		TEXT("/Game/GoHome/Developers/YSS/Teleport/WBP_TeleportHoldGauge.WBP_TeleportHoldGauge_C"),
		TEXT("/Script/GoHome.ItemInspectWidget") })										// 유물 조준 정보
	{
		HiddenHUDClasses.Add(TSoftClassPtr<UUserWidget>(FSoftClassPath(Path)));
	}
}

void USpectateComponent::BeginPlay()
{
	Super::BeginPlay();
	const APlayerController* PC = GetOwnerController();
	SetComponentTickEnabled(PC && PC->IsLocalController());
}

void USpectateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetSpectate();
	Super::EndPlay(EndPlayReason);
}

APlayerController* USpectateComponent::GetOwnerController() const
{
	return Cast<APlayerController>(GetOwner());
}

AExplorationGameState* USpectateComponent::GetExplorationGameState() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<AExplorationGameState>() : nullptr;
}

bool USpectateComponent::IsRoundResolved() const
{
	const AExplorationGameState* GameState = GetExplorationGameState();
	return GameState && (GameState->GetCurrentState() == EExpeditionState::Settlement || GameState->GetCurrentState() == EExpeditionState::Failed);
}

bool USpectateComponent::IsOwnPawnDead() const
{
	const APlayerController* PC = GetOwnerController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	return Health && Health->IsDead();
}

APawn* USpectateComponent::FindPawn(const APlayerState* PlayerState) const
{
	return PlayerState ? PlayerState->GetPawn() : nullptr;
}

bool USpectateComponent::IsPlayerAlive(const APlayerState* PlayerState) const
{
	if (!PlayerState || PlayerState->IsOnlyASpectator())
	{
		return false;
	}
	const AExplorationGameState* GameState = GetExplorationGameState();
	if (GameState && GameState->IsPlayerDead(PlayerState))
	{
		return false;
	}
	// GameState 미러보다 폰의 bIsDead 복제가 먼저 올 수 있다
	const APawn* Pawn = FindPawn(PlayerState);
	const UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	return !(Health && Health->IsDead());
}

// --- 틱 ---

void USpectateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APlayerController* PC = GetOwnerController();
	if (!PC)
	{
		return;
	}

	// 폰이 바뀌면(로비 재생성 등) 이전 관전은 끝
	if (View.Phase != ESpectatePhase::Inactive && WatchedPawn.Get() != PC->GetPawn())
	{
		ResetSpectate();
	}

	if (View.Phase == ESpectatePhase::Inactive)
	{
		if (!bSettled && GetExplorationGameState() && IsOwnPawnDead())
		{
			BeginDying();
		}
		return;
	}

	View.PhaseSeconds += DeltaTime;

	// 정산/실패가 이미 시작됐다(OnSettlementReady보다 상태 복제가 먼저 올 수 있다) — 관전 화면은 그 아래로 숨는다
	if (!bSettled && IsRoundResolved())
	{
		bSettled = true;
		HideScreen();
	}

	// 다른 시스템(배전반·운반 화면 종료 등)이 뷰 타깃을 되돌렸으면 다시 가져온다
	if (Camera && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTarget(Camera);
	}

	switch (View.Phase)
	{
	case ESpectatePhase::Dying:
		UpdateCorpseCamera();
		if (View.PhaseSeconds >= DyingSeconds)
		{
			BeginConnecting();
		}
		break;

	case ESpectatePhase::Connecting:
		// 첫 연결만 시체 시점에서 — 대상 전환 중 재연결이면 마지막 구도를 유지
		if (View.SwitchSerial == 0)
		{
			UpdateCorpseCamera();
		}
		if (!IsPlayerAlive(Target.Get()))
		{
			// 연결 중에 대상이 죽었다 — 다른 생존자로, 없으면 끝
			SetTarget(PickNextAlive(Target.Get(), 1));
			if (!Target.IsValid())
			{
				SetPhase(ESpectatePhase::NoSurvivor);
				break;
			}
		}
		// 노이즈가 끝났고 대상 폰이 복제돼 있어야 컷
		if (View.PhaseSeconds >= ConnectNoiseSeconds && FindPawn(Target.Get()))
		{
			SetPhase(ESpectatePhase::Spectating);
			Cut(1);
		}
		break;

	case ESpectatePhase::Spectating:
		if (!IsPlayerAlive(Target.Get()))
		{
			View.LostName = Target.IsValid() ? Target->GetPlayerName() : FString();
			SetPhase(ESpectatePhase::TargetLost);
		}
		UpdateOrbitCamera(DeltaTime, false);
		break;

	case ESpectatePhase::TargetLost:
		UpdateOrbitCamera(DeltaTime, false);
		if (View.PhaseSeconds >= TargetLostHoldSeconds)
		{
			SetTarget(PickNextAlive(Target.Get(), 1));
			if (Target.IsValid() && FindPawn(Target.Get()))
			{
				SetPhase(ESpectatePhase::Spectating);
				Cut(1);
			}
			else if (Target.IsValid())
			{
				// 다음 대상 폰이 아직 관련성 밖 — 피드를 다시 연결한다
				SetPhase(ESpectatePhase::Connecting);
			}
			else
			{
				SetPhase(ESpectatePhase::NoSurvivor);
			}
		}
		break;

	case ESpectatePhase::NoSurvivor:
		// 생존자 0 — 마지막 화면 유지, 실패 화면이 곧 위를 덮는다
		if (Target.IsValid())
		{
			UpdateOrbitCamera(DeltaTime, false);
		}
		break;

	default:
		break;
	}

	RefreshView();
}

void USpectateComponent::SetPhase(ESpectatePhase NewPhase)
{
	if (View.Phase == NewPhase)
	{
		return;
	}
	UE_LOG(LogSpectate, Log, TEXT("Phase %d -> %d"), static_cast<int32>(View.Phase), static_cast<int32>(NewPhase));
	View.Phase = NewPhase;
	View.PhaseSeconds = 0.f;
	// 사망 뒤에 늦게 만들어진 개인 HUD(경고 비네트 등)도 걷는다
	HidePersonalHUD();
}

// --- 단계 진입 ---

void USpectateComponent::BeginDying()
{
	APlayerController* PC = GetOwnerController();
	if (!PC)
	{
		return;
	}

	WatchedPawn = PC->GetPawn();
	View = FSpectateView();
	bSettled = IsRoundResolved();

	// 사망 시각(탐사 시작 기준)
	if (const AExplorationGameState* GameState = GetExplorationGameState())
	{
		if (GameState->HasTimeLimit())
		{
			View.DeathElapsedSeconds = FMath::Max(0.f, GameState->GetTotalSeconds() - GameState->GetRemainingSeconds());
		}
		if (BoundGameState.Get() != GameState)
		{
			AExplorationGameState* MutableGameState = const_cast<AExplorationGameState*>(GameState);
			MutableGameState->OnSettlementReady.AddUniqueDynamic(this, &ThisClass::HandleSettlementReady);
			BoundGameState = MutableGameState;
		}
	}

	if (PC->PlayerCameraManager)
	{
		CorpseStartLocation = PC->PlayerCameraManager->GetCameraLocation();
		CorpseStartRotation = PC->PlayerCameraManager->GetCameraRotation();
		// 서버가 SetViewTarget으로 보내는 ClientSetViewTarget을 무시 — 화면은 로컬 관전 카메라가 정한다
		PC->PlayerCameraManager->bClientSimulatingViewTarget = true;
	}

	// 뷰 타깃이 폰이 아니게 되면 OwnerNoSee가 풀려 내 3인칭 몸이 시체 시점 카메라를 가린다 — 시체 시점 동안만 내 화면에서 숨긴다
	if (APawn* OwnPawn = PC->GetPawn())
	{
		PC->HiddenActors.AddUnique(OwnPawn);
	}

	SpawnCamera();
	BindInput();
	HidePersonalHUD();
	SetPhase(ESpectatePhase::Dying);
	UpdateCorpseCamera();
	ShowScreen();
	RefreshView();
}

void USpectateComponent::BeginConnecting()
{
	SetTarget(PickNextAlive(GetOwnerController() ? GetOwnerController()->PlayerState.Get() : nullptr, 1));
	SetPhase(Target.IsValid() ? ESpectatePhase::Connecting : ESpectatePhase::NoSurvivor);
}

void USpectateComponent::ResetSpectate()
{
	APlayerController* PC = GetOwnerController();
	const bool bWasActive = View.Phase != ESpectatePhase::Inactive;

	UnbindInput();
	HideScreen();
	RestorePersonalHUD();

	if (PC && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->bClientSimulatingViewTarget = false;
	}
	if (PC && WatchedPawn.IsValid())
	{
		PC->HiddenActors.Remove(WatchedPawn.Get());
	}
	if (Camera)
	{
		if (PC && PC->GetViewTarget() == Camera && PC->GetPawn())
		{
			PC->SetViewTarget(PC->GetPawn());
		}
		Camera->Destroy();
		Camera = nullptr;
	}
	if (bWasActive && Target.IsValid() && PC && PC->IsLocalController())
	{
		Server_SetSpectateTarget(nullptr);
	}
	if (BoundGameState.IsValid())
	{
		BoundGameState->OnSettlementReady.RemoveDynamic(this, &ThisClass::HandleSettlementReady);
	}
	BoundGameState.Reset();

	Target.Reset();
	WatchedPawn.Reset();
	View = FSpectateView();
	bSettled = false;
}

void USpectateComponent::HandleSettlementReady(const FSettlementResult& Result)
{
	// 정산/실패 Sequence가 위를 덮는다 — 관전 화면만 걷고 카메라는 그대로(화면 뒤)
	bSettled = true;
	HideScreen();
}

// --- 대상 ---

APlayerState* USpectateComponent::PickNextAlive(const APlayerState* From, int32 Direction) const
{
	const AExplorationGameState* GameState = GetExplorationGameState();
	const APlayerController* PC = GetOwnerController();
	if (!GameState || !PC)
	{
		return nullptr;
	}

	const TArray<TObjectPtr<APlayerState>>& Players = GameState->PlayerArray;
	const int32 Count = Players.Num();
	if (Count == 0)
	{
		return nullptr;
	}

	// 기준(현재 대상 또는 나)이 목록에 없으면 처음부터
	int32 Start = Players.IndexOfByKey(From);
	if (Start == INDEX_NONE)
	{
		Start = Direction > 0 ? Count - 1 : 0;
	}

	for (int32 Offset = 1; Offset <= Count; ++Offset)
	{
		APlayerState* Candidate = Players[((Start + Direction * Offset) % Count + Count) % Count];
		if (Candidate && Candidate != PC->PlayerState && IsPlayerAlive(Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

void USpectateComponent::SetTarget(APlayerState* NewTarget)
{
	if (Target.Get() == NewTarget)
	{
		return;
	}
	Target = NewTarget;
	Server_SetSpectateTarget(NewTarget);
}

void USpectateComponent::Step(int32 Direction)
{
	UE_LOG(LogSpectate, Verbose, TEXT("Step %d (phase %d)"), Direction, static_cast<int32>(View.Phase));
	if (View.Phase != ESpectatePhase::Spectating)
	{
		return;
	}
	APlayerState* Next = PickNextAlive(Target.Get(), Direction);
	if (!Next || Next == Target.Get())
	{
		return;	// 마지막 생존자
	}
	SetTarget(Next);
	if (FindPawn(Next))
	{
		Cut(Direction);
	}
	else
	{
		SetPhase(ESpectatePhase::Connecting);
	}
}

void USpectateComponent::Cut(int32 Direction)
{
	const APawn* TargetPawn = FindPawn(Target.Get());
	if (!TargetPawn)
	{
		return;
	}
	// 오빗부터는 내 시체도 장면의 일부
	if (APlayerController* PC = GetOwnerController())
	{
		PC->HiddenActors.Remove(PC->GetPawn());
	}
	// 대상 뒤에서 시작 — 전환마다 같은 구도라 예측 가능하다
	Orbit = FRotator(DefaultPitch, TargetPawn->GetActorRotation().Yaw, 0.f);
	++View.SwitchSerial;
	View.SwitchDirection = Direction >= 0 ? 1 : -1;
	UpdateOrbitCamera(0.f, true);
}

void USpectateComponent::Server_SetSpectateTarget_Implementation(APlayerState* NewTarget)
{
	APlayerController* PC = GetOwnerController();
	if (!PC || PC->IsLocalController())
	{
		// 리슨 호스트는 서버가 모든 폰을 갖고 있고, 여기서 뷰 타깃을 바꾸면 로컬 관전 카메라를 덮는다
		return;
	}

	APawn* OwnPawn = PC->GetPawn();
	const AExplorationGameState* GameState = GetExplorationGameState();
	const bool bRequesterDead = GameState && OwnPawn && GameState->IsPlayerDead(PC->PlayerState);
	APawn* TargetPawn = NewTarget ? NewTarget->GetPawn() : nullptr;
	const bool bTargetValid = TargetPawn && NewTarget != PC->PlayerState && GameState && !GameState->IsPlayerDead(NewTarget);

	if (bRequesterDead && bTargetValid)
	{
		PC->SetViewTarget(TargetPawn);
	}
	else if (OwnPawn)
	{
		PC->SetViewTarget(OwnPawn);
	}
}

// --- 카메라 ---

void USpectateComponent::SpawnCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwnerController();
	if (!World || !PC || Camera)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Params.bDeferConstruction = true;
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(CorpseStartRotation, CorpseStartLocation), Params);
	if (!Camera)
	{
		return;
	}
	// 로컬 전용 — 리슨 호스트에서 스폰돼도 다른 클라로 복제되지 않게
	Camera->SetReplicates(false);
	Camera->FinishSpawning(FTransform(CorpseStartRotation, CorpseStartLocation));

	if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
	{
		CameraComponent->bConstrainAspectRatio = false;
		const UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get();
		CameraComponent->SetFieldOfView(Settings ? Settings->GetFieldOfView() : 90.f);
		// 시체 시점 채도·밝기(시안 saturate(.35) brightness(.7)) — 관전으로 넘어가면 가중치 0
		CameraComponent->PostProcessSettings.bOverride_ColorSaturation = true;
		CameraComponent->PostProcessSettings.ColorSaturation = FVector4(0.35f, 0.35f, 0.35f, 1.f);
		CameraComponent->PostProcessSettings.bOverride_ColorGain = true;
		CameraComponent->PostProcessSettings.ColorGain = FVector4(0.7f, 0.7f, 0.7f, 1.f);
		CameraComponent->PostProcessBlendWeight = 0.f;
	}
	PC->SetViewTarget(Camera);
}

void USpectateComponent::UpdateCorpseCamera()
{
	if (!Camera)
	{
		return;
	}
	const float Alpha = View.Phase == ESpectatePhase::Dying
		? FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(View.PhaseSeconds / FMath::Max(CorpseSettleSeconds, KINDA_SMALL_NUMBER), 0.f, 1.f), 2.f)
		: 1.f;
	const FVector Location = CorpseStartLocation - FVector(0.f, 0.f, CorpseDrop * Alpha);
	const FRotator Rotation(CorpseStartRotation.Pitch, CorpseStartRotation.Yaw, CorpseRoll * Alpha);
	Camera->SetActorLocationAndRotation(Location, Rotation);
	if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
	{
		CameraComponent->PostProcessBlendWeight = Alpha;
	}
}

void USpectateComponent::UpdateOrbitCamera(float DeltaTime, bool bSnap)
{
	const APawn* TargetPawn = FindPawn(Target.Get());
	if (!Camera || !TargetPawn)
	{
		return;
	}

	// 시스템 메뉴 등 UI가 입력을 가진 동안(커서 표시)은 돌리지 않는다
	const APlayerController* OwnerPC = GetOwnerController();
	if (SpectateInput && OwnerPC && !OwnerPC->bShowMouseCursor)
	{
		float MouseX = 0.f;
		float MouseY = 0.f;
		OwnerPC->GetInputMouseDelta(MouseX, MouseY);
		const float StickX = OwnerPC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		const float StickY = OwnerPC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);

		const UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get();
		const float Sensitivity = Settings ? Settings->GetMouseSensitivity() : 1.f;
		Orbit.Yaw += (MouseX * MouseDegreesPerUnit + StickX * GamepadDegreesPerSecond * DeltaTime) * Sensitivity;
		Orbit.Pitch = FMath::Clamp(Orbit.Pitch + (MouseY * MouseDegreesPerUnit + StickY * GamepadDegreesPerSecond * DeltaTime) * Sensitivity, MinPitch, MaxPitch);
	}
	Orbit.Roll = 0.f;

	const FVector DesiredPivot = TargetPawn->GetActorLocation() + FVector(0.f, 0.f, PivotHeight);
	Pivot = bSnap ? DesiredPivot : FMath::VInterpTo(Pivot, DesiredPivot, DeltaTime, PivotInterpSpeed);

	// 스프링암처럼 벽에 막히면 당겨온다
	FVector CameraLocation = Pivot - Orbit.Vector() * OrbitDistance;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SpectateCamera), false);
	Query.AddIgnoredActor(TargetPawn);
	Query.AddIgnoredActor(Camera);
	if (const APlayerController* PC = GetOwnerController())
	{
		Query.AddIgnoredActor(PC->GetPawn());
	}
	FHitResult Hit;
	if (GetWorld()->SweepSingleByChannel(Hit, Pivot, CameraLocation, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(ProbeRadius), Query))
	{
		CameraLocation = Hit.Location;
	}

	Camera->SetActorLocationAndRotation(CameraLocation, Orbit);
	if (UCameraComponent* CameraComponent = Camera->GetCameraComponent())
	{
		CameraComponent->PostProcessBlendWeight = 0.f;
	}
}

// --- 입력 ---

void USpectateComponent::BindInput()
{
	APlayerController* PC = GetOwnerController();
	if (!PC || SpectateInput)
	{
		return;
	}

	SpectateInput = NewObject<UEnhancedInputComponent>(PC, TEXT("SpectateInput"));
	SpectateInput->RegisterComponent();

	// 좌클릭/→/D/RB = 다음, 우클릭/←/A/LB = 이전(Pack 입력 표). 바인딩이 키를 소비해 아래(폰) 입력으로 새지 않는다.
	// EnhancedInputComponent는 레거시 BindKey를 숨겨 두었다 — 컨트롤러의 Escape 바인딩처럼 베이스 타입으로 묶는다.
	UInputComponent* KeyInput = SpectateInput;
	for (const FKey& Key : { EKeys::LeftMouseButton, EKeys::Right, EKeys::D, EKeys::Gamepad_RightShoulder })
	{
		KeyInput->BindKey(Key, IE_Pressed, this, &ThisClass::HandleNext);
	}
	for (const FKey& Key : { EKeys::RightMouseButton, EKeys::Left, EKeys::A, EKeys::Gamepad_LeftShoulder })
	{
		KeyInput->BindKey(Key, IE_Pressed, this, &ThisClass::HandlePrev);
	}
	// 궤도 회전은 축 바인딩 대신 UpdateOrbitCamera가 마우스 델타·우스틱을 직접 읽는다(EnhancedInputComponent는 레거시 축 바인딩 불가)

	if (const UInputAction* Talk = TalkAction.LoadSynchronous())
	{
		SpectateInput->BindAction(Talk, ETriggerEvent::Started, this, &ThisClass::HandleTalkStarted);
		SpectateInput->BindAction(Talk, ETriggerEvent::Completed, this, &ThisClass::HandleTalkCompleted);
	}

	PC->PushInputComponent(SpectateInput);
}

void USpectateComponent::UnbindInput()
{
	if (!SpectateInput)
	{
		return;
	}
	if (APlayerController* PC = GetOwnerController())
	{
		PC->PopInputComponent(SpectateInput);
		PC->ToggleSpeaking(false);
	}
	SpectateInput->DestroyComponent();
	SpectateInput = nullptr;
}

void USpectateComponent::HandleTalkStarted()
{
	if (APlayerController* PC = GetOwnerController())
	{
		PC->ToggleSpeaking(true);
	}
}

void USpectateComponent::HandleTalkCompleted()
{
	if (APlayerController* PC = GetOwnerController())
	{
		PC->ToggleSpeaking(false);
	}
}

// --- 화면 ---

void USpectateComponent::ShowScreen()
{
	APlayerController* PC = GetOwnerController();
	if (!PC || Screen || bSettled)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	if (UClass* Class = ScreenClass.LoadSynchronous())
	{
		Screen = CreateWidget<UUserWidget>(PC, Class);
		if (Screen)
		{
			Screen->AddToViewport(ScreenZOrder);
		}
	}
}

void USpectateComponent::HidePersonalHUD()
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	const APlayerController* PC = GetOwnerController();
	for (UUserWidget* Widget : Widgets)
	{
		if (!Widget || Widget->GetOwningPlayer() != PC || !Widget->IsVisible())
		{
			continue;
		}
		const bool bPersonal = HiddenHUDClasses.ContainsByPredicate([Widget](const TSoftClassPtr<UUserWidget>& Class)
		{
			const UClass* Loaded = Class.Get();
			return Loaded && Widget->IsA(Loaded);
		});
		if (bPersonal)
		{
			HiddenHUD.Emplace(Widget, Widget->GetVisibility());
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void USpectateComponent::RestorePersonalHUD()
{
	for (const TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>& Entry : HiddenHUD)
	{
		if (UUserWidget* Widget = Entry.Key.Get())
		{
			Widget->SetVisibility(Entry.Value);
		}
	}
	HiddenHUD.Reset();
}

void USpectateComponent::HideScreen()
{
	if (Screen)
	{
		Screen->RemoveFromParent();
		Screen = nullptr;
	}
}

void USpectateComponent::RefreshView()
{
	const APlayerController* PC = GetOwnerController();
	const AExplorationGameState* GameState = GetExplorationGameState();

	if (View.DeathCause.IsNone() && PC && PC->GetPawn())
	{
		if (const UHealthComponent* Health = PC->GetPawn()->FindComponentByClass<UHealthComponent>())
		{
			View.DeathCause = Health->GetDeathCause();
		}
	}

	View.Roster.Reset();
	View.AliveCount = 0;
	View.TargetSlot = 0;
	if (GameState)
	{
		for (int32 Index = 0; Index < GameState->PlayerArray.Num(); ++Index)
		{
			const APlayerState* PlayerState = GameState->PlayerArray[Index];
			if (!PlayerState || PlayerState->IsOnlyASpectator())
			{
				continue;
			}
			FSpectateRosterEntry& Entry = View.Roster.AddDefaulted_GetRef();
			Entry.bSelf = PC && PlayerState == PC->PlayerState;
			Entry.bDead = Entry.bSelf || !IsPlayerAlive(PlayerState);
			Entry.bCurrent = PlayerState == Target.Get();
			View.AliveCount += Entry.bDead ? 0 : 1;
			if (Entry.bCurrent)
			{
				View.TargetSlot = View.Roster.Num();
			}
		}
	}

	const APlayerState* TargetState = Target.Get();
	View.TargetName = TargetState ? TargetState->GetPlayerName() : FString();
	const APawn* TargetPawn = FindPawn(TargetState);
	const UHealthComponent* Health = TargetPawn ? TargetPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	const UOxygenComponent* Oxygen = TargetPawn ? TargetPawn->FindComponentByClass<UOxygenComponent>() : nullptr;
	View.bTargetVitalsValid = Health != nullptr;
	View.TargetHP = Health ? Health->GetHP() : 0.f;
	View.TargetHPRatio = Health && Health->GetMaxHP() > 0.f ? FMath::Clamp(Health->GetHP() / Health->GetMaxHP(), 0.f, 1.f) : 0.f;
	View.TargetOxygenRatio = Oxygen && Oxygen->GetMaxOxygen() > 0.f ? FMath::Clamp(Oxygen->GetOxygen() / Oxygen->GetMaxOxygen(), 0.f, 1.f) : 0.f;
}
