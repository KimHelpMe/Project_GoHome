#include "Core/ExpeditionEntryComponent.h"

#include "Core/ExplorationGameMode.h"
#include "Core/ExplorationGameState.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

UExpeditionEntryComponent::UExpeditionEntryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	for (const TCHAR* Path : {
		TEXT("/Game/GoHome/UI/HUD/WBP_HUD.WBP_HUD_C"),
		TEXT("/Game/GoHome/Developers/LSA/UI/WB_HP_OxygenUI.WB_HP_OxygenUI_C"),
		TEXT("/Game/GoHome/UI/InventorySlot/WBP_InventorySlots.WBP_InventorySlots_C"),
		TEXT("/Game/GoHome/UI/WBP_InteractionPrompt.WBP_InteractionPrompt_C"),
		TEXT("/Game/GoHome/UI/WBP_DangerVignette.WBP_DangerVignette_C"),
		TEXT("/Game/GoHome/UI/Compass/WBP_Compass.WBP_Compass_C") })
	{
		LegacyHUDClasses.Add(TSoftClassPtr<UUserWidget>(FSoftClassPath(Path)));
	}
}

void UExpeditionEntryComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshContext();
}

void UExpeditionEntryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetContext();
	Super::EndPlay(EndPlayReason);
}

void UExpeditionEntryComponent::ResetContext()
{
	if (AExplorationGameState* GS = BoundState.Get())
	{
		GS->OnEntryChanged.RemoveDynamic(this, &UExpeditionEntryComponent::HandleEntryChanged);
		GS->OnStateChanged.RemoveDynamic(this, &UExpeditionEntryComponent::HandleStateChanged);
	}
	RestoreHUD();
	RestoreMovement();
	if (bLocalInputLocked)
	{
		if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
		{
			PC->SetIgnoreMoveInput(false);
			PC->SetIgnoreLookInput(false);
		}
	}
	bLocalInputLocked = false;
	bOpeningFinished = false;
	BoundState.Reset();
	ReadyPawn.Reset();
	RegisteredHUD.Reset();
	View = FExpeditionEntryView();
	UpdateOpeningWidget();
	ViewChanged.Broadcast();
}

void UExpeditionEntryComponent::RefreshContext()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	AExplorationGameState* GS = GetWorld() ? GetWorld()->GetGameState<AExplorationGameState>() : nullptr;
	if (!PC) return;
	if (BoundState.Get() != GS)
	{
		ResetContext();
		BoundState = GS;
		if (GS)
		{
			GS->OnEntryChanged.AddDynamic(this, &UExpeditionEntryComponent::HandleEntryChanged);
			GS->OnStateChanged.AddDynamic(this, &UExpeditionEntryComponent::HandleStateChanged);
		}
	}
	HandleEntryChanged();
	if (GS && PC->IsLocalController() && PC->GetPawn() && ReadyPawn.Get() != PC->GetPawn())
	{
		ReadyPawn = PC->GetPawn();
		Server_ReportEntryReady(GS);
	}
}

void UExpeditionEntryComponent::Server_ReportEntryReady_Implementation(AExplorationGameState* Context)
{
	if (AExplorationGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AExplorationGameMode>() : nullptr)
	{
		GM->NotifyEntryReady(Cast<APlayerController>(GetOwner()), Context);
	}
}

bool UExpeditionEntryComponent::IsOpeningExpired() const
{
	const FExpeditionEntryState& State = View.State;
	return State.Phase != EExpeditionEntryPhase::Preparing
		&& GetEntryServerTime() >= State.OpeningStartServerTime + State.OpeningDuration + OpeningTailSeconds;
}

