#pragma once

#include "CoreMinimal.h"
#include "Data/FSettlementResult.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Upgrade/EquipmentUpgradeTypes.h"
#include "Shop/SharedLockerTypes.h"
#include "GoHomeSaveSubsystem.generated.h"

class UEconomyConfigDataAsset;
class UWorld;
class UGoHomeSaveGame;
enum class EExpeditionState : uint8;
struct FCheckPoint;

/**
 * 트래블 간 유지되는 GameInstanceSubsystem. GameState는 맵 이동마다 새로 스폰되므로
 * 이 서브시스템이 매 레벨 GameState::BeginPlay에서 OnStateChanged에 재구독한다.
 * NewState == Lobby일 때 저장하며, 구독 직후 현재 상태가 이미 Lobby면 즉시 저장한다
 * (로비 맵 재진입 시 엣지 트리거 누락 방지).
 */

UCLASS()
class GOHOME_API UGoHomeSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& CollectionBase) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Save")
	void SaveToDisk();

	// 납품 계산. 반영 후 이번 라운드 납품 누적액을 반환(GameState 복제 미러용).
	int32 AccumulateDeliveredValue(int32 Value);

	// 라운드 종료 정산
	FSettlementResult FinalizeRound(bool bForfeited, const TArray<FString>& CasualtyNames);

	// 세이브의 상태를 리플리케이트 가능한 평면 struct로 복사
	FExpeditionProgress BuildProgress() const;

	// 이어할 진행이 있는가 — 완료 라운드가 1 이상일 때만(기본 SaveGame의 초기 자금만으로는 진행으로 보지 않음)
	bool HasResumableProgress() const;

	// 호스트 전용: 진행을 초기화하고 즉시 디스크에 반영한다. 디스크 저장 실패 시 false.
	bool StartNewExpedition();

	UFUNCTION(BlueprintPure, Category = "Save")
	const UGoHomeSaveGame* GetSaveGame() const { return SaveGame; };

	// 현재 보유 자금(GameState 복제 미러용). 세이브 없으면 0.
	int32 GetCurrentFunds() const;

	// 강화 비용을 보유자금에서 차감
	bool TrySpendFunds(int32 Amount);

	// 저장된 강화 레벨을 가져온다.
	TArray<FEquipmentUpgradeLevelState> GetSavedUpgradeLevels() const;

	// 현재 업그레이드 레벨을 SaveGame에 저장한다.
	void SetSavedUpgradeLevels(const TArray<FEquipmentUpgradeLevelState>& InUpgradeLevels);

	// 출발 시 호출해서 값을 초기화
	void SetTargetMapQuota(int32 Quota) { CurrentMapQuota = Quota; };

	// 강화/구매 취소 시 차감했던 자금을 되돌린다(원자적 구매 롤백용).
	void RefundFunds(int32 Amount);

	// --- 잠수정 공유 보관함 저장소 (로직은 UItemShopSubsystem) ---
	const TArray<FSharedLockerEntry>& GetSharedLockerEntries() const;

	int32 GetLockerOwnedQuantity(FName ProductId) const;

	// 팀 보유 수량을 덮어쓴다. 0 이하면 항목을 지운다. 세이브가 없으면 false.
	bool SetLockerOwnedQuantity(FName ProductId, EItemShopItemLifetime Lifetime, int32 OwnedQuantity);

	// 이번 라운드 팀 구매 수량
	int32 GetRoundPurchaseQuantity(FName ProductId) const;
	const TArray<FItemShopRoundPurchase>& GetRoundShopPurchases() const;

	void SetRoundPurchaseQuantity(FName ProductId, int32 Quantity);

private:
	void ResetSave();

	// 새로 만든 SaveGame 인스턴스에 현재 스키마 버전을 넣는다.
	void CreateFreshSaveGame();

	// 옛 스키마 세이브를 현재 버전으로 올린다(디스크 반영은 다음 저장 시점).
	void MigrateSaveGame();
	ESettlementOutcome DetermineOutcome(const FCheckPoint* CheckPoint, int32 CompletedRound) const;

protected:
	UFUNCTION()
	void OnExpeditionStateChanged(EExpeditionState NewState);

	void OnPostLoadMap(UWorld* LoadedWorld);

	UPROPERTY(BlueprintReadOnly, Category = "Save")
	TObjectPtr<UGoHomeSaveGame> SaveGame;

private:
	FDelegateHandle PostLoadMapHandle;

	UPROPERTY()
	TObjectPtr<UEconomyConfigDataAsset> EconomyConfig;

	int32 CurrentMapQuota = 0;
};
