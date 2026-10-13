#include "Player/OxygenComponent.h"
#include "Curves/CurveFloat.h"
#include "Net/UnrealNetwork.h"
#include "Player/CarryWeightProvider.h"
#include "Player/Damageable.h"
#include "Player/DeathNotifier.h"
#include "Player/OxygenDrainBoostConfig.h"

UOxygenComponent::UOxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

float UOxygenComponent::GetOxygen() const
{
	return Oxygen;
}

float UOxygenComponent::GetMaxOxygen() const
{
	//return MaxOxygen;
	// 강화 포함 최종 산소 계산
	return FMath::Max(0.f, MaxOxygen + MaxOxygenBonus);
	/*
	MaxOxygen 15
	MaxOxygenBonus 3
	GetMaxOxygen() 18
	*/
}

float UOxygenComponent::GetBaseMaxOxygen() const
{
	return FMath::Max(0.f, MaxOxygen);
}

float UOxygenComponent::GetMaxOxygenBonus() const
{
	return MaxOxygenBonus;
}

float UOxygenComponent::RestoreOxygen(float Amount)
{
	if (!HasOwnerAuthority() || Amount <= 0.f)
	{
		return 0.f;
	}

	const float OldOxygen = Oxygen;
	SetOxygen(Oxygen + Amount);
	return Oxygen - OldOxygen;
}

int32 UOxygenComponent::GetDisplayedOxygenPips() const
{
	/*
	Oxygen 14.8 -> 15칸
	Oxygen 14.0 -> 14칸
	Oxygen 0.0 -> 0칸
	*/
	const int32 MaxPips = FMath::Max(0, FMath::CeilToInt(GetMaxOxygen()));
	return FMath::Clamp(FMath::CeilToInt(Oxygen), 0, MaxPips);
}

float UOxygenComponent::GetOxygenPercent() const
{
	const float CurrentMaxOxygen = GetMaxOxygen();
	if (CurrentMaxOxygen <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(Oxygen / CurrentMaxOxygen, 0.f, 1.f);
}

bool UOxygenComponent::IsInSafeZone() const
{
	return bInSafeZone;
}

void UOxygenComponent::SetInSafeZone(bool bNewInSafeZone)
{
	if (!HasOwnerAuthority() || bInSafeZone == bNewInSafeZone)
	{
		return;
	}

	bInSafeZone = bNewInSafeZone;
	OnSafeZoneChanged.Broadcast(bInSafeZone);
}

void UOxygenComponent::SetSprintDrainMultiplier(float NewMultiplier)
{
	if (!HasOwnerAuthority())
	{
		return;
	}

	SprintDrainMultiplier = FMath::Max(0.f, NewMultiplier);
}

void UOxygenComponent::SetMaxOxygenBonus(float NewBonus)
{
	if (!HasOwnerAuthority())
	{
		return;
	}

	const float ClampedBonus = FMath::Max(0.f, NewBonus);
	if (FMath::IsNearlyEqual(MaxOxygenBonus, ClampedBonus))
	{
		return;
	}

	MaxOxygenBonus = ClampedBonus;

	const float NewMaxOxygen = GetMaxOxygen();
	if (Oxygen > NewMaxOxygen)
	{
		SetOxygen(NewMaxOxygen);
		return;
	}

	OnOxygenChanged.Broadcast(Oxygen, NewMaxOxygen);
}

void UOxygenComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!HasOwnerAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	FindCarryWeightProviderComponent();
	SetOxygen(GetMaxOxygen());

	if (UActorComponent* DeathNotifierComponent = GetOwner()->FindComponentByInterface(UDeathNotifier::StaticClass()))
	{
		if (IDeathNotifier* DeathNotifier = Cast<IDeathNotifier>(DeathNotifierComponent))
		{
			DeathNotifier->GetOnDeathDelegate().AddUObject(this, &ThisClass::HandleOwnerDeath);
		}
	}
}

void UOxygenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasOwnerAuthority() || DeltaTime <= 0.f)
	{
		return;
	}

	RemoveInvalidDrainBoosts();
	UpdateOxygen(DeltaTime);
}

void UOxygenComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOxygenComponent, MaxOxygen);
	DOREPLIFETIME(UOxygenComponent, MaxOxygenBonus);
	DOREPLIFETIME(UOxygenComponent, Oxygen);
	DOREPLIFETIME(UOxygenComponent, bInSafeZone);
}

bool UOxygenComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UOxygenComponent::FindCarryWeightProviderComponent()
{
	CachedCarryWeightProvider.SetObject(nullptr);
	CachedCarryWeightProvider.SetInterface(nullptr);

	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	const TSet<UActorComponent*>& OwnerComponents = OwnerActor->GetComponents();
	for (UActorComponent* OwnerComponent : OwnerComponents)
	{
		// 자기 자신이거나 비어 있는 컴포넌트는 무게 상태 제공 대상이 아니다.
		if (!OwnerComponent || OwnerComponent == this)
		{
			continue;
		}

		ICarryWeightProvider* CarryWeightProvider = Cast<ICarryWeightProvider>(OwnerComponent);
		if (!CarryWeightProvider)
		{
			continue;
		}

		// 산소는 운반 무게 계산 방식은 모르고, 초과 무게 계약만 읽는다.
		CachedCarryWeightProvider.SetObject(OwnerComponent);
		CachedCarryWeightProvider.SetInterface(CarryWeightProvider);
		return;
	}
}

void UOxygenComponent::UpdateOxygen(float DeltaTime)
{
	if (bInSafeZone)
	{
		RecoverOxygen(DeltaTime);
		return;
	}

	DrainOxygen(DeltaTime);

	if (Oxygen <= 0.f)
	{
		ApplySuffocationDamage(DeltaTime);
	}
}

void UOxygenComponent::RecoverOxygen(float DeltaTime)
{
	const float RecoveryAmount = FMath::Max(0.f, SafeZoneRecoveryRate) * DeltaTime;
	SetOxygen(Oxygen + RecoveryAmount);
}

void UOxygenComponent::DrainOxygen(float DeltaTime)
{
	if (Oxygen <= 0.f)
	{
		return;
	}

	const float DrainAmount = CalculateOxygenDrainRate() * DeltaTime;
	SetOxygen(Oxygen - DrainAmount);
}

void UOxygenComponent::ApplySuffocationDamage(float DeltaTime)
{
	const float DamageAmount = FMath::Max(0.f, SuffocationDamagePerSecond) * DeltaTime;
	if (DamageAmount <= 0.f)
	{
		return;
	}

	// HP 컴포넌트 이름을 직접 모르고, "데미지를 받을 수 있는 대상"만 찾는다.
	if (UActorComponent* DamageableComponent = GetOwner()->FindComponentByInterface(UDamageable::StaticClass()))
	{
		IDamageable::Execute_ApplyDamage(DamageableComponent, DamageAmount, GetOwner(), FName(TEXT("Suffocation")));
	}
}

void UOxygenComponent::HandleOwnerDeath()
{
	SetComponentTickEnabled(false);
}

void UOxygenComponent::SetOxygen(float NewOxygen)
{
	const float SafeMaxOxygen = GetMaxOxygen();
	const float ClampedOxygen = FMath::Clamp(NewOxygen, 0.f, SafeMaxOxygen);

	if (FMath::IsNearlyEqual(Oxygen, ClampedOxygen))
	{
		return;
	}

	Oxygen = ClampedOxygen;
	OnOxygenChanged.Broadcast(Oxygen, SafeMaxOxygen);
}

float UOxygenComponent::CalculateOxygenDrainRate() const
{
	if (MaxOxygen <= 0.f || TargetOxygenDuration <= 0.f)
	{
		return 0.f;
	}

	const float BaseDrainRate = MaxOxygen / TargetOxygenDuration;
	const float OverweightDrainMultiplier = CalculateOverweightDrainMultiplier();

	// 몬스터 배율 추가 계산식 변경
	const float MonsterDrainMultiplier = CalculateMonsterDrainMultiplier();

	return FMath::Max(0.f, BaseDrainRate * OverweightDrainMultiplier * MonsterDrainMultiplier * SprintDrainMultiplier);
}

