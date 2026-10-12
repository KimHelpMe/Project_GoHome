#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "Core/ExpeditionState.h"
#include "Core/FailReason.h"
#include "Shop/SharedLockerTypes.h"
#include "GoHomeGameState.generated.h"

class UDockingDoorComponent;
class APlayerState;
struct FExpeditionProgress;

// 상시 진행도 HUD 값(라운드·다음 관문·보유 자금, 탐사맵이면 할당량)이 바뀔 때 브로드캐스트 — 값은 콜백 안에서 Get*()로 꺼낸다
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExpeditionProgressChanged);

// 잠수정 공유 보관함 수량 미러가 바뀔 때 — 값은 GetSharedLockerItems()로 꺼낸다
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSharedLockerChanged);

/**
 * 탐사 진행 단계만 책임진다 (도킹 문 상태는 UDockingDoorComponent가 별도 소유).
 */
UCLASS()
class GOHOME_API AGoHomeGameState : public AGameState
{
	GENERATED_BODY()

public:
	AGoHomeGameState();

	UFUNCTION(BlueprintCallable, Category = "Expedition")
	void AddDeliveredValue(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Expedition")
	void Fail(EFailReason Reason);

	void OnPlayerRemovedFromParty(APlayerState* PlayerState);

protected:
	virtual void BeginPlay() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_State();

	UFUNCTION()
	void OnRep_ExpeditionProgress();

	UFUNCTION()
	void OnRep_SharedLockerItems();

	// 진행도 값이 바뀐 뒤 공통 지점 — 서브클래스가 자기 델리게이트(예: 탐사 할당량)도 함께 쏘도록 오버라이드
	virtual void NotifyExpeditionProgressChanged();

public:
	UFUNCTION(BlueprintCallable, Category="Expedition")
	void SetState(EExpeditionState NewState);

	UFUNCTION(BlueprintPure, Category = "Expedition")
	EExpeditionState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category="Docking Door")
	UDockingDoorComponent* GetDockingDoorComponent() const { return DockingDoorComponent; }

	// 서버 전용. 세이브 진행도(라운드·자금·다음 관문)를 복제 필드에 싣는다(BeginPlay에서 1회, SaveSubsystem::BuildProgress 기반).
	void SetExpeditionProgress(const FExpeditionProgress& Progress);

	// 서버 전용. 납품/강화 후 세이브의 새 보유 자금을 복제 필드에 싣는다(로비·탐사 공통).
	void SetCurrentFunds(int32 InCurrentFunds);

	// 서버 전용. UItemShopSubsystem이 보관함 수량이 바뀔 때마다(구매·꺼내기·넣기·소모·분실·정산) 다시 싣는다.
	void SetSharedLockerItems(const TArray<FSharedLockerViewEntry>& InItems);

	UFUNCTION(BlueprintPure, Category = "Shared Locker")
	const TArray<FSharedLockerViewEntry>& GetSharedLockerItems() const { return SharedLockerItems; }

	// 서버 전용. 이번 라운드 팀 구매 누적(상점 "남은 구매" 표시용). 보관함 미러와 같은 시점(RefreshLockerView)에 싣는다.
	void SetRoundShopPurchases(const TArray<FItemShopRoundPurchase>& InPurchases);

	// 이번 라운드에 팀이 산 수량(없으면 0). 서버 검증은 세이브 기준 — 이건 표시용 미러다.
	int32 GetRoundShopPurchaseQuantity(FName ProductId) const;

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetCurrentRound() const { return CurrentRound; }

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetFinalRound() const { return FinalRound; }

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetCurrentFunds() const { return CurrentFunds; }

	// 다음 체크포인트(관문) 라운드. 남은 관문이 없으면 0.
	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetNextCheckPointRound() const { return NextCheckPointRound; }

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetNextCheckPointQuota() const { return NextCheckPointQuota; }

	// 이 맵에 할당량이 있으면 true + 값(탐사맵만). 상시 HUD가 할당량 줄 표시 여부를 여기서 판단한다.
	virtual bool GetMapQuotaProgress(int32& OutDeliveredValue, int32& OutMapQuota) const { return false; }

	// 이 맵에 제한시간이 있으면 true + 남은/전체 초(탐사맵만). 상시 HUD가 매 틱 읽어 시간 줄 표시 여부·값을 정한다.
	virtual bool GetTimeLimitProgress(float& OutRemainingSeconds, float& OutTotalSeconds) const { return false; }

	UPROPERTY(BlueprintAssignable, Category = "Expedition")
	FOnExpeditionStateChanged OnStateChanged;

	// 로비/탐사 공통 상시 진행도 HUD가 바인딩 — 바인딩 직후 Get*()로 한 번 당겨오도록
	UPROPERTY(BlueprintAssignable, Category = "Expedition")
	FOnExpeditionProgressChanged OnExpeditionProgressChanged;

	// 보관함 UI가 바인딩 — 바인딩 직후 GetSharedLockerItems()로 한 번 당겨오도록
	UPROPERTY(BlueprintAssignable, Category = "Shared Locker")
	FOnSharedLockerChanged OnSharedLockerChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Expedition")
	EExpeditionState CurrentState = EExpeditionState::Lobby;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Docking Door")
	TObjectPtr<UDockingDoorComponent> DockingDoorComponent;

	// 완료된 라운드 수(세이브 CurrentRound 미러). 표시용 "현재 라운드"는 위젯 쪽에서 +1.
	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 CurrentRound = 0;

	// 전체 라운드 수(마지막 체크포인트 Round, EconomyConfig 기준).
	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 FinalRound = 0;

	// 보유 자금(세이브 CurrentFunds 미러, 세이브가 호스트 전용 SoT). BeginPlay 1회 + 납품/강화마다 갱신.
	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 CurrentFunds = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 NextCheckPointRound = 0;

	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 NextCheckPointQuota = 0;

	// 잠수정 공유 보관함 표시 미러(세이브 + 런타임 꺼냄 장부가 서버 SoT). 팀원 전원이 같은 수량을 본다.
	UPROPERTY(ReplicatedUsing = OnRep_SharedLockerItems, BlueprintReadOnly, Category = "Shared Locker", meta = (AllowPrivateAccess = "true"))
	TArray<FSharedLockerViewEntry> SharedLockerItems;

	// 상점 라운드 구매 누적 미러. 바뀌면 보관함과 같은 OnSharedLockerChanged로 알린다.
	UPROPERTY(ReplicatedUsing = OnRep_SharedLockerItems, BlueprintReadOnly, Category = "Shared Locker", meta = (AllowPrivateAccess = "true"))
	TArray<FItemShopRoundPurchase> RoundShopPurchases;

private:
	UPROPERTY()
	TSet<TObjectPtr<APlayerState>> RemovedFromParty;
};
