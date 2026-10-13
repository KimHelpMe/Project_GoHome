// 

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/HudNoticeTypes.h"
#include "Core/PauseMenuBackend.h"
#include "Core/SpectateBackend.h"
#include "Core/SupplyScreenBackend.h"
#include "Core/ZoneSelectBackend.h"
#include "Data/FSettlementResult.h"
#include "Shop/ItemShopTypes.h"
#include "GoHomePlayerController.generated.h"

class UEquipmentUpgradeDataAsset;
class AGameStateBase;
class UUserWidget;
class USpectateComponent;
class UAnnouncerComponent;
class UExpeditionEntryComponent;

/**
 *
 */
UCLASS()
class GOHOME_API AGoHomePlayerController : public APlayerController, public IPauseMenuBackend, public ISupplyScreenBackend, public IZoneSelectBackend, public ISpectateBackend
{
	GENERATED_BODY()

public:
	AGoHomePlayerController();

	// ISpectateBackend — 사망 → 관전 흐름은 USpectateComponent가 소유한다
	virtual const FSpectateView& GetSpectateView() const override;

	// ISupplyScreenBackend
	virtual ESupplyEntry GetSupplyEntry() const override { return SupplyEntry; }
	virtual void RequestCloseSupplyScreen() override;
	virtual const UItemShopCatalogDataAsset* GetShopCatalog() const override;
	virtual bool RequestShopPurchase(FName ProductId, int32 Quantity) override;
	virtual bool IsShopPurchasePending() const override { return PendingPurchaseProduct != NAME_None; }
	virtual FSupplyPurchaseResultEvent& OnSupplyPurchaseResult() override { return SupplyPurchaseResultEvent; }
	virtual bool RequestLockerTransfer(ESharedLockerAction Action, FName ProductId) override;
	virtual bool IsLockerRequestPending() const override { return bLockerRequestPending; }
	virtual FSharedLockerResultEvent& OnSupplyLockerResult() override { return SharedLockerResultEvent; }
	virtual int32 GetHeldLockerQuantity(FName ProductId) const override;

	UFUNCTION(BlueprintPure, Category = "UI|Supply")
	bool IsSupplyScreenOpen() const { return SupplyScreen != nullptr; }

	// 보급 화면(분기·상점·보관함) Modal을 연다. 로컬 컨트롤러 전용 — 서버 오브젝트는 Client_OpenSupplyScreen.
	UFUNCTION(BlueprintCallable, Category = "UI|Supply")
	void OpenSupplyScreen(ESupplyEntry Entry);

	UFUNCTION(Client, Reliable)
	void Client_OpenSupplyScreen(ESupplyEntry Entry);

	// IPauseMenuBackend
	virtual EPauseSessionRole GetSessionRole() const override;
	virtual void RequestResume() override;
	virtual void RequestLeave(EPauseLeaveTarget Target) override;
	virtual bool IsLeaveInFlight() const override { return PendingLeaveTarget.IsSet(); }
	virtual FPauseLeaveFailedEvent& OnLeaveFailed() override { return LeaveFailedEvent; }

	UFUNCTION(BlueprintPure, Category = "UI|Pause")
	bool IsPauseMenuOpen() const { return PauseMenu != nullptr; }

	// Escape / 게임패드 Start. 메뉴가 열려 있으면 위젯이 키를 먼저 받으므로 여기 오는 건 "열기"뿐이다.
	UFUNCTION(BlueprintCallable, Category = "UI|Pause")
	void OpenPauseMenu();

	// 탐사 HUD 묶음(WBP_HUD) 생성/파괴 지점. 실제 CreateWidget + AddToViewport 는 BP_GoHomePlayerController 가 한다.
	// Ready  : 로컬 컨트롤러 + AExplorationGameState 유효할 때 1회 (탐사 레벨 진입).
	// Teardown: 탐사 레벨을 벗어날 때 1회 (로비 복귀 / 접속 종료).
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnExplorationHUDReady();

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnExplorationHUDTeardown();

