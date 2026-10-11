

#include "Item/ItemActorBase.h"
#include "Item/ItemDataAsset.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "AI/NoiseType.h"
#include "TimerManager.h"
#include "Interaction/InventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/AudioComponent.h"
#include "Player/GoHomeCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"

AItemActorBase::AItemActorBase()
{
	// Tick 은 가능하지만, 시작할 때는 꺼진 상태.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	SetReplicateMovement(true);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	MeshComponent->SetIsReplicated(false);
	MeshComponent->SetNotifyRigidBodyCollision(true);
	MeshComponent->SetMobility(EComponentMobility::Movable);

	NoiseAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("NoiseAudioComponent"));
	NoiseAudioComponent->SetupAttachment(MeshComponent);
	NoiseAudioComponent->bAutoActivate = false;
	NoiseAudioComponent->SetIsReplicated(false); // 재생 자체는 각 클라 로컬에서 처리(리플리케이트 안 함).

	// 물속 부유 컨셉 : 중력 off + 감쇠
	// 물리가 켜진 경우(드롭 및 사망) 바닥으로 가라앉는 대신 던진 방향으로 나아가다 서서히 멈춰 그자리에 떠있게 만듬.
	MeshComponent->SetEnableGravity(false);
	MeshComponent->BodyInstance.LinearDamping = 3.0f;
	MeshComponent->BodyInstance.AngularDamping = 3.0f;
	MeshComponent->BodyInstance.MassScale = 3.0f; // 무게 -> 플레이어와 부딪힌 경우, 멀리 팅겨나가는 것 방지.

	// 아이템 흔들림.
	DriftPhaseOffset = FMath::FRandRange(0.f, 100.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (DefaultMeshAsset.Succeeded())
	{
		MeshComponent->SetStaticMesh(DefaultMeshAsset.Object);
	}

}

void AItemActorBase::BeginPlay()
{
	Super::BeginPlay();

	SyncVisualsFromItemData();

	if (HasAuthority())
	{
		SnapToGround();
	}

	// 스포너로 배치된 아이템은 물리를 켜지 않음 -> 그 자리에 정지.
	// 실제 물리는 최초 드랍될 때 시작.
}

void AItemActorBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority() || !MeshComponent->IsSimulatingPhysics())
	{
		SetActorTickEnabled(false);
		return;
	}

	const  float Weight = GetBuoyancyWeight();
	const float VerticalAccel = (NeutralWeight - Weight) * BuoyancyAccelFactor;

	// 가라앉는(무거운) 아이템이 바닥 등에 부딪혀 거의 멈추면 완전히 정지시킴.
	// 떠오르는(가벼운) 아이템은 수면 근처에서도 계속 흔들려야 하니 이 정지 처리를 적용하지 않는다.
	// 막 가라앉기 시작해 아직 한 번도 SettleVelocityThreshold를 못 넘긴 경우는 "멈춘 것"이 아님.
	// "아직 안 움직인 것"으로 , 한 번은 그 속도를 넘긴 뒤에만 정지 판정을 허용함.
	const bool bIsSinking = bIsSinkingOrRising && VerticalAccel < 0.f;
	const bool bAboveSettleSpeed = MeshComponent->GetPhysicsLinearVelocity().SizeSquared() >= FMath::Square(SettleVelocityThreshold);

	if (bIsSinking)
	{
		if (bAboveSettleSpeed)
		{
			bHasReachedSinkSpeed = true;
		}
		else if (bHasReachedSinkSpeed)
		{
			SetActorTickEnabled(false);
			OnSettled();
			return;
		}
	}

	// 부유 중 조금씩 흔들리는 느낌(가라앉거나 떠오르는 동안에도 유지).
	const float Time = GetGameTimeSinceCreation() + DriftPhaseOffset;
	const FVector DriftForce = FVector(
		FMath::Sin(Time * 0.6f), 
		FMath::Cos(Time * 0.5f), 
		FMath::Sin(Time * 0.8f) * 0.5f) * DriftForceStrength;
	MeshComponent->AddForce(DriftForce, NAME_None, true);

	if (bIsSinkingOrRising)
	{
		MeshComponent->AddForce(FVector(0.f, 0.f, VerticalAccel), NAME_None, true);
	}
}

