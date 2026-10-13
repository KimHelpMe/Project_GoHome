// 


#include "Core/GoHomePlayerController.h"

#include "Core/HudNoticeSubsystem.h"
#include "Engine/LocalPlayer.h"

#include "Core/LobbyGameState.h"
#include "Core/ExpeditionZoneDataAsset.h"
#include "Core/ExplorationGameState.h"

#include "Core/SessionSubsystem.h"
#include "Core/GoHomeGameUserSettings.h"
#include "Camera/CameraComponent.h"

#include "Blueprint/UserWidget.h"
#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "Upgrade/EquipmentUpgradeSubsystem.h"

#include "Shop/ItemShopSubsystem.h"
#include "Interaction/InventoryComponent.h"
#include "Item/ItemActorBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogPauseMenu, Log, All);

#if !UE_BUILD_SHIPPING
// 세션 정리 실패 → 메뉴 안 오류/재시도 경로를 실제 OnlineSubsystem 실패 없이 확인하기 위한 개발용 스위치
static TAutoConsoleVariable<int32> CVarPauseSimulateLeaveFailure(
	TEXT("GoHome.Pause.SimulateLeaveFailure"),
	0,
	TEXT("1이면 인게임 메뉴의 타이틀 이동/게임 종료가 세션 정리 실패로 끝난다(오류·재시도 UI 검증용)."),
	ECVF_Cheat);
#endif


void AGoHomePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		if (UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get())
		{
			SettingsAppliedHandle = Settings->OnGameplaySettingsApplied.AddUObject(this, &AGoHomePlayerController::ApplyLocalPlayerSettings);
		}
	}
	ApplyLocalPlayerSettings();
	BindGameStateSetEvent();
	RefreshExplorationHUD();
	RefreshProgressHUD();
	BindSettlementDismiss();
}

void AGoHomePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bExplorationHUDActive)
	{
		bExplorationHUDActive = false;
		OnExplorationHUDTeardown();
	}
	if (ProgressHUD)
	{
		ProgressHUD->RemoveFromParent();
		ProgressHUD = nullptr;
	}
	if (HudNoticeFeed)
	{
		HudNoticeFeed->RemoveFromParent();
		HudNoticeFeed = nullptr;
	}
	if (UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get())
	{
		Settings->OnGameplaySettingsApplied.Remove(SettingsAppliedHandle);
	}
	SettingsAppliedHandle.Reset();
	StopWaitingForSession();
	if (PauseMenu)
	{
		PauseMenu->RemoveFromParent();
		PauseMenu = nullptr;
	}
	if (SupplyScreen)
	{
		SupplyScreen->RemoveFromParent();
		SupplyScreen = nullptr;
	}
	if (ZoneSelectScreen)
	{
		ZoneSelectScreen->RemoveFromParent();
		ZoneSelectScreen = nullptr;
	}
	GetWorldTimerManager().ClearTimer(DestinationRequestTimeoutHandle);
	Super::EndPlay(EndPlayReason);
}

void AGoHomePlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	ApplyLocalPlayerSettings();
	BindGameStateSetEvent();
	RefreshExplorationHUD();
	RefreshProgressHUD();
}

void AGoHomePlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	ApplyLocalPlayerSettings();
	BindGameStateSetEvent();
	RefreshExplorationHUD();
	RefreshProgressHUD();

	// 폰이 바뀌면(사망 후 관전 등) 이전 폰 기준으로 연 보급 화면(보관함 내 소지 수량 등)은 닫는다.
	if (IsLocalController() && SupplyScreen)
	{
		CloseSupplyScreen(true);
	}
	if (IsLocalController() && ZoneSelectScreen)
	{
		CloseZoneSelect(true);
	}
}

void AGoHomePlayerController::ApplyLocalPlayerSettings()
{
	if (!IsLocalController())
	{
		return;
	}

	const UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get();
	if (!Settings)
	{
		return;
	}

	if (APawn* ControlledPawn = GetPawn())
	{
		if (UCameraComponent* Camera = ControlledPawn->FindComponentByClass<UCameraComponent>())
		{
			Camera->SetFieldOfView(Settings->GetFieldOfView());
		}
	}
}

