
#include "Interaction/CoopCarryObjectBase.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/GoHomeCharacter.h"
#include "Interaction/CoopCarryDataAsset.h"
#include "Core/GoHomeGameState.h"
#include "Core/HudNoticeSubsystem.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"


ACoopCarryObjectBase::ACoopCarryObjectBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	NetDormancy = DORM_Awake;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	HandleA = CreateDefaultSubobject<USceneComponent>(TEXT("HandleA"));
	HandleA->SetupAttachment(MeshComponent);

	HandleB = CreateDefaultSubobject<USceneComponent>(TEXT("HandleB"));
	HandleB->SetupAttachment(MeshComponent);

	// 공용 추적 카메라.
	// 위치는 오브젝트를 따라가되 회전/스케일은 절대 값(메시 회전, 기울기, Roll, 스케일 무시).
	CarryCameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CarryCameraBoom"));
	CarryCameraBoom->SetupAttachment(MeshComponent);
	CarryCameraBoom->SetUsingAbsoluteRotation(true);
	CarryCameraBoom->SetUsingAbsoluteScale(true);
	CarryCameraBoom->TargetArmLength = 450.f;
	CarryCameraBoom->TargetOffset = FVector(0.f, 0.f, 80.f);
	CarryCameraBoom->bUsePawnControlRotation = false;
	CarryCameraBoom->bEnableCameraLag = true; // 클라는 위치/진행 방향을 계단식으로 받으므로 랙으로 부드럽게.
	CarryCameraBoom->bEnableCameraRotationLag = true;
	CarryCameraBoom->CameraLagSpeed = 8.f;
	CarryCameraBoom->CameraRotationLagSpeed = 8.f;
	CarryCameraBoom->PrimaryComponentTick.bStartWithTickEnabled = false; // 두 명 다 잡았을 때만 틱(OnRep_Carriers).

	CarryCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CarryCamera"));
	CarryCamera->SetupAttachment(CarryCameraBoom, USpringArmComponent::SocketName);
}

void ACoopCarryObjectBase::BeginPlay()
{
	Super::BeginPlay();
	SyncFromCarryData();

	// BP에서 랙을 조정했을 수 있으니, 켜질 때 잡깐 끈 뒤 되돌릴 값으로 기억.
	bDefaultCameraLag = CarryCameraBoom->bEnableCameraLag;
	bDefaultCameraRotationLag = CarryCameraBoom->bEnableCameraRotationLag;
}

void ACoopCarryObjectBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 공용 카메라 : 모든 머신이 복제된 진행 방향으로 각자 계산 -> 두 캐리어가 같은 화면을 봄.
	if (IsFullyCarried())
	{
		UpdateCarryCameraRotation();

		if (CarryCameraSnapFrames > 0 && --CarryCameraSnapFrames == 0)
		{
			CarryCameraBoom->bEnableCameraLag = bDefaultCameraLag;
			CarryCameraBoom->bEnableCameraRotationLag = bDefaultCameraRotationLag;
		}
	}

	if (!HasAuthority() || !IsFullyCarried()) return;

	AGoHomeCharacter* CharacterA = Cast<AGoHomeCharacter>(CarrierA);
	AGoHomeCharacter* CharacterB = Cast<AGoHomeCharacter>(CarrierB);
	if (!CharacterA || !CharacterB) return;

	// 둘 사이 거리가 너무 벌어지면(한쪽이 억지로 멀어지려 하면) 강제로 놓침.
	// stuck-드롭을 없앤 이후로는 이게 유일한 자동 해제 수단.
	if (FVector::Dist(CharacterA->GetActorLocation(), CharacterB->GetActorLocation()) > MaxCarryDistance ||
		FVector::Dist(GetActorLocation(), CharacterA->GetActorLocation()) > MaxCarryDistance ||
		FVector::Dist(GetActorLocation(), CharacterB->GetActorLocation()) > MaxCarryDistance)
	{
		ReleaseCarriers();
		return;
	}

	AGoHomeCharacter* MoverCharacter = bCarrierAIsMover ? CharacterA : CharacterB;
	AGoHomeCharacter* RotatorCharacter = bCarrierAIsMover ? CharacterB : CharacterA;

	USceneComponent* HandleForA = bCarrierAOnHandleA ? HandleA.Get() : HandleB.Get();
	USceneComponent* HandleForB = bCarrierAOnHandleA ? HandleB.Get() : HandleA.Get();

	const FVector LocationA = CharacterA->GetActorLocation();
	const FVector LocationB = CharacterB->GetActorLocation();

	// 0) 손잡이로 붙는 단계 : 도착 전엔 오브젝트를 고정하고 캐리어만 손잡이로 이동.
	// (이때 오브젝트가 캐리어 중간점을 따라가면, 같은 쪽에서 잡았을 때 오브젝트가 끌려와 손잡이를 계속 쫓게 됨)
	if (!bHandlesReached)
	{
		HandleReachElapsed += DeltaTime;
		const float ReachError = FMath::Max(
			FVector::Dist(LocationA, HandleForA->GetComponentLocation()),
			FVector::Dist(LocationB, HandleForB->GetComponentLocation()));

		if (ReachError <= HandleArriveTolerance || HandleReachElapsed >= HandleReachTimeout)
		{
			bHandlesReached = true;
		}
	}

	FVector SharedMovement = FVector::ZeroVector;

	if (bHandlesReached)
	{
		// 1) 조향 : 회전 역할의 A/D -> 진행 방향 Yaw, W/S -> 진행 방향 Pitch.
		// 캐릭터가 손잡이를 못 따라오고 있으면(벽에 막힘 등) 그만큼 조향을 늦추고, 많이 벗어나면 멈춤.
		const FVector2D SteerInput = RotatorCharacter->GetLastCarryMoveInput();
		if (!SteerInput.IsNearlyZero())
		{
			const float HandleError = FMath::Max(
				FVector::Dist(LocationA, HandleForA->GetComponentLocation()),
				FVector::Dist(LocationB, HandleForB->GetComponentLocation()));
			const float SteerScale = 1.f - FMath::Clamp(
				(HandleError - HandleLagTolerance) / FMath::Max(HandleLagStopDistance - HandleLagTolerance, 1.f), 0.f, 1.f);

			if (SteerScale > 0.f)
			{
				const float NewYaw = HeadingYaw + SteerInput.X * SteerYawSpeed * SteerScale * DeltaTime;
				const float NewPitch = FMath::Clamp(
					HeadingPitch + SteerInput.Y * SteerPitchSpeed * SteerScale * DeltaTime, -MaxHeadingPitch, MaxHeadingPitch);

				// 진행 방향이 바뀐 만큼을 월드 기준 회전으로 만들어, 두 손잡이의 중간을 중심으로 오브젝트를 돌림.
				const FQuat DeltaQuat = FRotator(NewPitch, NewYaw, 0.f).Quaternion()
					* FRotator(HeadingPitch, HeadingYaw, 0.f).Quaternion().Inverse();
				const FVector Pivot = (HandleForA->GetComponentLocation() + HandleForB->GetComponentLocation()) * 0.5f;
				const FQuat NewRotation = DeltaQuat * GetActorQuat();
				const FVector NewLocation = Pivot + DeltaQuat.RotateVector(GetActorLocation() - Pivot);

				// 회전만 바뀌는 이동은 sweep을 켜도 충돌 검사를 안 하므로(엔진 MoveComponentImpl) 새 자세로 직접 겹침 검사.
				// 이 회전 때문에 "새로" 막히는 경우만 취소 -> 원래 바닥에 닿아 있던 상태에서 조향 자체가 영구히 막히는 것 방지.
				const bool bBlockedByThisRotation =
					IsBlockedAt(NewLocation, NewRotation) && !IsBlockedAt(GetActorLocation(), GetActorQuat());

				if (!bBlockedByThisRotation)
				{
					SetActorLocationAndRotation(NewLocation, NewRotation);
					HeadingYaw = NewYaw;
					HeadingPitch = NewPitch;
				}
			}
		}

		// 2) 오브젝트 위치 : 두 손잡이의 중간이 두 캐릭터의 중간에 오도록 따라감.
		// 평행 이동의 주체는 여전히 캐릭터 -> 각자 로컬 예측 이동(ServerMove) 흐름을 그대로 유지.
		const FVector HandleMidpoint = (HandleForA->GetComponentLocation() + HandleForB->GetComponentLocation()) * 0.5f;
		const FVector CarrierMidpoint = (LocationA + LocationB) * 0.5f;
		SetActorLocation(GetActorLocation() + (CarrierMidpoint - HandleMidpoint), true); // sweep = true -> 벽, 물건 등에 막히면 멈춤.
		ForceNetUpdate(); // 물리 없이 코드로 직접 옮기는 액터라, 다음 정기 갱신 주기를 안 기다리고 바로 리플리케이트 요청.

		// 공유 이동 = 이동 역할 W/S -> 진행 방향 전후, Space/Ctrl -> 상하. 회전 역할 Shift -> 부스트.
		const FVector2D MoveInput = MoverCharacter->GetLastCarryMoveInput();
		const FVector HeadingForward = FRotator(HeadingPitch, HeadingYaw, 0.f).Vector();
		const float BoostMultiplier = RotatorCharacter->IsCarryBoosting() ? CarryBoostMultiplier : 1.0f;
		SharedMovement =
			(HeadingForward * MoveInput.Y + FVector::UpVector * MoverCharacter->GetLastCarryVerticalInput())
			* CarrySpeedScale * BoostMultiplier;
	}

	// 3) 캐릭터 입력/방향 : 공유 이동 + 각자 자기 손잡이로 가는 보정, 몸통은 손잡이 정면 방향.
	// 순간이동/어태치 대신 입력으로 따라가게 해서 클라 예측 보정 튐, 비루트 어태치 리플리케이션 문제를 피함.
	const FVector CorrectionA = ((HandleForA->GetComponentLocation() - LocationA) / HandleFollowRange).GetClampedToMaxSize(1.f);
	const FVector CorrectionB = ((HandleForB->GetComponentLocation() - LocationB) / HandleFollowRange).GetClampedToMaxSize(1.f);

	CharacterA->SetCombinedCarryInput(SharedMovement + CorrectionA);
	CharacterB->SetCombinedCarryInput(SharedMovement + CorrectionB);

	// 몸통 방향 : 서로 상대 손잡이 쪽(수평)을 바라봄 -> 어떻게 돌려도 항상 마주보고 듦.
	const FVector HandleAxis = HandleForB->GetComponentLocation() - HandleForA->GetComponentLocation();
	if (!HandleAxis.IsNearlyZero())
	{
		const float FacingYawA = HandleAxis.Rotation().Yaw;
		CharacterA->SetCarryFacingYaw(FacingYawA);
		CharacterB->SetCarryFacingYaw(FacingYawA + 180.f);
	}
}

void ACoopCarryObjectBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ServerDeliver()처럼 이미 정상적으로 놓아준 경우(CarrierA/B가 이미 nullptr)엔 중복 호출 안 됨.
	// 레벨 스트리밍 언로드/추락 등 예외적인 파괴 경로로도 캐리어 상태가 영구히 안 풀리는 것 방지.
	if (HasAuthority() && (CarrierA || CarrierB))
	{
		ReleaseCarriers();
	}

	Super::EndPlay(EndPlayReason);
}

bool ACoopCarryObjectBase::CanInteract(APawn* InstigatorPawn) const
{
	if (!InstigatorPawn) return false;

	// 빈 핸들이 하나도 없으면 못 잡음.
	if (CarrierA && CarrierB) return false;

	// 이미 다른 걸 운반 중인 폰은 못 잡음.
	if (const AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(InstigatorPawn))
	{
		if (Character->IsCoopCarrying()) return false;
	}

	return true;
}

void ACoopCarryObjectBase::OnInteract(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn) return;
	
	AssignCarrier(InstigatorPawn);
}

bool ACoopCarryObjectBase::AssignCarrier(APawn* Pawn)
{
	if (!CarrierA)
	{
		CarrierA = Pawn;
	}

	else if(!CarrierB)
	{
		CarrierB = Pawn;
	}

	else
	{
		return false; // 이미 둘 다 차있는 상태.
	}

	if (IsFullyCarried())
	{
		// 둘 다 배정된 순간 1회 랜덤으로 이동/회전 역할을 정함(놓칠 때까지 고정).
		bCarrierAIsMover = FMath::RandBool();

		// 손잡이 배정 : 두 캐리어의 이동 거리 합이 짧은 쪽으로(서로 엇갈려 지나가는 것 방지).
		const FVector PawnA = CarrierA->GetActorLocation();
		const FVector PawnB = CarrierB->GetActorLocation();
		const float StraightCost = FVector::Dist(PawnA, HandleA->GetComponentLocation()) + FVector::Dist(PawnB, HandleB->GetComponentLocation());
		const float CrossedCost = FVector::Dist(PawnA, HandleB->GetComponentLocation()) + FVector::Dist(PawnB, HandleA->GetComponentLocation());
		bCarrierAOnHandleA = StraightCost <= CrossedCost;

		// 진행 방향 = 이동 역할이 손잡이에 붙은 뒤 바라보게 될 방향(상대 손잡이 쪽, 수평).
		// 붙기 전 서 있던 방향이 아니라 손잡이 기준이라, 붙고 나서 W가 보는 방향과 어긋나지 않음.
		const USceneComponent* MoverHandle = (bCarrierAIsMover == bCarrierAOnHandleA) ? HandleA.Get() : HandleB.Get();
		const USceneComponent* RotatorHandle = (MoverHandle == HandleA.Get()) ? HandleB.Get() : HandleA.Get();
		HeadingYaw = (RotatorHandle->GetComponentLocation() - MoverHandle->GetComponentLocation()).Rotation().Yaw;
		HeadingPitch = 0.f;

		// 손잡이로 붙는 단계부터 시작.
		bHandlesReached = false;
		HandleReachElapsed = 0.f;
	}

	MeshComponent->SetSimulatePhysics(false); // 상호작용 동안은 물리 끄고 SetActorLocation으로만 이동.
	MeshComponent->IgnoreActorWhenMoving(Pawn, true); // 이동 스윕이 캐리어 본인 캡슐에 걸리는 것 방지.

	// 오브젝트가 캐릭터 이동보다 한 틱 늦게 계산되는 것 방지 -> 캐릭터 틱 이후에 실행되도록 순서 강제.
	AddTickPrerequisiteActor(Pawn);

	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(Pawn))
	{
		Character->SetCoopCarryObject(this);
	}

	OnRep_Carriers(); // 서버 자신에게는 RepNotify가 안 뜨므로 직접 호출.
	return true;
}

