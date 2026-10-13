

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/Damageable.h"
#include "Player/DeathNotifier.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeathDynamic);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHPChanged, float, CurrentHP, float, MaxHP);
// 서버 전용: 실제로 HP 가 깎인 피해 1건(감소량·가해자·피해 종류·치명 여부). 대사 판단은 구독자(아나운서) 몫
DECLARE_MULTICAST_DELEGATE_FourParams(FOnDamageTaken, float /*Amount*/, AActor* /*Instigator*/, FName /*DamageType*/, bool /*bLethal*/);

/**
 * IDamageable을 구현해 "누가 데미지를 주는지" 몰라도 되게 한다.
 * HP 0 도달 시 서버 전용 OnDeath를 1회만 브로드캐스트한다 (bIsDead로 중복 방지).
 */
UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
class GOHOME_API UHealthComponent : public UActorComponent, public IDamageable, public IDeathNotifier
{
	GENERATED_BODY()

public:
	UHealthComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Health")
	float MaxHP = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_HP, BlueprintReadOnly, Category = "Health")
	float HP = 100.f;

	FSimpleMulticastDelegate OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeathDynamic OnDeathDynamic;

	// HP UI는 이 이벤트를 구독해서 텍스트를 갱신한다.
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHPChanged OnHPChanged;

	FOnDamageTaken OnDamageTaken;

	virtual void ApplyDamage_Implementation(float Amount, AActor* Instigator, FName DamageType) override;

	// 서버 권한 회복. 사망한 플레이어는 회복하지 않으며 실제 회복량을 반환한다.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Health")
	float RestoreHealth(float Amount);

	virtual FSimpleMulticastDelegate& GetOnDeathDelegate() override { return OnDeath; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHP() const { return HP; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHP() const { return MaxHP; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bIsDead; }

	// 사망을 일으킨 마지막 데미지 종류(사망 화면 사인 표시용). 사망 전엔 NAME_None.
	// DamageType 없이 AI 폰이 준 데미지는 "Monster"로 기록한다(몬스터 BP는 DamageType을 비워 둔다).
	UFUNCTION(BlueprintPure, Category = "Health")
	FName GetDeathCause() const { return DeathCause; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_HP();

	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	// bIsDead와 같은 틱에 세팅 — 같은 번치로 복제되지만 순서 보장은 없어 UI는 늦게 와도 채울 수 있게 읽는다
	UPROPERTY(Replicated)
	FName DeathCause = NAME_None;

	UFUNCTION()
	void OnRep_IsDead();

private:
	void BroadcastHPChanged();
};