void AGoHomePlayerController::BindGameStateSetEvent()
{
	UWorld* World = GetWorld();
	if (!World || BoundGameStateWorld.Get() == World)
	{
		return;
	}

	BoundGameStateWorld = World;
	World->GameStateSetEvent.AddUObject(this, &AGoHomePlayerController::HandleGameStateSet);
}

void AGoHomePlayerController::HandleGameStateSet(AGameStateBase* /*NewGameState*/)
{
	// seamless travel 은 컨트롤러를 살려 둔 채 월드만 바꾼다 — 이전 월드에서 연 메뉴를 새 월드로 끌고 가지 않는다.
	if (PauseMenu && !IsLeaveInFlight())
	{
		ClosePauseMenu();
	}
	if (SupplyScreen)
	{
		CloseSupplyScreen(true);
	}
	if (ZoneSelectScreen)
	{
		CloseZoneSelect(true);
	}
	// 새 월드의 로비 GameState는 다음 열기/요청 때 다시 묶는다. 이전 월드 요청은 버린다.
	BoundLobbyGameState.Reset();
	PendingDestination = NAME_None;
	GetWorldTimerManager().ClearTimer(DestinationRequestTimeoutHandle);
	RefreshExplorationHUD();
	RefreshProgressHUD();
	BindSettlementDismiss();
}

void AGoHomePlayerController::RefreshProgressHUD()
{
	if (!IsLocalController() || !ProgressHUDClass)
	{
		return;
	}

	UWorld* World = GetWorld();
	// seamless travel 중 떠나는 월드에서 SetPawn/GameStateSet 이 불려도 거기엔 만들지 않는다(UMG ensure !bIsTearingDown)
	if (!World || World->bIsTearingDown)
	{
		return;
	}
	AGameStateBase* GameState = World->GetGameState();

	// 로비·탐사 레벨에서만 표시(로딩 맵 등 다른 GameState 에선 띄우지 않음)
	const bool bWantHUD = Cast<ALobbyGameState>(GameState) || Cast<AExplorationGameState>(GameState);
	if (!bWantHUD)
	{
		return;
	}

	if (ProgressHUD && ProgressHUDGameState.Get() == GameState && ProgressHUD->IsInViewport())
	{
		return;
	}

	if (ProgressHUD)
	{
		ProgressHUD->RemoveFromParent();
	}

	ProgressHUD = CreateWidget<UUserWidget>(this, ProgressHUDClass);
	ProgressHUDGameState = GameState;
	if (ProgressHUD)
	{
		ProgressHUD->AddToViewport(ProgressHUDZOrder);
	}

	// 알림 피드도 같은 월드 수명 — 진행도 HUD 와 함께 다시 만든다
	if (HudNoticeFeed)
	{
		HudNoticeFeed->RemoveFromParent();
		HudNoticeFeed = nullptr;
	}
	if (HudNoticeFeedClass)
	{
		HudNoticeFeed = CreateWidget<UUserWidget>(this, HudNoticeFeedClass);
		if (HudNoticeFeed)
		{
			HudNoticeFeed->AddToViewport(HudNoticeFeedZOrder);
		}
	}
}

void AGoHomePlayerController::Client_PushHudNotice_Implementation(const FHudNotice& Notice)
{
	if (UHudNoticeSubsystem* Notices = ULocalPlayer::GetSubsystem<UHudNoticeSubsystem>(GetLocalPlayer()))
	{
		Notices->Push(Notice);
	}
}

void AGoHomePlayerController::RefreshExplorationHUD()
{
	if (!IsLocalController())
	{
		return;
	}

	const bool bWantHUD = GetWorld() && GetWorld()->GetGameState<AExplorationGameState>() != nullptr;
	if (bWantHUD == bExplorationHUDActive)
	{
		return;
	}

	bExplorationHUDActive = bWantHUD;
	if (bWantHUD)
	{
		OnExplorationHUDReady();
	}
	else
	{
		OnExplorationHUDTeardown();
	}
}

void AGoHomePlayerController::Server_SelectZone_Implementation(FName ZoneId)
{
	if (ALobbyGameState* LobbyGameState = GetWorld()->GetGameState<ALobbyGameState>())
	{
		LobbyGameState->SetSelectedZone(ZoneId);
	}
}