void UExpeditionEntryComponent::UpdateView()
{
	AExplorationGameState* GS = BoundState.Get();
	if (!GS)
	{
		View = FExpeditionEntryView();
		return;
	}
	const bool bExploring = GS->GetCurrentState() == EExpeditionState::Exploration;
	View.Context = GS;
	View.State = GS->GetEntryState();
	View.OpeningTailSeconds = OpeningTailSeconds;
	View.bGameplayReady = bExploring && GS->HasExplorationStarted();
	const bool bHasOpening = View.State.OpeningDuration > 0.f;

	// 서버 시각으로 판정 — 정산/실패, 지연 복제, 늦은 합류는 지난 오프닝을 재생하지 않는다.
	if (!bExploring || (bHasOpening ? IsOpeningExpired() : View.bGameplayReady))
	{
		bOpeningFinished = true;
	}
	View.bOpeningVisible = bExploring && bHasOpening && !bOpeningFinished;
	View.bHUDVisible = bOpeningFinished;
}

void UExpeditionEntryComponent::HandleEntryChanged()
{
	UpdateView();
	ApplyMovementGate();
	ApplyLocalGate();
	UpdateOpeningWidget();
	ViewChanged.Broadcast();
}

void UExpeditionEntryComponent::HandleStateChanged(EExpeditionState State)
{
	HandleEntryChanged();
}

double UExpeditionEntryComponent::GetEntryServerTime() const
{
	const AExplorationGameState* GS = BoundState.Get();
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

void UExpeditionEntryComponent::FinishOpening(UObject* Context)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || !Context || Context != BoundState.Get() || !View.bOpeningVisible) return;
	// 화면만 걷는다 — 탐사 개시(이동·타이머)는 서버 시각 그대로
	bOpeningFinished = true;
	HandleEntryChanged();
}

void UExpeditionEntryComponent::UpdateOpeningWidget()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	UWorld* World = GetWorld();
	const bool bWant = View.bOpeningVisible && World && !World->bIsTearingDown;
	if (bWant && !OpeningWidget)
	{
		if (UClass* Class = OpeningClass.LoadSynchronous())
		{
			OpeningWidget = CreateWidget<UUserWidget>(PC, Class);
			if (OpeningWidget)
			{
				OpeningWidget->AddToViewport(OpeningZOrder);
				BindSkipInput();
			}
		}
	}
	else if (!bWant && OpeningWidget)
	{
		OpeningWidget->RemoveFromParent();
		OpeningWidget = nullptr;
		UnbindSkipInput();
	}
}

void UExpeditionEntryComponent::BindSkipInput()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || SkipInput)
	{
		return;
	}
	SkipInput = NewObject<UEnhancedInputComponent>(PC, TEXT("ExpeditionEntrySkipInput"));
	SkipInput->RegisterComponent();
	// 시안: Space / Esc. 게임패드는 A/B. 바인딩이 키를 소비해 컨트롤러의 Escape(시스템 메뉴)로 새지 않는다.
	for (const FKey& Key : { EKeys::SpaceBar, EKeys::Escape, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right })
	{
		SkipInput->BindKey(Key, IE_Pressed, this, &ThisClass::HandleSkip);
	}
	PC->PushInputComponent(SkipInput);
}

void UExpeditionEntryComponent::UnbindSkipInput()
{
	if (!SkipInput)
	{
		return;
	}
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		PC->PopInputComponent(SkipInput);
	}
	SkipInput->DestroyComponent();
	SkipInput = nullptr;
}

void UExpeditionEntryComponent::HandleSkip()
{
	FinishOpening(BoundState.Get());
}

void UExpeditionEntryComponent::RegisterHUD(UUserWidget* Widget)
{
	if (!Widget) return;
	RegisteredHUD.Add(Widget);
	ApplyLocalGate();
}

void UExpeditionEntryComponent::DiscoverLegacyHUD()
{
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget* Widget : Widgets)
	{
		if (Widget->GetOwningPlayer() != GetOwner()) continue;
		if (LegacyHUDClasses.ContainsByPredicate([Widget](const TSoftClassPtr<UUserWidget>& Class)
		{
			return Class.Get() && Widget->IsA(Class.Get());
		})) RegisteredHUD.Add(Widget);
	}
}

