#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/CarryWeightProvider.h"
#include "OxygenComponent.generated.h"

class UOxygenDrainBoostConfig;
class UCurveFloat;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOxygenChanged, float, CurrentOxygen, float, MaxOxygen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSafeZoneChanged, bool, bNewInSafeZone);
// 서버 전용: 몬스터가 물기·잡기로 산소 흡입 가속을 시작했다(모든 몬스터 BP 공격 지점이 StartOxygenDrainBoostFromInstigator 를 부른다)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDrainBoostStarted, AActor* /*InstigatorActor*/);

/**
 * 소모 속도 계산 시 ICarryWeightProvider의 초과 무게를 참조한다.
 * 산소 0 도달 시 Owner의 IDamageable::ApplyDamage를 동기 호출한다 (별도 이벤트 계층 없음).
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class GOHOME_API UOxygenComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOxygenComponent();

	// UI가 매 Tick 직접 확인하지 않아도 산소 변경을 받을 수 있게 한다.
	UPROPERTY(BlueprintAssignable, Category = "Oxygen")
	FOnOxygenChanged OnOxygenChanged;

	UPROPERTY(BlueprintAssignable, Category = "Oxygen")
	FOnSafeZoneChanged OnSafeZoneChanged;

	FOnDrainBoostStarted OnDrainBoostStarted;

	// 외부 코드는 산소 값을 직접 쓰지 말고, 읽기 전용 함수로만 접근한다.
	UFUNCTION(BlueprintPure, Category = "Oxygen")
	float GetOxygen() const;

	UFUNCTION(BlueprintPure, Category = "Oxygen")
	float GetMaxOxygen() const;

	UFUNCTION(BlueprintPure, Category = "Oxygen")
	float GetBaseMaxOxygen() const;

	UFUNCTION(BlueprintPure, Category = "Oxygen")
	float GetMaxOxygenBonus() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Oxygen")
	void SetMaxOxygenBonus(float NewBonus);

	// 서버 권한 회복. 최대 산소를 넘지 않으며 실제 회복량을 반환한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Oxygen")
	float RestoreOxygen(float Amount);

	// UI에서 15칸 산소 표시할 때 사용할 값.
	UFUNCTION(BlueprintPure, Category = "Oxygen")
	int32 GetDisplayedOxygenPips() const;

	// UI 게이지나 디버그 확인용 퍼센트.
	UFUNCTION(BlueprintPure, Category = "Oxygen")
	float GetOxygenPercent() const;

	UFUNCTION(BlueprintPure, Category = "Oxygen")
	bool IsInSafeZone() const;

	// 안전지대 판정은 게임 상태라서 서버만 바꾼다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Oxygen")
	void SetInSafeZone(bool bNewInSafeZone);

	// 스프린트 전용 산소 소모 배율. 1.0 = 평소와 동일. 기존 초과무게/몬스터 배율과는 별개로 곱해진다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Oxygen")
	void SetSprintDrainMultiplier(float NewMultiplier);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// UI 기준 산소 칸 수. 기본 15칸.
	// 강화가 없는 기본 산소 최대치. 최종 최대 산소는 GetMaxOxygen()로 가져온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Oxygen, Category = "Oxygen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxOxygen = 15.f;

	// 강화용 보너스 산소
	UPROPERTY(ReplicatedUsing = OnRep_MaxOxygenBonus, BlueprintReadOnly, Category = "Oxygen")
	float MaxOxygenBonus = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_Oxygen, BlueprintReadOnly, Category = "Oxygen")
	float Oxygen = 15.f;

	// 산소가 가득 찬 상태에서 기본적으로 버티는 시간. 600초 = 10분.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Oxygen", meta = (ClampMin = "0.01", UIMin = "1.0"))
	float TargetOxygenDuration = 300.f;

	// 초과 무게를 산소 소모 배율로 바꾸는 곡선.
	// X축은 초과 무게, Y축은 기본 산소 소모량에 곱할 배율이다 (0 -> 1 권장).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Oxygen|Weight")
	TObjectPtr<UCurveFloat> OverweightDrainMultiplierCurve;

	// 산소가 0일 때 1초마다 들어가는 질식 데미지.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Oxygen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SuffocationDamagePerSecond = 5.f;

	// 안전지대 안에서 1초마다 회복되는 산소량.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Oxygen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SafeZoneRecoveryRate = 2.f;

	// 현재 안전지대 안에 있는지 여부.
	UPROPERTY(ReplicatedUsing = OnRep_InSafeZone, BlueprintReadOnly, Category = "Oxygen")
	bool bInSafeZone = false;

	UFUNCTION()
	void OnRep_Oxygen();

	UFUNCTION()
	void OnRep_InSafeZone();

	UFUNCTION()
	void OnRep_MaxOxygenBonus();

private:
	// 초과 무게 상태를 제공하는 인터페이스만 저장한다.
	// 산소 컴포넌트는 CarryWeightComponent 구체 클래스나 InventoryComponent를 몰라도 된다.
	UPROPERTY()
	TScriptInterface<ICarryWeightProvider> CachedCarryWeightProvider;

	bool HasOwnerAuthority() const;
	void FindCarryWeightProviderComponent();
	void UpdateOxygen(float DeltaTime);
	void RecoverOxygen(float DeltaTime);
	void DrainOxygen(float DeltaTime);
	void ApplySuffocationDamage(float DeltaTime);
	void SetOxygen(float NewOxygen);
	float CalculateOxygenDrainRate() const;
	float CalculateOverweightDrainMultiplier() const;
	float GetCachedOverweightAmount() const;
	// 죽으면 산소 Tick 끄기
	void HandleOwnerDeath();

	float SprintDrainMultiplier = 1.f;


// 몬스터 공격시 추가 산소 감소 관련
private:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Oxygen|Monster Drain", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UOxygenDrainBoostConfig> OxygenDrainBoostConfig;

	TMap<TWeakObjectPtr<AActor>, float> ActiveBoosts;

private:
	float CalculateMonsterDrainMultiplier() const;
	void RemoveInvalidDrainBoosts();

public:
	void StartOxygenDrainBoostFromInstigator(AActor* InstigatorActor);
	void StopOxygenDrainBoostFromInstigator(AActor* InstigatorActor);
};