void AGoHomePlayerController::Client_OpenSelectZone_Implementation()
{
	OpenZoneSelect();
}

// --- 탐사 목적지 화면 ---

void AGoHomePlayerController::OpenZoneSelect()
{
	if (!IsLocalController() || ZoneSelectScreen || SupplyScreen || PauseMenu)
	{
		return;
	}
	if (!GetWorld() || !GetWorld()->GetGameState<ALobbyGameState>())
	{
		return;
	}

	UClass* WidgetClass = ZoneSelectClass.LoadSynchronous();
	if (!WidgetClass)
	{
		return;
	}

	// 화면이 NativeConstruct에서 목록·현재 값을 읽고 변경 이벤트에 묶는다 — 생성 전에 GameState 구독을 맞춰 둔다.
	BindLobbyDestination();
	ZoneSelectScreen = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!ZoneSelectScreen)
	{
		return;
	}
	ZoneSelectScreen->AddToViewport(ZoneSelectZOrder);

	// Modal 규칙: UIOnly + 커서(CanOpenPauseMenu가 커서로 다른 Modal 소유를 판정)
	FlushPressedKeys();
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ZoneSelectScreen->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AGoHomePlayerController::CloseZoneSelect(bool bRestoreInput)
{
	if (!ZoneSelectScreen)
	{
		return;
	}

	ZoneSelectScreen->RemoveFromParent();
	ZoneSelectScreen = nullptr;

	if (bRestoreInput)
	{
		FlushPressedKeys();
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AGoHomePlayerController::RequestCloseZoneSelect()
{
	CloseZoneSelect(true);
}

TArray<const UExpeditionZoneDataAsset*> AGoHomePlayerController::GetSelectableZones() const
{
	TArray<const UExpeditionZoneDataAsset*> Zones;
	if (const ALobbyGameState* LobbyGameState = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr)
	{
		for (const UExpeditionZoneDataAsset* Zone : LobbyGameState->GetAvailableZones())
		{
			if (Zone)
			{
				Zones.Add(Zone);
			}
		}
	}
	return Zones;
}

FName AGoHomePlayerController::GetTeamDestination() const
{
	const ALobbyGameState* LobbyGameState = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
	return LobbyGameState ? LobbyGameState->GetSelectedZoneId() : NAME_None;
}

bool AGoHomePlayerController::RequestTeamDestination(FName ZoneId)
{
	if (IsDestinationRequestPending() || ZoneId == NAME_None || ZoneId == GetTeamDestination())
	{
		return false;
	}

	BindLobbyDestination();
	PendingDestination = ZoneId;
	GetWorldTimerManager().SetTimer(DestinationRequestTimeoutHandle, this, &ThisClass::HandleDestinationRequestTimeout, DestinationRequestTimeoutSeconds, false);

	// 리슨 호스트는 이 호출 안에서 SetSelectedZone → OnRep → HandleSelectedZoneChanged까지 끝난다(요청 상태를 먼저 세워 둔다).
	Server_SelectZone(ZoneId);
	return true;
}

void AGoHomePlayerController::BindLobbyDestination()
{
	ALobbyGameState* LobbyGameState = GetWorld() ? GetWorld()->GetGameState<ALobbyGameState>() : nullptr;
	if (!LobbyGameState || BoundLobbyGameState == LobbyGameState)
	{
		return;
	}
	BoundLobbyGameState = LobbyGameState;
	LobbyGameState->OnSelectedZoneChanged.AddUniqueDynamic(this, &ThisClass::HandleSelectedZoneChanged);
}

void AGoHomePlayerController::HandleSelectedZoneChanged(FName NewZoneId)
{
	// 클라 OnRep은 서버 값과 무관하게 이 클라가 처음 받는 값에도 불린다 — 내 요청과 같은 값일 때만 내 지정으로 본다.
	const bool bMine = PendingDestination != NAME_None && PendingDestination == NewZoneId;
	if (bMine)
	{
		PendingDestination = NAME_None;
		GetWorldTimerManager().ClearTimer(DestinationRequestTimeoutHandle);
	}
	TeamDestinationChangedEvent.Broadcast(NewZoneId, bMine ? EZoneDestinationChange::Mine : EZoneDestinationChange::Other);
}

void AGoHomePlayerController::HandleDestinationRequestTimeout()
{
	if (PendingDestination == NAME_None)
	{
		return;
	}
	// 같은 값을 다른 팀원이 먼저 지정했으면 서버 값이 안 바뀌어 OnRep이 오지 않는다 — 결과적으로 원하는 값이면 성공.
	const FName Current = GetTeamDestination();
	const bool bReached = Current == PendingDestination;
	PendingDestination = NAME_None;
	TeamDestinationChangedEvent.Broadcast(Current, bReached ? EZoneDestinationChange::Mine : EZoneDestinationChange::Failed);
}

#if !UE_BUILD_SHIPPING
// 모니터 상호작용 없이 화면 확인용: GoHome.ZoneSelect.Open
static FAutoConsoleCommandWithWorldAndArgs GZoneSelectOpenCommand(
	TEXT("GoHome.ZoneSelect.Open"),
	TEXT("탐사 목적지 화면을 연다(로비 전용)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (AGoHomePlayerController* PC = World ? Cast<AGoHomePlayerController>(World->GetFirstPlayerController()) : nullptr)
		{
			PC->OpenZoneSelect();
		}
	}));
#endif

void AGoHomePlayerController::Client_OpenEquipmentUpgrade_Implementation()
{
	OnOpenEquipmentUpgrade();
}

void AGoHomePlayerController::Client_RefreshUpgradeFunds_Implementation(
	int32 InCurrentFunds)
{
	OnUpgradeCurrentFundsChanged(InCurrentFunds);
}

// PlayerController는 클라이언트 UI 요청을 서버로 넘기는 통로만 맡는다.
// 실제 강화 처리는 EquipmentUpgradeSubsystem에 위임한다.
void AGoHomePlayerController::Server_RequestEquipmentUpgrade_Implementation(UEquipmentUpgradeDataAsset* UpgradeData)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UEquipmentUpgradeSubsystem* UpgradeSubsystem = GameInstance->GetSubsystem<UEquipmentUpgradeSubsystem>();
	if (!UpgradeSubsystem)
	{
		return;
	}

	int32 CurrentFunds = 0;

	const EEquipmentUpgradeRequestResult UpgradeResult =
		UpgradeSubsystem->RequestUpgrade(
			this,
			UpgradeData,
			CurrentFunds);

	if (UpgradeResult != EEquipmentUpgradeRequestResult::Succeeded)
	{
		return;
	}

	// 강화 후 최신 코인을 상시 HUD용 GameState 미러에 반영한다(로비·탐사 공통).
	if (UWorld* World = GetWorld())
	{
		if (AGoHomeGameState* GoHomeGameState = World->GetGameState<AGoHomeGameState>())
		{
			GoHomeGameState->SetCurrentFunds(CurrentFunds);
		}
	}

	// 강화 UI에도 최신 코인을 전달한다.
	Client_RefreshUpgradeFunds(CurrentFunds);
}