	// IZoneSelectBackend
	virtual TArray<const UExpeditionZoneDataAsset*> GetSelectableZones() const override;
	virtual FName GetTeamDestination() const override;
	virtual bool RequestTeamDestination(FName ZoneId) override;
	virtual bool IsDestinationRequestPending() const override { return PendingDestination != NAME_None; }
	virtual FZoneDestinationChangedEvent& OnTeamDestinationChanged() override { return TeamDestinationChangedEvent; }
	virtual FName GetLastPreviewedZone() const override { return LastPreviewedZone; }
	virtual void SetLastPreviewedZone(FName ZoneId) override { LastPreviewedZone = ZoneId; }
	virtual void RequestCloseZoneSelect() override;

	UFUNCTION(BlueprintPure, Category = "UI|ZoneSelect")
	bool IsZoneSelectOpen() const { return ZoneSelectScreen != nullptr; }

	// 탐사 목적지 Modal을 연다. 로컬 컨트롤러 전용 — 서버 오브젝트(ZoneSelectMonitor)는 Client_OpenSelectZone.
	UFUNCTION(BlueprintCallable, Category = "UI|ZoneSelect")
	void OpenZoneSelect();

	// 팀 목적지 지정 요청. UI는 IZoneSelectBackend::RequestTeamDestination으로만 부른다. 서버 검증은 ALobbyGameState::SetSelectedZone.
	UFUNCTION(Server, Reliable)
	void Server_SelectZone(FName ZoneId);

	UFUNCTION(Client, Reliable)
	void Client_OpenSelectZone();

	// 강화 관련
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnOpenEquipmentUpgrade();

	UFUNCTION(Client, Reliable)
	void Client_OpenEquipmentUpgrade();

	UFUNCTION(Client, Reliable)
	void Client_RefreshUpgradeFunds(int32 InCurrentFunds);

	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnUpgradeCurrentFundsChanged(int32 InCurrentFunds);

	// 상점 관련
	UFUNCTION(BlueprintImplementableEvent, Category = "UI")
	void OnOpenShop();

	UFUNCTION(Client, Reliable)
	void Client_OpenShop();

	// 클라이언트 UI에서 누른 강화 요청을 서버로 전달한다.
	// 실제 처리 로직은 EquipmentUpgradeSubsystem에서 담당한다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Equipment Upgrade")
	void Server_RequestEquipmentUpgrade(UEquipmentUpgradeDataAsset* UpgradeData);

	// 상점 관련 - UI의 구매 주문서를 서버로 전달한다.
	// 실제 상점 처리는 ItemShopSubsystem이 담당한다.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Item Shop")
	void Server_RequestShopPurchase(FItemShopPurchaseRequest Request);

	// 구매 결과(성공 시 상품은 플레이어가 아니라 잠수정 공유 보관함으로 간다). 상점 BP가 문구를 띄울 때 쓴다.
	UFUNCTION(Client, Reliable)
	void Client_ShopPurchaseResult(const FItemShopPurchaseResult& Result);

	UFUNCTION(BlueprintImplementableEvent, Category = "Item Shop")
	void OnShopPurchaseResult(const FItemShopPurchaseResult& Result);

	// 공유 보관함 — UI는 ISupplyScreenBackend::RequestLockerTransfer로만 부른다. 서버 검증은 UItemShopSubsystem.
	UFUNCTION(Server, Reliable)
	void Server_RequestLockerWithdraw(FName ProductId);

	UFUNCTION(Server, Reliable)
	void Server_RequestLockerDeposit(FName ProductId);

	UFUNCTION(Client, Reliable)
	void Client_SharedLockerResult(const FSharedLockerResult& Result);

	// 서버(UHudNoticeSubsystem::SendToPawnOwner) → 이 플레이어 화면 우측 알림 피드에 한 줄.
	UFUNCTION(Client, Reliable)
	void Client_PushHudNotice(const FHudNotice& Notice);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_Pawn() override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// 로비·탐사 공용 인게임 시스템 메뉴(UPauseMenuWidget 부모 WBP). 비어 있으면 Escape 메뉴가 열리지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|Pause")
	TSubclassOf<UUserWidget> PauseMenuClass;

	// HUD·Modal 위, 정산 같은 Sequence보다는 아래에 두지 않는다 — 열려 있는 동안은 이게 최상단이어야 포커스가 안 샌다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|Pause")
	int32 PauseMenuZOrder = 100;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Pause")
	FString TitleMapPath = TEXT("/Game/GoHome/Maps/LV_Title");