float UOxygenComponent::CalculateOverweightDrainMultiplier() const
{
	const float OverweightAmount = GetCachedOverweightAmount();
	if (OverweightAmount <= 0.f || !OverweightDrainMultiplierCurve)
	{
		return 1.f;
	}

	const float CurveMultiplier = OverweightDrainMultiplierCurve->GetFloatValue(OverweightAmount);
	return FMath::Max(1.f, CurveMultiplier);
}

float UOxygenComponent::GetCachedOverweightAmount() const
{
	const UObject* CarryWeightProviderObject = CachedCarryWeightProvider.GetObject();
	const ICarryWeightProvider* CarryWeightProvider = CachedCarryWeightProvider.GetInterface();

	if (!IsValid(CarryWeightProviderObject) || !CarryWeightProvider)
	{
		const_cast<UOxygenComponent*>(this)->FindCarryWeightProviderComponent();
		CarryWeightProviderObject = CachedCarryWeightProvider.GetObject();
		CarryWeightProvider = CachedCarryWeightProvider.GetInterface();
	}

	if (!IsValid(CarryWeightProviderObject) || !CarryWeightProvider)
	{
		return 0.f;
	}

	return FMath::Max(0.f, CarryWeightProvider->GetOverweightAmount());
}

void UOxygenComponent::OnRep_Oxygen()
{
	OnOxygenChanged.Broadcast(Oxygen, GetMaxOxygen());
}

void UOxygenComponent::OnRep_InSafeZone()
{
	OnSafeZoneChanged.Broadcast(bInSafeZone);
}

void UOxygenComponent::OnRep_MaxOxygenBonus()
{
	OnOxygenChanged.Broadcast(Oxygen, GetMaxOxygen());
}


// 몬스터 산소 추가 감소 관련 함수들
void UOxygenComponent::StartOxygenDrainBoostFromInstigator(AActor* InstigatorActor)
{
	if (!HasOwnerAuthority() || !IsValid(InstigatorActor))
	{
		return;
	}

	RemoveInvalidDrainBoosts();

	// 배율 설정과 무관하게 "공격 시작" 사실은 알린다(아나운서). 반복 호출 간격은 구독자가 거른다
	OnDrainBoostStarted.Broadcast(InstigatorActor);

	const float DrainMultiplier = OxygenDrainBoostConfig
		? OxygenDrainBoostConfig->GetDrainMultiplierForInstigator(InstigatorActor)
		: 1.f;

	if (DrainMultiplier <= 1.f)
	{
		ActiveBoosts.Remove(TWeakObjectPtr<AActor>(InstigatorActor));
		return;
	}

	ActiveBoosts.FindOrAdd(TWeakObjectPtr<AActor>(InstigatorActor)) = DrainMultiplier;
}

void UOxygenComponent::StopOxygenDrainBoostFromInstigator(AActor* InstigatorActor)
{
	if (!HasOwnerAuthority() || !InstigatorActor)
	{
		return;
	}

	ActiveBoosts.Remove(TWeakObjectPtr<AActor>(InstigatorActor));
}

float UOxygenComponent::CalculateMonsterDrainMultiplier() const
{
	float CombinedMultiplier = 1.f;

	for (const TPair<TWeakObjectPtr<AActor>, float>& BoostPair : ActiveBoosts)
	{
		if (!BoostPair.Key.IsValid())
		{
			continue;
		}

		CombinedMultiplier += FMath::Max(1.f, BoostPair.Value) - 1.f;
	}

	const float MaxMultiplier = OxygenDrainBoostConfig
		? FMath::Max(1.f, OxygenDrainBoostConfig->MaxMonsterDrainMultiplier)
		: CombinedMultiplier;

	return FMath::Clamp(CombinedMultiplier, 1.f, MaxMultiplier);
}

void UOxygenComponent::RemoveInvalidDrainBoosts()
{
	for (auto It = ActiveBoosts.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