// PlayerController는 구매 요청을 서버로 전달만 한다.
// 실제 상품 검증은 ItemShopSubsystem이 담당한다.
void AGoHomePlayerController::Server_RequestShopPurchase_Implementation(
	FItemShopPurchaseRequest Request)
{
	if (!HasAuthority())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UItemShopSubsystem* ShopSubsystem =
		GameInstance->GetSubsystem<UItemShopSubsystem>();

	if (!ShopSubsystem)
	{
		return;
	}

	FItemShopPurchaseResult PurchaseResult;

	const bool bSuccess =
		ShopSubsystem->TryProcessPurchase(
			this,
			Request,
			PurchaseResult);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ItemShop] Purchase. Success: %s, RemainingFunds: %d, Result: %d"),
		bSuccess ? TEXT("true") : TEXT("false"),
		PurchaseResult.RemainingFunds,
		static_cast<uint8>(PurchaseResult.Result));

	Client_ShopPurchaseResult(PurchaseResult);
}

void AGoHomePlayerController::Client_ShopPurchaseResult_Implementation(const FItemShopPurchaseResult& Result)
{
	// 보급 화면 요청이었다면 상품·수량을 붙여 알린다(서버 결과에는 상품이 없다).
	const FName ProductId = PendingPurchaseProduct;
	const int32 Quantity = PendingPurchaseQuantity;
	PendingPurchaseProduct = NAME_None;
	PendingPurchaseQuantity = 0;
	SupplyPurchaseResultEvent.Broadcast(Result, ProductId, Quantity);

	OnShopPurchaseResult(Result);
}

