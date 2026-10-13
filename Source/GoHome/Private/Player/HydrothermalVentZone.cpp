


#include "Player/HydrothermalVentZone.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/Damageable.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Net/UnrealNetwork.h"
#include "Core/AnnouncerComponent.h"

AHydrothermalVentZone::AHydrothermalVentZone()
{
	PrimaryActorTick.bCanEverTick = true;

	// 분출 상태(VentState)를 클라이언트로 복제하기 위해 필요
	bReplicates = true;

	EffectArea = CreateDefaultSubobject<UBoxComponent>(TEXT("EffectArea"));
	SetRootComponent(EffectArea);
	EffectArea->SetBoxExtent(FVector(400.f, 400.f, 400.f));
	EffectArea->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	EffectArea->SetGenerateOverlapEvents(true);

	VentSmokeVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("VentSmokeVFX"));
	VentSmokeVFX->SetupAttachment(EffectArea);
	VentSmokeVFX->SetAbsolute(false, false, true);
	VentSmokeVFX->bAutoActivate = true;
}

void AHydrothermalVentZone::BeginPlay()
{
	Super::BeginPlay();
	
	EffectArea->OnComponentBeginOverlap.AddDynamic(this, &AHydrothermalVentZone::OnEffectAreaBeginOverlap);
	EffectArea->OnComponentEndOverlap.AddDynamic(this, &AHydrothermalVentZone::OnEffectAreaEndOverlap);

	// 에디터 배치 편의를 위해 생성자에서 자동 재생으로 켜둔 VFX를 인게임에서는 일단 끈다.
	// 이후 분출 상태에 들어갈 때만 켜진다
	if (VentSmokeVFX)
	{
		VentSmokeVFX->Deactivate();
	}

	// 첫 대기 타이머 시작. 상태는 서버가 굴린다
	if (HasAuthority())
	{
		SetVentState(EVentState::Idle);
	}
}

void AHydrothermalVentZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 분출 사이클(대기 -> 예고 -> 분출)은 서버에서만 진행시킨다.
	// 클라이언트가 각자 시간을 재면 사람마다 분출 타이밍이 어긋난다
	if (HasAuthority())
	{
		UpdateVentCycle(DeltaTime);
	}

	// 분출 중이 아니면 상승력도 데미지도 없다. 여기가 "분출할 때만 데미지"의 핵심
	if (VentState != EVentState::Erupting)
	{
		return;
	}

	ApplyForceToOverlappingCharacters(DeltaTime);

	// 데미지는 서버 권위 판정이 걸린 상태 변화라서 서버에서만 계산
	if (HasAuthority())
	{
		ApplyDamageToOverlappingCharacters(DeltaTime);
	}
}

void AHydrothermalVentZone::OnEffectAreaBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACharacter* OtherCharacter = Cast<ACharacter>(OtherActor);
	if (!OtherCharacter)
	{
		return;
	}

	if (!HasAuthority() && !OtherCharacter->IsLocallyControlled())
	{
		return;
	}

	OverlappingCharacters.AddUnique(OtherCharacter);

	// 이미 분출이 진행 중인 와중에 뛰어든 캐릭터도 즉시 감속이 걸려야 한다
	ApplyBrakingOverride(OtherCharacter, VentState == EVentState::Erupting);

	// 이미 예고·분출 중인 영역에 들어온 플레이어에게도 경고(서버 판정, 재입장 간격은 아나운서가 거른다)
	if (HasAuthority() && VentState != EVentState::Idle)
	{
		UAnnouncerComponent::SendToPawnOwner(OtherCharacter, EAnnouncerEvent::VentWarning, GetFName());
	}

	OnCharacterEnteredZone(OtherCharacter);
}

void AHydrothermalVentZone::OnEffectAreaEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	ACharacter* OtherCharacter = Cast<ACharacter>(OtherActor);
	if (!OtherCharacter)
	{
		return;
	}

	if (!HasAuthority() && !OtherCharacter->IsLocallyControlled())
	{
		return;
	}

	OverlappingCharacters.Remove(OtherCharacter);

	// 존을 나가면 무조건 원래 감속값으로 복원
	ApplyBrakingOverride(OtherCharacter, false);

	// 영역을 벗어나면 남은 분출 경고를 지운다
	if (HasAuthority())
	{
		UAnnouncerComponent::CancelForPawnOwner(OtherCharacter, GetFName());
	}

	OnCharacterExitedZone(OtherCharacter);
}

void AHydrothermalVentZone::ApplyForceToOverlappingCharacters(float DeltaTime)
{
	for (ACharacter* Character : OverlappingCharacters)
	{
		if (!IsValid(Character))
		{
			continue;
		}

		if (!HasAuthority() && !Character->IsLocallyControlled())
		{
			continue;
		}

		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (!Movement)
		{
			continue;
		}

		const FVector Force = FVector::UpVector * VentForceStrength;
		Movement->AddForce(Force * Movement->Mass);
	}
}

void AHydrothermalVentZone::ApplyDamageToOverlappingCharacters(float DeltaTime)
{
	const float DamageAmount = FMath::Max(0.f, DamagePerSecond) * DeltaTime;
	if (DamageAmount <= 0.f)
	{
		return;
	}

	for (ACharacter* Character : OverlappingCharacters)
	{
		if (!IsValid(Character))
		{
			continue;
		}

		// HP 컴포넌트 이름을 직접 모르고, "데미지를 받을 수 있는 대상"만 찾는다 (OxygenComponent::ApplySuffocationDamage와 동일한 방식)
		if (UActorComponent* DamageableComponent = Character->FindComponentByInterface(UDamageable::StaticClass()))
		{
			IDamageable::Execute_ApplyDamage(DamageableComponent, DamageAmount, this, DamageTypeName);
		}
	}
}