void UExpeditionEntryComponent::RestoreHUD()
{
	for (const auto& Pair : HiddenHUD)
	{
		if (UUserWidget* Widget = Pair.Key.Get())
		{
			// 그 사이 다른 화면이 Collapsed 등으로 바꿨으면 덮어쓰지 않는다.
			if (Widget->GetVisibility() == ESlateVisibility::Hidden) Widget->SetVisibility(Pair.Value);
		}
	}
	HiddenHUD.Reset();
}

void UExpeditionEntryComponent::ApplyLocalGate()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) return;
	AExplorationGameState* GS = BoundState.Get();
	const bool bBlock = GS && GS->IsExpeditionPreparing();
	// 빙의 직후 엔진 ClientRestart가 ResetIgnoreInputFlags로 무시 카운트를 0으로 지운다 — 우리 몫이 사라졌으면 다시 잠근다.
	if (bLocalInputLocked && (!PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored()))
	{
		bLocalInputLocked = false;
		PC->ResetIgnoreInputFlags();
	}
	if (bBlock != bLocalInputLocked)
	{
		PC->SetIgnoreMoveInput(bBlock);
		PC->SetIgnoreLookInput(bBlock);
		bLocalInputLocked = bBlock;
	}
	// 개인 HUD는 오프닝(도착 안내 퇴장까지)이 끝나야 보인다 — 탐사 개시(이동 허용)보다 조금 늦다.
	const bool bHideHUD = GS && GS->GetCurrentState() == EExpeditionState::Exploration && !View.bHUDVisible;
	if (!bHideHUD)
	{
		RestoreHUD();
		return;
	}
	DiscoverLegacyHUD();
	for (const TWeakObjectPtr<UUserWidget>& Ptr : RegisteredHUD)
	{
		if (UUserWidget* Widget = Ptr.Get())
		{
			if (!Widget->IsInViewport()) continue;
			if (!HiddenHUD.Contains(Widget)) HiddenHUD.Add(Widget, Widget->GetVisibility());
			Widget->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

void UExpeditionEntryComponent::RestoreMovement()
{
	if (UCharacterMovementComponent* Move = LockedMovement.Get())
	{
		if (Move->MovementMode == MOVE_None) Move->SetMovementMode(SavedMovementMode, SavedCustomMode);
	}
	LockedMovement.Reset();
}

void UExpeditionEntryComponent::ApplyMovementGate()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->HasAuthority()) return;
	ACharacter* Character = Cast<ACharacter>(PC->GetPawn());
	UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	const bool bBlock = BoundState.IsValid() && BoundState->IsExpeditionPreparing();
	if (LockedMovement.Get() != Move || !bBlock) RestoreMovement();
	// 잠근 뒤 캐릭터 BP가 수영 모드 등으로 바꾸면 그 모드를 복원값으로 삼고 다시 잠근다
	if (bBlock && Move && LockedMovement.Get() == Move && Move->MovementMode != MOVE_None)
	{
		LockedMovement.Reset();
	}
	if (bBlock && Move && !LockedMovement.IsValid())
	{
		LockedMovement = Move;
		SavedMovementMode = Move->MovementMode;
		SavedCustomMode = Move->CustomMovementMode;
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
}

void UExpeditionEntryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 상태 변경은 RepNotify/PC 훅으로 받는다. 여기서는 시간으로 끝나는 오프닝, 지연 생성되는 BP HUD, 폰 이동 잠금만 보완한다.
	AExplorationGameState* GS = BoundState.Get();
	if (!GS)
	{
		return;
	}
	if (View.bOpeningVisible && IsOpeningExpired())
	{
		HandleEntryChanged();
		return;
	}
	if (GS->IsExpeditionPreparing())
	{
		ApplyMovementGate();
	}
	if (!View.bHUDVisible && GS->GetCurrentState() == EExpeditionState::Exploration)
	{
		ApplyLocalGate();
	}
}