// --- 잠수정 공유 보관함 ---

void AGoHomePlayerController::Server_RequestLockerWithdraw_Implementation(FName ProductId)
{
	UItemShopSubsystem* ShopSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UItemShopSubsystem>() : nullptr;
	if (!ShopSubsystem)
	{
		return;
	}

	FSharedLockerResult Result;
	ShopSubsystem->TryWithdrawFromLocker(this, ProductId, Result);
	Client_SharedLockerResult(Result);
}

void AGoHomePlayerController::Server_RequestLockerDeposit_Implementation(FName ProductId)
{
	UItemShopSubsystem* ShopSubsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UItemShopSubsystem>() : nullptr;
	if (!ShopSubsystem)
	{
		return;
	}

	FSharedLockerResult Result;
	ShopSubsystem->TryDepositToLocker(this, ProductId, Result);
	Client_SharedLockerResult(Result);
}

void AGoHomePlayerController::Client_SharedLockerResult_Implementation(const FSharedLockerResult& Result)
{
	bLockerRequestPending = false;
	SharedLockerResultEvent.Broadcast(Result);
}

bool AGoHomePlayerController::RequestLockerTransfer(ESharedLockerAction Action, FName ProductId)
{
	if (bLockerRequestPending || ProductId == NAME_None)
	{
		return false;
	}
	bLockerRequestPending = true;
	if (Action == ESharedLockerAction::Withdraw)
	{
		Server_RequestLockerWithdraw(ProductId);
	}
	else
	{
		Server_RequestLockerDeposit(ProductId);
	}
	return true;
}

int32 AGoHomePlayerController::GetHeldLockerQuantity(FName ProductId) const
{
	const APawn* MyPawn = GetPawn();
	const UInventoryComponent* Inventory = MyPawn ? MyPawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory || ProductId == NAME_None)
	{
		return 0;
	}
	int32 Count = 0;
	for (int32 Slot = 0; Slot < Inventory->GetInventorySlotCount(); ++Slot)
	{
		const AItemActorBase* Item = Inventory->GetItemInSlot(Slot);
		Count += Item && Item->GetSharedLockerProductId() == ProductId ? 1 : 0;
	}
	return Count;
}

// --- 보급 화면(로비 메인 분기·상점·팀 보관함) ---

void AGoHomePlayerController::Client_OpenSupplyScreen_Implementation(ESupplyEntry Entry)
{
	OpenSupplyScreen(Entry);
}