void AItemActorBase::SnapToGround()
{
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	const FVector Start = GetActorLocation();
	const FVector End = Start - FVector::UpVector * 500.f;

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		const float HalfHeight = MeshComponent->Bounds.BoxExtent.Z;
		SetActorLocation(Hit.Location + FVector::UpVector * HalfHeight);
	}
}


void AItemActorBase::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();

	// 부착된 상태(손에 들려있음)면 Rep.Location은 부모(소켓) 기준 상대 좌표라서 월드 좌표로 취급하면 안 됨.
	// 월드에 놓여있는(부착 안 된) 경우에만 우리가 직접 이동시킨다.
	if (MeshComponent->GetAttachParent() != nullptr) return;

	// 물리 시뮬레이션 중(드롭/부유 중)이면 Super가 이미 물리 리플리케이션 보간(속도 기반 부드러운 추적)을 처리.
	// 여기서 매번 텔레포트로 덮어쓰면 그 보간이 무시되고 리플리케이션 패킷 마다
	// 위치가 툭툭 튀어서(클라에서 뚝뚝 끊기는 것 처럼) 보임. 물리가 꺼진(정적 배치) 경우에만 보정.
	if (MeshComponent->IsSimulatingPhysics()) return;	

	// Super 내부의 SetActorLocationAndRotation이 물리 바디를 가졌지만 시뮬레이션은 꺼진(클라)
	// 이 컴포넌트를 실제로 못 옮기는 것으로 확인되어, 리플리케이트된 원본 값으로 직접 이동시켜 우회.
	const FRepMovement& Rep = GetReplicatedMovement();
	MeshComponent->SetWorldLocationAndRotation(Rep.Location, Rep.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}


void AItemActorBase::OnRep_ItemData()
{
	SyncVisualsFromItemData();
}

void AItemActorBase::SyncVisualsFromItemData()
{
	if (ItemData && ItemData->Mesh && MeshComponent->GetStaticMesh() != ItemData->Mesh)
	{
		MeshComponent->SetStaticMesh(ItemData->Mesh);
		MeshComponent->SetRelativeScale3D(ItemData->Scale);
	}

	if (ItemData && ItemData->HeldNoiseSound && NoiseAudioComponent->Sound != ItemData->HeldNoiseSound)
	{
		NoiseAudioComponent->SetSound(ItemData->HeldNoiseSound);
		
		if (ItemData->HeldNoiseAttenuation)
		{
			NoiseAudioComponent->AttenuationSettings = ItemData->HeldNoiseAttenuation;
		}
	}

	UpdateNoiseAudio();

}


bool AItemActorBase::CanInteract(APawn* InstigatorPawn) const
{
	return !bIsBeingClaimed;
}

void AItemActorBase::OnInteract(APawn* InstigatorPawn)
{
	ServerGrantToPawn(InstigatorPawn);
}

bool AItemActorBase::ServerGrantToPawn(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn) return false;

	UInventoryComponent* Inventory = InstigatorPawn->FindComponentByClass<UInventoryComponent>();
	if (!Inventory) return false;

	if (!Inventory->TryAddItem(this)) return false;

	bIsBeingClaimed = true;
	bHasBeenPickedUp = true;
	HoldingPawn = InstigatorPawn;

	// 새로 주운 아이템을 바로 활성 슬롯으로 전환(손에 부착까지 여기서 처리됨).
	const int32 SlotIndex = Inventory->FindSlotIndexOf(this);
	if (SlotIndex != INDEX_NONE)
	{
		Inventory->SetActiveSlot(SlotIndex);
	}

	return true;
}

void AItemActorBase::OnRep_HoldingPawn(APawn* OldHoldingPawn)
{
	if (OldHoldingPawn == HoldingPawn) return;
	UpdateAttachment(OldHoldingPawn);
}

