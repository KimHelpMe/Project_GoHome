

#include "Player/HealthComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// HP의 진짜 값은 서버가 정한다. BP에서 MaxHP를 바꿔도 시작 HP가 그 값에 맞춰지게 한다.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	HP = FMath::Max(0.f, MaxHP);
	BroadcastHPChanged();
}

void UHealthComponent::ApplyDamage_Implementation(float Amount, AActor* Instigator, FName DamageType)
{
	// 서버 아니면 무시
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 이미 죽었으면 /데미지 0 이하면 무시
	if (bIsDead || Amount <= 0.f)
	{
		return;
	}

	// HP를 데미지만큼 깎고 0 아래로 내려가지 않게 제한
	const float OldHP = HP;
	HP = FMath::Clamp(HP - Amount, 0.f, MaxHP);

	if (!FMath::IsNearlyEqual(HP, OldHP))
	{
		BroadcastHPChanged();
		OnDamageTaken.Broadcast(OldHP - HP, Instigator, DamageType, HP <= 0.f);
	}

	// 0 이하면 죽음
	if (HP <= 0.f)
	{
		bIsDead = true;
		DeathCause = DamageType;
		if (DeathCause.IsNone())
		{
			const APawn* InstigatorPawn = Cast<APawn>(Instigator);
			if (InstigatorPawn && !InstigatorPawn->IsPlayerControlled())
			{
				DeathCause = TEXT("Monster");
			}
		}
		OnDeath.Broadcast();
		OnDeathDynamic.Broadcast();
	}
}

float UHealthComponent::RestoreHealth(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bIsDead || Amount <= 0.f)
	{
		return 0.f;
	}

	const float OldHP = HP;
	const float SafeMaxHP = FMath::Max(0.f, MaxHP);
	HP = FMath::Clamp(HP + Amount, 0.f, SafeMaxHP);

	if (FMath::IsNearlyEqual(HP, OldHP))
	{
		return 0.f;
	}

	BroadcastHPChanged();
	return HP - OldHP;
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHealthComponent, MaxHP);
	DOREPLIFETIME(UHealthComponent, HP);
	DOREPLIFETIME(UHealthComponent, bIsDead);
	DOREPLIFETIME(UHealthComponent, DeathCause);
}

void UHealthComponent::OnRep_HP()
{
	BroadcastHPChanged();
}

void UHealthComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		OnDeathDynamic.Broadcast();
	}
}

void UHealthComponent::BroadcastHPChanged()
{
	OnHPChanged.Broadcast(HP, MaxHP);
}