void AHydrothermalVentZone::UpdateVentCycle(float DeltaTime)
{
	// 트리거 방식일 때는 존 안에 아무도 없으면 대기 상태에서 시간을 깎지 않고 그대로 멈춰 있는다.
	// 단, 이미 예고나 분출이 시작된 뒤라면 플레이어가 도망쳐도 끝까지 마친다 (중간에 뚝 끊기면 어색하므로)
	if (VentState == EVentState::Idle && bEruptOnlyWhenCharacterInside && OverlappingCharacters.Num() == 0)
	{
		return;
	}

	StateTimeRemaining -= DeltaTime;
	if (StateTimeRemaining > 0.f)
	{
		return;
	}

	switch (VentState)
	{
	case EVentState::Idle:
		SetVentState(EVentState::Warning);
		break;
	case EVentState::Warning:
		SetVentState(EVentState::Erupting);
		break;
	case EVentState::Erupting:
		SetVentState(EVentState::Idle);
		break;
	}
}

void AHydrothermalVentZone::SetVentState(EVentState NewState)
{
	VentState = NewState;

	// 상태를 바꿀 때마다 그 상태의 지속 시간을 함께 세팅한다.
	// 상태 변경 창구를 이 함수 하나로 묶어야 타이머 세팅을 빠뜨리지 않는다
	switch (NewState)
	{
	case EVentState::Idle:
		StateTimeRemaining = IdleDuration + FMath::FRandRange(0.f, FMath::Max(0.f, IdleDurationVariance));
		break;
	case EVentState::Warning:
		StateTimeRemaining = WarningDuration;
		break;
	case EVentState::Erupting:
		StateTimeRemaining = EruptDuration;
		break;
	}

	// OnRep_VentState는 클라이언트에서만 불리므로, 서버도 연출을 반영하려면 여기서 직접 호출해야 한다
	OnVentStateChanged();

	// 아나운서: 예고 진입 때 영역 안 플레이어에게 1회, 분출이 끝나면 취소. 서버 상태 전이에서만 발행(OnRep/VFX 경로 아님)
	if (HasAuthority() && (NewState == EVentState::Warning || NewState == EVentState::Idle))
	{
		for (ACharacter* Character : OverlappingCharacters)
		{
			if (NewState == EVentState::Warning)
			{
				UAnnouncerComponent::SendToPawnOwner(Character, EAnnouncerEvent::VentWarning, GetFName());
			}
			else
			{
				UAnnouncerComponent::CancelForPawnOwner(Character, GetFName());
			}
		}
	}
}

void AHydrothermalVentZone::OnRep_VentState()
{
	OnVentStateChanged();
}

void AHydrothermalVentZone::OnVentStateChanged()
{
	const bool bErupting = (VentState == EVentState::Erupting);

	// 분출 중에만 연기 VFX 재생. Activate(true)는 처음부터 다시 재생하라는 뜻
	if (VentSmokeVFX && VentSmokeVFXAsset)
	{
		if (bErupting)
		{
			VentSmokeVFX->Activate(true);
		}
		else
		{
			VentSmokeVFX->Deactivate();
		}
	}

	// 수영 감속 덮어쓰기도 분출 중에만 적용한다. 상태가 바뀌는 순간에만 한 번 돌면 된다
	for (ACharacter* Character : OverlappingCharacters)
	{
		ApplyBrakingOverride(Character, bErupting);
	}

	// 소리나 카메라 흔들림 같은 연출은 블루프린트 자식 클래스에서 이 이벤트를 받아 붙인다
	switch (VentState)
	{
	case EVentState::Warning:
		OnVentWarningStarted();
		break;
	case EVentState::Erupting:
		OnVentEruptStarted();
		break;
	case EVentState::Idle:
		OnVentEruptEnded();
		break;
	}
}

void AHydrothermalVentZone::ApplyBrakingOverride(ACharacter* Character, bool bEnable)
{
	if (!IsValid(Character))
	{
		return;
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	if (bEnable)
	{
		// 원래 값을 저장해두지 않으면 나중에 되돌릴 수 없다
		if (!OriginalBrakingDeceleration.Contains(Character))
		{
			OriginalBrakingDeceleration.Add(Character, Movement->BrakingDecelerationSwimming);
		}
		Movement->BrakingDecelerationSwimming = ZoneBrakingDeceleration;
	}
	else if (const float* Original = OriginalBrakingDeceleration.Find(Character))
	{
		Movement->BrakingDecelerationSwimming = *Original;
		OriginalBrakingDeceleration.Remove(Character);
	}
}

void AHydrothermalVentZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHydrothermalVentZone, VentState);
}

void AHydrothermalVentZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateVentSmokeVFX();
}

void AHydrothermalVentZone::UpdateVentSmokeVFX()
{
	if (!VentSmokeVFX)
	{
		return;
	}

	VentSmokeVFX->SetVisibility(VentSmokeVFXAsset != nullptr);

	if (VentSmokeVFXAsset && VentSmokeVFX->GetAsset() != VentSmokeVFXAsset)
	{
		VentSmokeVFX->SetAsset(VentSmokeVFXAsset);
	}

	VentSmokeVFX->SetVariableVec3(FName("User.EffectScale"), EffectArea->GetScaledBoxExtent() / 400.f);
}