void AItemActorBase::UpdateAttachment(APawn* OldHoldingPawn)
{
	if (HoldingPawn && bIsActiveHeld)
	{
		// 활성 슬롯: 손에 보이게 부착.
		MeshComponent->SetVisibility(true, true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// 습득한 아이템의 그림자가 시야를 가리는 것 방지.
		MeshComponent->SetCastShadow(false);

		if (HasAuthority())
		{
			MeshComponent->SetSimulatePhysics(false);
			CancelFloatCycle();
		}

		if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(HoldingPawn))
		{
			Character->AttachItemToRightHand(MeshComponent);
		}
	}

	else if (HoldingPawn && !bIsActiveHeld)
	{
		// 인벤토리엔 있지만 비활성 슬롯: 숨기고 -> 완전히 Detach하지 않고 캐릭터 루트에 재부착 -> 캐릭터 따라다님.
		// (KeepWorldTransform으로 Detach하여 그 순간 월드 좌표에 고정 시켰었으나,
		// 이후 플레이어가 이동한 만큼 나중에 드롭될 때 "Detach 되었던 예전 자리"에서 나타남.
		// 사망/드롭 지점과 동떨어지는 버그 확인되어 수정.

		MeshComponent->AttachToComponent(HoldingPawn->GetRootComponent(), 
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		MeshComponent->SetVisibility(false, true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (HasAuthority())
		{
			MeshComponent->SetSimulatePhysics(false);
			CancelFloatCycle();
		}

		if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(HoldingPawn))
		{
			Character->DetachItemFromRightHand();
		}
	}
	else
	{
		// 아무도 안 들고 있음(드롭/납품 취소 등): 원래 월드 오브젝트 상태로 복원.
		MeshComponent->SetVisibility(true, true);
		MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// 월드에 놓인 오브젝트는 다시 정상적으로 그림자가 생김.
		MeshComponent->SetCastShadow(true);

		if (HasAuthority())
		{
			MeshComponent->SetSimulatePhysics(true);
			BeginFloatCycle();
		}

		if (AGoHomeCharacter* PrevCharacter = Cast<AGoHomeCharacter>(OldHoldingPawn))
		{
			PrevCharacter->DetachItemFromRightHand();
		}
	}

	// HoldingPawn / bIsActiveHeld 바뀔 때마다 소음 오디오 상태도 같이 갱신.
	UpdateNoiseAudio();
}

void AItemActorBase::BeginSinkOrRise()
{
	if (!HasAuthority() || !MeshComponent->IsSimulatingPhysics()) return;

	bIsSinkingOrRising = true;
	bHasReachedSinkSpeed = false; // 새로 가라앉기/떠오르기 시작 -> 정지 판정 초기화.
}

void AItemActorBase::BeginFloatCycle()
{
	bIsSinkingOrRising = false;
	SetActorTickEnabled(true); // 부유 단계부터 흔들림 시작.

	GetWorldTimerManager().SetTimer(SinkOrRiseTimerHandle, this,
		&AItemActorBase::BeginSinkOrRise, FloatDuration, false);
}

void AItemActorBase::CancelFloatCycle()
{
	GetWorldTimerManager().ClearTimer(SinkOrRiseTimerHandle);
	SetActorTickEnabled(false);
	bIsSinkingOrRising = false;
}

float AItemActorBase::GetTotalWeight() const
{
	return ItemData ? ItemData->Weight : 0.f;
}

float AItemActorBase::GetBuoyancyWeight() const
{
	return ItemData ? ItemData->Weight : 1.f;
}


float AItemActorBase::GetCurrentValue() const
{
	if (!ItemData) return 0.f;

	const float PenaltyRatio = ItemData->BreakValuePenaltyPercent * BreakCount;
	const float ValueRatio = FMath::Max(1.f - PenaltyRatio, ItemData->MinValuePercent);
	return ItemData->Value * ValueRatio;
}

void AItemActorBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AItemActorBase, bIsBeingClaimed);
	DOREPLIFETIME(AItemActorBase, BreakCount);
	DOREPLIFETIME(AItemActorBase, CurrentNoiseRadius);
	DOREPLIFETIME(AItemActorBase, HoldingPawn);
	DOREPLIFETIME(AItemActorBase, bIsActiveHeld);
	DOREPLIFETIME(AItemActorBase, ItemData);
	DOREPLIFETIME(AItemActorBase, bHasBeenPickedUp);
}