	// OnlineSubsystem이 DestroySession 콜백을 끝내 안 주는 경우의 실패 처리 시간
	UPROPERTY(EditDefaultsOnly, Category = "UI|Pause")
	float LeaveTimeoutSeconds = 10.f;

	// 로비·탐사 공통 상시 진행도 HUD(라운드·다음 관문·보유 자금, 탐사맵이면 할당량). BP에서 위젯 클래스를 지정한다.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> ProgressHUDClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	int32 ProgressHUDZOrder = 0;

	// 우측 알림 피드(UHudNoticeFeedWidget 부모 WBP). 진행도 HUD 와 같은 시점(로비·탐사 GameState 마다)에 (재)생성한다.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> HudNoticeFeedClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	int32 HudNoticeFeedZOrder = 1;

	// 유물 조준 정보(UItemInspectWidget — 트리까지 C++). 알림 피드와 같은 시점에 (재)생성한다. 피드와 겹치면 이게 위.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> ItemInspectClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.ItemInspectWidget")));

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	int32 ItemInspectZOrder = 2;

	// 아나운서(UAnnouncerWidget — 트리까지 C++). 탐사 월드에서만 알림 피드와 같은 시점에 (재)생성한다. 관전 중에도 유지(팀 시간 안내).
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> AnnouncerClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.AnnouncerWidget")));

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	int32 AnnouncerZOrder = 3;

	// 좌하단 바이탈 계기(UVitalsWidget — 트리까지 C++, 산소 바늘 계기 + HP). 탐사 월드에서만 알림 피드와 같은 시점에 (재)생성한다.
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSoftClassPtr<UUserWidget> VitalsClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.VitalsWidget")));

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	int32 VitalsZOrder = 0;

	// 보급 화면 Modal 클래스. 기본값은 C++ USupplyScreenWidget(트리를 C++이 만든다) — WBP 없이도 열린다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|Supply")
	TSoftClassPtr<UUserWidget> SupplyScreenClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.SupplyScreenWidget")));

	// 전체 화면 Modal — 탐사 HUD(WBP_HUD, BP가 ZOrder 100으로 추가: 조준점 포함)보다 위.
	// 열려 있는 동안 커서가 켜져 시스템 메뉴는 열리지 않으므로 그보다 높아도 겹치지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|Supply")
	int32 SupplyScreenZOrder = 110;

	// 탐사 목적지 Modal 클래스. 기본값은 C++ UZoneSelectScreenWidget(트리를 C++이 만든다) — WBP 없이도 열린다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|ZoneSelect")
	TSoftClassPtr<UUserWidget> ZoneSelectClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.ZoneSelectScreenWidget")));

	UPROPERTY(EditDefaultsOnly, Category = "UI|ZoneSelect")
	int32 ZoneSelectZOrder = 110;

	// 지정 요청이 이 시간 안에 복제되지 않으면 실패로 알린다(서버는 거절을 따로 알려 주지 않는다)
	UPROPERTY(EditDefaultsOnly, Category = "UI|ZoneSelect")
	float DestinationRequestTimeoutSeconds = 5.f;

private:
	// 로컬 컨트롤러에서 현재 월드의 GameState 로 탐사 레벨 여부를 판정해 Ready/Teardown 을 엣지에서 1회씩 쏜다.
	void RefreshExplorationHUD();

	// 로비/탐사 GameState 가 잡힐 때마다 진행도 HUD 를 그 GameState 기준으로 (재)생성한다.
	// seamless travel 은 컨트롤러를 살려 둔 채 월드만 바꾸므로 BeginPlay 1회 생성으로는 탐사맵에서 사라진다.
	void RefreshProgressHUD();

	// 로컬 사용자 FOV를 폰 획득/재획득·설정 적용 때 카메라에 적용한다.
	void ApplyLocalPlayerSettings();

	FDelegateHandle SettingsAppliedHandle;

	UPROPERTY(VisibleAnywhere, Category = "Spectate")
	TObjectPtr<USpectateComponent> SpectateComponent;