void AGoHomePlayerController::OpenSupplyScreen(ESupplyEntry Entry)
{
	if (!IsLocalController() || SupplyScreen || ZoneSelectScreen || PauseMenu)
	{
		return;
	}

	UClass* WidgetClass = SupplyScreenClass.LoadSynchronous();
	if (!WidgetClass)
	{
		return;
	}

	// 화면이 NativeConstruct에서 진입 경로를 묻는다(GetSupplyEntry) — 생성 전에 정해 둔다.
	SupplyEntry = Entry;
	SupplyScreen = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!SupplyScreen)
	{
		return;
	}
	SupplyScreen->AddToViewport(SupplyScreenZOrder);

	// Modal 규칙: UIOnly + 커서. 커서가 켜져 있으면 인게임 시스템 메뉴는 열리지 않는다(CanOpenPauseMenu).
	FlushPressedKeys();
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(SupplyScreen->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AGoHomePlayerController::CloseSupplyScreen(bool bRestoreInput)
{
	if (!SupplyScreen)
	{
		return;
	}

	SupplyScreen->RemoveFromParent();
	SupplyScreen = nullptr;

	if (bRestoreInput)
	{
		FlushPressedKeys();
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AGoHomePlayerController::RequestCloseSupplyScreen()
{
	CloseSupplyScreen(true);
}

const UItemShopCatalogDataAsset* AGoHomePlayerController::GetShopCatalog() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UItemShopSubsystem* ShopSubsystem = GameInstance ? GameInstance->GetSubsystem<UItemShopSubsystem>() : nullptr;
	return ShopSubsystem ? ShopSubsystem->GetCatalog() : nullptr;
}

bool AGoHomePlayerController::RequestShopPurchase(FName ProductId, int32 Quantity)
{
	if (IsShopPurchasePending() || ProductId == NAME_None || Quantity <= 0)
	{
		return false;
	}

	PendingPurchaseProduct = ProductId;
	PendingPurchaseQuantity = Quantity;

	FItemShopPurchaseRequest Request;
	FItemShopCartLine& Line = Request.Lines.AddDefaulted_GetRef();
	Line.ProductId = ProductId;
	Line.Quantity = Quantity;
	Server_RequestShopPurchase(Request);
	return true;
}

#if !UE_BUILD_SHIPPING
// 오브젝트 배선 전 화면 확인용: GoHome.Supply.Open [lobby|expedition]
static FAutoConsoleCommandWithWorldAndArgs GSupplyOpenCommand(
	TEXT("GoHome.Supply.Open"),
	TEXT("보급 화면을 연다. 인자: lobby(기본, 메인 분기) | expedition(탐사 아이템 박스 보관함)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		AGoHomePlayerController* PC = World ? Cast<AGoHomePlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (PC)
		{
			const bool bExpedition = Args.Num() > 0 && Args[0].Equals(TEXT("expedition"), ESearchCase::IgnoreCase);
			PC->OpenSupplyScreen(bExpedition ? ESupplyEntry::ExpeditionLocker : ESupplyEntry::LobbyHub);
		}
	}));
#endif

// 상점 열기
void AGoHomePlayerController::Client_OpenShop_Implementation()
{
	OnOpenShop();
}


// --- 인게임 시스템 메뉴 ---

void AGoHomePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 열기만 키에 직접 묶는다(IMC 에셋 불필요). 열린 뒤엔 UIOnly라 키가 게임 입력으로 오지 않고 위젯 NativeOnKeyDown이 받는다.
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::OpenPauseMenu);
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &ThisClass::OpenPauseMenu);
}

bool AGoHomePlayerController::CanOpenPauseMenu() const
{
	if (!IsLocalController() || !PauseMenuClass || PauseMenu || IsLeaveInFlight())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		return false;
	}

	const AGameStateBase* GameState = World->GetGameState();
	if (!Cast<ALobbyGameState>(GameState) && !Cast<AExplorationGameState>(GameState))
	{
		return false;
	}

	// 다른 Modal(존 선택·강화·상점·관전·정산)은 열면서 커서를 켠다 — 그동안 Escape는 그 화면 몫이다.
	// UIOnly Modal이면 키가 여기까지 오지도 않고, GameAndUI Modal(강화)이 처리하지 않은 Escape는 여기서 걸러진다.
	return !bShowMouseCursor;
}

void AGoHomePlayerController::OpenPauseMenu()
{
	// 이미 열려 있는데 키가 게임 입력으로 왔다 = 누군가(폰 BeginPlay 등) 입력 모드를 바꿔 놓았다. 메뉴로 입력을 되찾는다.
	if (PauseMenu && PauseMenu->IsInViewport())
	{
		ApplyPauseInputMode();
		return;
	}

	if (!CanOpenPauseMenu())
	{
		return;
	}

	PauseMenu = CreateWidget<UUserWidget>(this, PauseMenuClass);
	if (!PauseMenu)
	{
		return;
	}
	PauseMenu->AddToViewport(PauseMenuZOrder);

	// 이동 키를 누른 채 열면 그 입력이 남아 계속 움직이지 않게. 세션 전체 일시정지(SetPause)는 하지 않는다.
	FlushPressedKeys();
	ApplyPauseInputMode();

	UE_LOG(LogPauseMenu, Log, TEXT("Pause menu opened (%s)"), GetSessionRole() == EPauseSessionRole::Host ? TEXT("host") : TEXT("participant"));
}

void AGoHomePlayerController::ClosePauseMenu()
{
	if (!PauseMenu)
	{
		return;
	}

	PauseMenu->RemoveFromParent();
	PauseMenu = nullptr;

	// 메뉴는 커서가 꺼진 순수 게임플레이 상태에서만 열리므로 그 상태로 되돌린다.
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	UE_LOG(LogPauseMenu, Log, TEXT("Pause menu closed"));
}