void AItemActorBase::NotifyHit(UPrimitiveComponent* MyComp,
	AActor* Other,
	UPrimitiveComponent* OtherComp,
	bool bSelfMoved,
	FVector HitLocation,
	FVector HitNormal,
	FVector NormalImpulse,
	const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, 
		             Other, 
		             OtherComp, 
		             bSelfMoved, 
		             HitLocation, 
		             HitNormal, 
		             NormalImpulse, 
		             Hit);

	// 가라앉아서 멈춘(Tick off) 아이템이 플레이어 등과 다시 충동할 경우.
	// 중력이 꺼져 있어 스스로 못내려오기 때문에 Tick을 다시 on 하여 가라앉는 힘을 재 적용 시킴.
	if (HasAuthority() && bIsSinkingOrRising && MeshComponent->IsSimulatingPhysics() && !IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}

	TryApplyBreakFromImpact(GetVelocity().Size());
}

bool AItemActorBase::TryApplyBreakFromImpact(float ImpactSpeed)
{
	if (!HasAuthority() || !ItemData || !ItemData->bCanBreak) return false;
	if (BreakCount >= ItemData->MaxBreakCount) return false;

	if (ImpactSpeed >= ItemData->BreakVelocityThreshold)
	{
		++BreakCount;
		UpdateDamageVisual(); // 서버 자신은 OnRep이 안 뜨므로 수동 호출.
		Multicast_PlayBreakSound(); // 서버 포함 모든 머신에서 재생(NetMulticast는 서버 자신에게도 실행됨).
		return true;
	}
	return false;
}



// NoiseType

void AItemActorBase::NotifyPickedUp()
{
	if (!HasAuthority() || !ItemData || !ItemData->bMakesNoise) return;

	if (CurrentNoiseRadius <= 0.f)
	{
		CurrentNoiseRadius = ItemData->BaseNoiseRadius;
	}

	UGoHomeNoiseLibrary::GenerateNoise(this, GetActorLocation(),
		CurrentNoiseRadius, ENoiseType::Medium, this);

	GetWorldTimerManager().SetTimer(NoiseGrowthTimerHandle, this,
		&AItemActorBase::GrowNoiseRadius, ItemData->NoiseGrowthIntervalSeconds, true);
}

void AItemActorBase::NotifyDropped()
{
	GetWorldTimerManager().ClearTimer(NoiseGrowthTimerHandle);
}

void AItemActorBase::ServerDrop()
{
	if (!HasAuthority() || !HoldingPawn) return;

	APawn* PreviousHolder = HoldingPawn;

	bIsBeingClaimed = false;
	bIsActiveHeld = false;
	HoldingPawn = nullptr;

	UpdateAttachment(PreviousHolder);

	const FVector ThrowDirection = (PreviousHolder->GetActorForwardVector() + FVector::UpVector * 0.3f).GetSafeNormal();
	MeshComponent->SetPhysicsLinearVelocity(ThrowDirection * DropThrowSpeed);

	if (UInventoryComponent* Inventory = PreviousHolder->FindComponentByClass<UInventoryComponent>())
	{
		Inventory->RemoveItem(this);
	}
}

void AItemActorBase::SetActiveHeld(bool bNewActive)
{
	if (!HasAuthority()) return;
	if (bIsActiveHeld == bNewActive) return;

	bIsActiveHeld = bNewActive;
	UpdateAttachment();
}

FText AItemActorBase::GetInteractionPromptText_Implementation() const
{
	const FText PickupText = FText::FromString(TEXT("줍기"));

	// 납품 정산 대상만 가치 표시 - 정산 루프(InventoryComponent::ServerDeliverAllItems)와 같은 기준.
	// 사용 아이템/장비(IsDeliverable false)와 보관함 상품은 기존처럼 "줍기"만.
	if (!IsDeliverable() || IsSharedLockerItem())
	{
		return PickupText;
	}

	// 파손이 반영된 현재 가치. 반올림도 정산과 동일하게 맞춰서, 표시 금액 = 실제 납품 금액.
	return FText::Format(NSLOCTEXT("Item", "PickupWithValue", "줍기(Value {0})"),
		FText::AsNumber(FMath::RoundToInt(GetCurrentValue())));
}

void AItemActorBase::OnRep_IsActiveHeld()
{
	UpdateAttachment();
}