void ACoopCarryObjectBase::ReleaseCarriers()
{
	if (!HasAuthority()) return;

	if (CarrierA)
	{ 
		RemoveTickPrerequisiteActor(CarrierA);
		MeshComponent->IgnoreActorWhenMoving(CarrierA, false); // 무시 해제.
	}

	if (CarrierB) 
	{ 
		RemoveTickPrerequisiteActor(CarrierB);
		MeshComponent->IgnoreActorWhenMoving(CarrierB, false);
	}

	if (AGoHomeCharacter* CharacterA = Cast<AGoHomeCharacter>(CarrierA))
	{
		CharacterA->SetCoopCarryObject(nullptr);
	}

	if (AGoHomeCharacter* CharacterB = Cast<AGoHomeCharacter>(CarrierB))
	{
		CharacterB->SetCoopCarryObject(nullptr);
	}

	CarrierA = nullptr;
	CarrierB = nullptr;

	MeshComponent->SetSimulatePhysics(true); // 아무도 안잡고 있으면 물리 켜서 가라앉음.

	OnRep_Carriers();
}

void ACoopCarryObjectBase::OnRep_Carriers()
{
	// 공용 카메라 붐은 두 명 다 잡았을 때만 틱(스프링암 매 프레임 충돌 검사 비용 절약).
	const bool bCarried = IsFullyCarried();
	CarryCameraBoom->SetComponentTickEnabled(bCarried);

	if (bCarried)
	{
		// 켜지는 순간 랙 때문에 이전 운반 때의 위치에서 날아오지 않게, 첫 갱신 한 번은 랙 없이.
		// (스프링암은 랙 여부와 상관없이 매 갱신 이전 위치를 새로 저장하므로 한 번이면 초기화됨)
		UpdateCarryCameraRotation();
		CarryCameraBoom->bEnableCameraLag = false;
		CarryCameraBoom->bEnableCameraRotationLag = false;
		CarryCameraSnapFrames = 2;
	}
}