void AGoHomePlayerController::ApplyPauseInputMode()
{
	if (!PauseMenu)
	{
		return;
	}

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AGoHomePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!PauseMenu)
	{
		return;
	}

	// 월드 교체 등으로 위젯이 뷰포트에서 떨어졌으면 포인터만 정리 — 남겨 두면 다시는 메뉴가 안 열린다.
	if (!PauseMenu->IsInViewport())
	{
		PauseMenu = nullptr;
		return;
	}

	// 메뉴가 열린 동안 커서가 꺼졌다 = 다른 코드가 GameOnly로 되돌렸다(폰이 늦게 도착해 BeginPlay 등). 입력을 되찾는다.
	if (!bShowMouseCursor)
	{
		UE_LOG(LogPauseMenu, Log, TEXT("Input mode changed while pause menu open; reapplying"));
		ApplyPauseInputMode();
	}
}

void AGoHomePlayerController::DismissPauseMenuWithoutInputRestore()
{
	if (!PauseMenu)
	{
		return;
	}

	PauseMenu->RemoveFromParent();
	PauseMenu = nullptr;
	UE_LOG(LogPauseMenu, Log, TEXT("Pause menu dismissed by another screen"));
}

void AGoHomePlayerController::BindSettlementDismiss()
{
	if (!IsLocalController())
	{
		return;
	}

	AExplorationGameState* ExplorationState = GetWorld() ? GetWorld()->GetGameState<AExplorationGameState>() : nullptr;
	if (!ExplorationState || SettlementDismissGameState.Get() == ExplorationState)
	{
		return;
	}

	SettlementDismissGameState = ExplorationState;
	ExplorationState->OnSettlementReady.AddUniqueDynamic(this, &ThisClass::HandleSettlementReadyForPause);
}

void AGoHomePlayerController::HandleSettlementReadyForPause(const FSettlementResult& /*Result*/)
{
	// 정산/실패 화면이 입력(UIOnly·커서)을 가져간다 — 메뉴가 위에 남아 포커스를 다투지 않게 걷어낸다.
	// 나가기 진행 중이면 그대로 둔다(정리 완료 후 이동/종료가 우선).
	if (!IsLeaveInFlight())
	{
		DismissPauseMenuWithoutInputRestore();
	}

	// 보급 화면(탐사 보관함)도 같은 이유로 걷어낸다(입력 모드는 정산 화면 몫).
	CloseSupplyScreen(false);

	// 이동 키를 누른 채 정산이 UIOnly로 입력을 가져가면 키 뗌이 위젯으로 가서 눌림 상태가 남는다 — 로비 복귀 후에도 계속 움직임.
	FlushPressedKeys();
}

EPauseSessionRole AGoHomePlayerController::GetSessionRole() const
{
	// 리슨 서버(또는 혼자 하는 Standalone)가 세션 주인이다.
	return GetNetMode() == NM_Client ? EPauseSessionRole::Participant : EPauseSessionRole::Host;
}

void AGoHomePlayerController::RequestResume()
{
	if (IsLeaveInFlight())
	{
		return;
	}
	ClosePauseMenu();
}

void AGoHomePlayerController::RequestLeave(EPauseLeaveTarget Target)
{
	if (IsLeaveInFlight() || !IsLocalController())
	{
		return;
	}

	PendingLeaveTarget = Target;
	UE_LOG(LogPauseMenu, Log, TEXT("Leave requested: %s, role=%s"),
		Target == EPauseLeaveTarget::Title ? TEXT("title") : TEXT("quit"),
		GetSessionRole() == EPauseSessionRole::Host ? TEXT("host") : TEXT("participant"));

#if !UE_BUILD_SHIPPING
	if (CVarPauseSimulateLeaveFailure.GetValueOnGameThread() != 0)
	{
		// 실제 OnlineSubsystem 응답처럼 비동기로 실패시킨다(진행 중 표시가 보이도록 짧게 지연).
		FTimerDelegate Fail = FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			FailLeave(NSLOCTEXT("PauseMenu", "LeaveFailedSimulated", "세션을 정리하지 못했습니다. (테스트용 실패)"));
		});
		GetGameInstance()->GetTimerManager().SetTimer(LeaveTimeoutHandle, Fail, 0.75f, false);
		return;
	}