	// 아나운서 사건 수신·큐·표시 데이터(서버: 개인 사건 발화 간격, 로컬: 팀 진행 판정·대기열)
	UPROPERTY(VisibleAnywhere, Category = "Announcer")
	TObjectPtr<UAnnouncerComponent> AnnouncerComponent;

	// 탐사 진입(팀 대기 → 오프닝 → 탐사 개시) 게이트. 오프닝이 끝나야(View.bHUDVisible) 탐사 HUD를 만든다.
	UPROPERTY(VisibleAnywhere, Category = "Expedition|Entry")
	TObjectPtr<UExpeditionEntryComponent> EntryComponent;

	// 탐사 월드에서 오프닝이 끝나기 전이면 false — HUD 생성(진행도·피드·바이탈·아나운서·WBP_HUD)을 미룬다
	bool IsExplorationHUDAllowed() const;
	void HandleEntryViewChanged();
	void RemoveProgressHUDWidgets();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ProgressHUD;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HudNoticeFeed;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ItemInspect;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Announcer;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Vitals;

	// ProgressHUD 가 생성될 때의 GameState — 다르면(새 월드) 다시 만든다
	TWeakObjectPtr<AGameStateBase> ProgressHUDGameState;

	// GameState 가 Pawn 보다 늦게 복제되는 경우를 위해 월드마다 GameStateSetEvent 에 재바인딩.
	void BindGameStateSetEvent();
	void HandleGameStateSet(AGameStateBase* NewGameState);

	bool bExplorationHUDActive = false;
	TWeakObjectPtr<UWorld> BoundGameStateWorld;

	// --- 인게임 시스템 메뉴 ---
	bool CanOpenPauseMenu() const;
	void ClosePauseMenu();
	void ApplyPauseInputMode();

	// 다른 화면이 입력을 가져가는 순간(정산) 메뉴만 걷어낸다 — 입력 모드는 그 화면 몫이라 건드리지 않는다.
	void DismissPauseMenuWithoutInputRestore();
	void BindSettlementDismiss();
	UFUNCTION()
	void HandleSettlementReadyForPause(const FSettlementResult& Result);
	TWeakObjectPtr<AGameStateBase> SettlementDismissGameState;

	void StartSessionCleanup();
	UFUNCTION()
	void HandleSessionDestroyed(bool bWasSuccessful);
	void HandleLeaveTimeout();
	void FailLeave(const FText& Reason);
	void CompleteLeave();
	void StopWaitingForSession();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PauseMenu;

	// --- 보급 화면 ---
	void CloseSupplyScreen(bool bRestoreInput);

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SupplyScreen;

	ESupplyEntry SupplyEntry = ESupplyEntry::LobbyHub;

	// --- 탐사 목적지 화면 ---
	void CloseZoneSelect(bool bRestoreInput);
	void BindLobbyDestination();
	UFUNCTION()
	void HandleSelectedZoneChanged(FName NewZoneId);
	void HandleDestinationRequestTimeout();

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ZoneSelectScreen;

	TWeakObjectPtr<AGameStateBase> BoundLobbyGameState;
	FName PendingDestination = NAME_None;
	FName LastPreviewedZone = NAME_None;
	FTimerHandle DestinationRequestTimeoutHandle;
	FZoneDestinationChangedEvent TeamDestinationChangedEvent;

	// 보급 화면 구매 요청(한 번에 하나). 결과가 오면 비운다.
	FName PendingPurchaseProduct = NAME_None;
	int32 PendingPurchaseQuantity = 0;
	FSupplyPurchaseResultEvent SupplyPurchaseResultEvent;

	// 보관함 꺼내기/넣기 요청 진행 중(결과 RPC가 오면 해제). 화면을 닫았다 열어도 유지.
	bool bLockerRequestPending = false;

	// 보관함 꺼내기/넣기 결과(Client_SharedLockerResult → 보급 화면)
	FSharedLockerResultEvent SharedLockerResultEvent;

	TOptional<EPauseLeaveTarget> PendingLeaveTarget;
	FPauseLeaveFailedEvent LeaveFailedEvent;
	FTimerHandle LeaveTimeoutHandle;
	bool bWaitingForSessionDestroy = false;
};