FText ACoopCarryObjectBase::GetInteractionPromptText_Implementation() const
{
	return FText::FromString(TEXT("함께 옮기기"));
}

void ACoopCarryObjectBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACoopCarryObjectBase, CarrierA);
	DOREPLIFETIME(ACoopCarryObjectBase, CarrierB);
	DOREPLIFETIME(ACoopCarryObjectBase, CarryData);
	DOREPLIFETIME(ACoopCarryObjectBase, bIsBeingDelivered);
	DOREPLIFETIME(ACoopCarryObjectBase, bCarrierAIsMover);
	DOREPLIFETIME(ACoopCarryObjectBase, HeadingYaw);
	DOREPLIFETIME(ACoopCarryObjectBase, HeadingPitch);
}

void ACoopCarryObjectBase::SyncFromCarryData()
{
	if (!CarryData) return;

	if (CarryData->Mesh)
	{
		MeshComponent->SetStaticMesh(CarryData->Mesh);
	}
	SetActorScale3D(CarryData->Scale);
}

void ACoopCarryObjectBase::OnRep_CarryData()
{
	SyncFromCarryData();
}

void ACoopCarryObjectBase::ServerDeliver()
{
	if (!HasAuthority() || bIsBeingDelivered || !CarryData || !IsFullyCarried()) return;
	bIsBeingDelivered = true;

	if (AGoHomeGameState* GameState = GetWorld()->GetGameState<AGoHomeGameState>())
	{
		GameState->AddDeliveredValue(FMath::RoundToInt(CarryData->Value));
	}

	// 함께 옮긴 두 사람 각자에게(본인 알림)
	FHudNotice Notice;
	Notice.Severity = EHudNoticeSeverity::Good;
	Notice.Badge = FText::FromString(TEXT("✓"));
	Notice.Title = FText::Format(NSLOCTEXT("ItemNotice", "CoopDelivered", "{0} 납품"), CarryData->DisplayName);
	Notice.Subtitle = FText::Format(NSLOCTEXT("ItemNotice", "DeliveredSum", "+{0}"), FText::AsNumber(FMath::RoundToInt(CarryData->Value)));
	UHudNoticeSubsystem::SendToPawnOwner(CarrierA, Notice);
	UHudNoticeSubsystem::SendToPawnOwner(CarrierB, Notice);

	ReleaseCarriers();
	Destroy();
}

bool ACoopCarryObjectBase::IsBlockedAt(const FVector& Location, const FQuat& Rotation) const
{
	FComponentQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(CarrierA.Get());
	Params.AddIgnoredActor(CarrierB.Get());

	TArray<FOverlapResult> Overlaps;
	return GetWorld()->ComponentOverlapMultiByChannel(
		Overlaps, MeshComponent, Location, Rotation, MeshComponent->GetCollisionObjectType(), Params);
}

void ACoopCarryObjectBase::UpdateCarryCameraRotation()
{
	// 진행 방향 뒤에서 약간 내려다봄. 기울기는 일부만 따라가고 Roll은 반영 안 함(멀미 방지).
	CarryCameraBoom->SetWorldRotation(FRotator(CarryCameraBasePitch + HeadingPitch * CarryCameraPitchFollow, HeadingYaw, 0.f));
}