#endif

	StartSessionCleanup();
}

void AGoHomePlayerController::StartSessionCleanup()
{
	USessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<USessionSubsystem>() : nullptr;
	if (!Sessions || !Sessions->HasActiveSession())
	{
		// 정리할 온라인 세션이 없다(세션 없이 맵을 연 PIE 등) — 이동하면서 연결만 끊는다.
		UE_LOG(LogPauseMenu, Log, TEXT("No active online session; leaving directly"));
		CompleteLeave();
		return;
	}

	bWaitingForSessionDestroy = true;
	Sessions->OnDestroyComplete.AddUniqueDynamic(this, &ThisClass::HandleSessionDestroyed);
	// seamless travel로 월드가 바뀌어도 타임아웃이 살아 있도록 GameInstance 타이머를 쓴다.
	GetGameInstance()->GetTimerManager().SetTimer(LeaveTimeoutHandle, this, &ThisClass::HandleLeaveTimeout, LeaveTimeoutSeconds, false);

	if (Sessions->IsDestroyInProgress())
	{
		// 앞선 요청이 타임아웃된 뒤 재시도 — 진행 중인 파괴를 중복 요청하지 않고 그 완료를 다시 기다린다.
		UE_LOG(LogPauseMenu, Log, TEXT("DestroySession already in progress; waiting for it"));
		return;
	}

	// 실패를 동기로 브로드캐스트할 수도 있다 — 그 경우 HandleSessionDestroyed가 이 안에서 처리한다.
	Sessions->DestroySession();
}

void AGoHomePlayerController::HandleSessionDestroyed(bool bWasSuccessful)
{
	if (!bWaitingForSessionDestroy)
	{
		return;
	}
	StopWaitingForSession();

	UE_LOG(LogPauseMenu, Log, TEXT("DestroySession complete: %s"), bWasSuccessful ? TEXT("success") : TEXT("failure"));
	if (bWasSuccessful)
	{
		CompleteLeave();
	}
	else
	{
		FailLeave(NSLOCTEXT("PauseMenu", "LeaveFailed", "세션을 정리하지 못했습니다. 다시 시도하세요."));
	}
}

void AGoHomePlayerController::HandleLeaveTimeout()
{
	StopWaitingForSession();
	FailLeave(NSLOCTEXT("PauseMenu", "LeaveTimeout", "세션 정리 응답이 없습니다. 다시 시도하세요."));
}

void AGoHomePlayerController::StopWaitingForSession()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetTimerManager().ClearTimer(LeaveTimeoutHandle);
	}

	if (bWaitingForSessionDestroy)
	{
		bWaitingForSessionDestroy = false;
		if (USessionSubsystem* Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<USessionSubsystem>() : nullptr)
		{
			Sessions->OnDestroyComplete.RemoveDynamic(this, &ThisClass::HandleSessionDestroyed);
		}
	}
}

void AGoHomePlayerController::FailLeave(const FText& Reason)
{
	if (!PendingLeaveTarget.IsSet())
	{
		return;
	}

	const EPauseLeaveTarget Target = PendingLeaveTarget.GetValue();
	PendingLeaveTarget.Reset();
	UE_LOG(LogPauseMenu, Warning, TEXT("Leave failed: %s"), *Reason.ToString());
	LeaveFailedEvent.Broadcast(Target, Reason);
}

void AGoHomePlayerController::CompleteLeave()
{
	// 이동/종료가 끝날 때까지 PendingLeaveTarget을 유지해 메뉴 입력(취소·재요청)을 계속 잠근다.
	const EPauseLeaveTarget Target = PendingLeaveTarget.Get(EPauseLeaveTarget::Title);
	UE_LOG(LogPauseMenu, Log, TEXT("Session cleanup done; %s"), Target == EPauseLeaveTarget::QuitGame ? TEXT("quitting") : TEXT("opening title"));

	if (Target == EPauseLeaveTarget::QuitGame)
	{
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	}
	else
	{
		// 로컬 맵을 열면 클라는 서버 연결을 끊고, 리슨 호스트는 서버를 닫는다(참가자는 연결 끊김으로 기본 맵 복귀).
		UGameplayStatics::OpenLevel(this, FName(*TitleMapPath));
	}
}