void AItemActorBase::GrowNoiseRadius()
{
	if (!ItemData) return;

	CurrentNoiseRadius = FMath::Min(
		CurrentNoiseRadius + ItemData->NoiseRadiusGrowthPerInterval,
		ItemData->MaxNoiseRadius);

	const ENoiseType Type = (CurrentNoiseRadius >= 1500.f) ? ENoiseType::Large : ENoiseType::Medium;
	UGoHomeNoiseLibrary::GenerateNoise(this, GetActorLocation(), CurrentNoiseRadius, Type, this);

	UpdateNoiseAudio(); // 서버 자신은 OnRep_CurrentNoiseRadius가 안 뜨므로 수동 호출.
}

void AItemActorBase::OnRep_CurrentNoiseRadius()
{
	UpdateNoiseAudio();
}

void AItemActorBase::UpdateNoiseAudio()
{
	if (!NoiseAudioComponent || !ItemData || !ItemData->bMakesNoise || !ItemData->HeldNoiseSound)
	{
		if (NoiseAudioComponent && NoiseAudioComponent->IsPlaying())
		{
			NoiseAudioComponent->Stop();
		}
		return;
	}

	if (!HoldingPawn)
	{
		if (NoiseAudioComponent->IsPlaying())
		{
			NoiseAudioComponent->Stop();
		}
		return;
	}

	const float Ratio = FMath::GetMappedRangeValueClamped(FVector2D(ItemData->BaseNoiseRadius,
		                                                  ItemData->MaxNoiseRadius),
		                                                  FVector2D(0.f, 1.f), CurrentNoiseRadius);

	NoiseAudioComponent->SetVolumeMultiplier(FMath::Lerp(ItemData->MinNoiseVolumeMultiplier, 
		                                                 ItemData->MaxNoiseVolumeMultiplier, Ratio));
	NoiseAudioComponent->SetPitchMultiplier(FMath::Lerp(ItemData->MinNoisePitchMultiplier, 
		                                                ItemData->MaxNoisePitchMultiplier, Ratio));

	if(!NoiseAudioComponent->IsPlaying())
	{ 
		NoiseAudioComponent->Play();
	}
}


void AItemActorBase::SetBeingClaimed(bool bNewClaimed)
{
	if (!HasAuthority()) return;
	bIsBeingClaimed = bNewClaimed;
}

void AItemActorBase::SetExternallyPositioned(bool bExternallyPositioned)
{
	if (!HasAuthority()) return;

	if (bExternallyPositioned)
	{
		MeshComponent->SetSimulatePhysics(false);
		CancelFloatCycle();
	}

	else
	{
		MeshComponent->SetSimulatePhysics(true);
		BeginFloatCycle();
	}
}


void AItemActorBase::OnRep_BreakCount()
{
	UpdateDamageVisual();
}

void AItemActorBase::Multicast_PlayBreakSound_Implementation()
{
	if (!ItemData || !ItemData->BreakSound) return;

	// 인벤토리 안에서 깨진 경우도 아이템이 플레이어에 붙어 있어서, 들고 있는 사람 위치에서 재생됨.
	UGameplayStatics::PlaySoundAtLocation(this, ItemData->BreakSound, GetActorLocation(),
		FRotator::ZeroRotator, 1.f, 1.f, 0.f, ItemData->BreakSoundAttenuation);
}


void AItemActorBase::UpdateDamageVisual()
{
	const bool bShouldShowDamage = ItemData && ItemData->bCanBreak && BreakCount > 0;

	MeshComponent->SetRenderCustomDepth(bShouldShowDamage);

	if (bShouldShowDamage)
	{
		// BreakCount 1 -> DamageStencilValue(기본 3)
		// BreakCount 2 이상 -> +1(기본 4)로 고정.
		// Clamp를 걸어두는 이유 : 나중에 MaxBreakCount가 2보다 커져도 머터리얼이 모르는
		//                        스탠실 값(5, 6...)이 나가서 틴트가 안보이는 사고를 방지하기 위함.

		const int32 Tier = FMath::Clamp(BreakCount, 1, 2);
		MeshComponent->SetCustomDepthStencilValue(DamageStencilValue + Tier - 1);

		// 단계별 균열 텍스처 머터리얼로 교체. 해당 단계 머터리얼이 없을 경우(배열이 비어있으면) 건너뜀.
		if (ItemData->BreakStageMaterials.IsValidIndex(Tier - 1) && ItemData->BreakStageMaterials[Tier - 1])
		{
			MeshComponent->SetMaterial(0, ItemData->BreakStageMaterials[Tier - 1]);
		}

	}
}
