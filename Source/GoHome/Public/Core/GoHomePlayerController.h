// 

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/HudNoticeTypes.h"
#include "Core/PauseMenuBackend.h"
#include "Core/SharedLockerBackend.h"
#include "Data/FSettlementResult.h"
#include "Shop/ItemShopTypes.h"
#include "GoHomePlayerController.generated.h"

class UEquipmentUpgradeDataAsset;
class AGameStateBase;
class UUserWidget;

/**
 *
 */
UCLASS()
class GOHOME_API AGoHomePlayerController : public APlayerController, public IPauseMenuBackend, public ISharedLockerBackend
{
	GENERATED_BODY()

public:
	// ISharedLockerBackend
	virtual void RequestLockerWithdraw(FName ProductId) override;
	virtual void RequestLockerDeposit(FName ProductId) override;
	virtual void RequestCloseSharedLocker() override;
	virtual FSharedLockerResultEvent& OnSharedLockerResult() override { return SharedLockerResultEvent; }

	UFUNCTION(BlueprintPure, Category = "UI|Shared Locker")
	bool IsSharedLockerOpen() const { return SharedLockerWidget != nullptr; }

	// 서버(ASharedLockerActor::OnInteract) → 상호작용한 플레이어 화면에 보관함 UI를 연다.
	UFUNCTION(Client, Reliable)
	void Client_OpenSharedLocker();

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

	UFUNCTION(BlueprintImplementableEvent, Category="UI")
	void OnOpenSelectZone();
	
	UFUNCTION(Server, Reliable, BlueprintCallable)                                                                  
	void Server_SelectZone(FName ZoneId);
	
	// UI 실제 생성/뷰포트 추가는 BP에서 (WBP_ZoneSelect 참조는 C++이 몰라도 되므로)                  
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

	// 공유 보관함 — UI는 ISharedLockerBackend로만 부른다. 서버 검증은 UItemShopSubsystem.
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

	// 잠수정 공유 보관함 Modal(USharedLockerWidget 부모 WBP). 비어 있으면 보관함 상호작용 시 아무 것도 안 열린다.
	UPROPERTY(EditDefaultsOnly, Category = "UI|Shared Locker")
	TSubclassOf<UUserWidget> SharedLockerWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Shared Locker")
	int32 SharedLockerZOrder = 50;

private:
	// 로컬 컨트롤러에서 현재 월드의 GameState 로 탐사 레벨 여부를 판정해 Ready/Teardown 을 엣지에서 1회씩 쏜다.
	void RefreshExplorationHUD();

	// 로비/탐사 GameState 가 잡힐 때마다 진행도 HUD 를 그 GameState 기준으로 (재)생성한다.
	// seamless travel 은 컨트롤러를 살려 둔 채 월드만 바꾸므로 BeginPlay 1회 생성으로는 탐사맵에서 사라진다.
	void RefreshProgressHUD();

	// 로컬 사용자 FOV를 폰 획득/재획득·설정 적용 때 카메라에 적용한다.
	void ApplyLocalPlayerSettings();

	FDelegateHandle SettingsAppliedHandle;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ProgressHUD;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HudNoticeFeed;

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

	// --- 공유 보관함 UI ---
	void OpenSharedLocker();
	void CloseSharedLocker(bool bRestoreInput);

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> SharedLockerWidget;

	FSharedLockerResultEvent SharedLockerResultEvent;

	TOptional<EPauseLeaveTarget> PendingLeaveTarget;
	FPauseLeaveFailedEvent LeaveFailedEvent;
	FTimerHandle LeaveTimeoutHandle;
	bool bWaitingForSessionDestroy = false;
};
