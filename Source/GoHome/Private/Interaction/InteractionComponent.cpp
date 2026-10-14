//THE

#include "Interaction/InteractionComponent.h"
#include "Interaction/Interactable.h"
#include "Core/ExpeditionPreparation.h"
#include "GameFramework/GameStateBase.h"
#include "Interaction/CoopCarryObjectBase.h"
#include "Player/GoHomeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Interaction/DeliveryPoint.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Item/ItemActorBase.h"
#include "Kismet/GameplayStatics.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Server RPC를 쓰려면 컴포넌트 자체가 리플리케이트 되어야 한다.
	// 생성자(CDO 초기화) 시점에는 SetIsReplicated 대신 SetIsReplicatedByDefault를 써야 ensure가 안 뜬다.
	SetIsReplicatedByDefault(true);
}


void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	CachedCamera = GetOwner()->FindComponentByClass<UCameraComponent>();
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	TimeSinceLastTrace += DeltaTime;
	if (TimeSinceLastTrace >= TraceInterval)
	{
		TimeSinceLastTrace = 0.f;
		PerformTrace(); // CurrenTarget 먼저 갱신함.
		UpdateNearbyItemHints(); // 갱신된 CurrentTarget을 제외하고 계산.
	}
}

void UInteractionComponent::PerformTrace()
{
	if (!CachedCamera) return;

	AActor* NewTarget = nullptr;

	bool bIsFocusing = false;
	bool bIsCarrying = false;

	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(GetOwner()))
	{
		bIsFocusing = Character->IsFocusingSwitchboard();
		bIsCarrying = Character->IsCoopCarrying();
	}

	if (bIsCarrying)
	{
		// 운반 중엔 공용 3인칭 화면이라 1인칭 트레이스로 조준할 수 없음 -> 근처 납품 지점을 대상으로 잡음(조준 불필요).
		// 대상 변경 브로드캐스트(HUD 프롬프트)와 TryInteract의 운반 분기는 기존 그대로 사용.
		NewTarget = FindNearbyDeliveryPoint(Cast<APawn>(GetOwner()));
	}

	// 포커스 모드 중엔 일반 조준 트레이스/아웃라인을 끄고 전선 하이라이트에 양보함.
	else if (!bIsFocusing)
	{
		const FVector Start = CachedCamera->GetComponentLocation();
		const FVector End = Start + CachedCamera->GetForwardVector() * TraceDistance;

		FHitResult Hit;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(GetOwner()); // 자기 자신은 무시.

		float BlockDistance = TraceDistance; // 시선이 처음 막힌 거리(안 막혔으면 최대 거리).

		// 1) 선 트레이스 - 정확히 조준했으면 그대로 선택(기존 조작감 유지).
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			BlockDistance = Hit.Distance;
			if (Hit.GetActor() && Hit.GetActor()->Implements<UInteractable>())
			{
				NewTarget = Hit.GetActor();
			}
		}

		// 2) 빗나갔으면 구체 스윕으로 한 번 더 - 살짝 빗나간 조준 보정.
		// 스윕도 처음 막히는 것에서 멈추므로 벽 너머 대상은 잡히지 않음.
		if (!NewTarget && AimAssistRadius > 0.f
			&& GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeSphere(AimAssistRadius), Params))
		{
			BlockDistance = FMath::Min(BlockDistance, Hit.Distance);
			if (Hit.GetActor() && Hit.GetActor()->Implements<UInteractable>())
			{
				NewTarget = Hit.GetActor();
			}
		}

		// 3) 그래도 없으면 - 시선이 레벨 콜리전에 먼저 막혔더라도 겹쳐 놓인 아이템이 있으면 구제.
		if (!NewTarget && OverlapAimTolerance > 0.f)
		{
			NewTarget = FindItemBehindBlocker(Start, End, BlockDistance);
		}
	}

	if (NewTarget != CurrentTarget)
	{
		SetOutlineEnabled(CurrentTarget, false, AimOutlineStencilValue);
		CurrentTarget = NewTarget;
		SetOutlineEnabled(CurrentTarget, true, AimOutlineStencilValue);

		OnInteractableTargetChanged.Broadcast(CurrentTarget);
	}
}

void UInteractionComponent::SetOutlineEnabled(AActor* Target, bool bEnabled, int32 StencilValue)
{
	if (!Target) return;

	// 조준/근접힌트 강조가 꺼지는 경우, 파손된 아이템이면 그냥 끄지 말고
	// 자기 파손 상태(스텐실 3/4 또는 off)로 되돌아가게 함.
	if (!bEnabled)
	{
		if (AItemActorBase* Item = Cast<AItemActorBase>(Target))
		{
			Item->UpdateDamageVisual();
			return;
		}
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Target->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		Component->SetRenderCustomDepth(bEnabled);
		if (bEnabled)
		{
			Component->SetCustomDepthStencilValue(StencilValue);
		}
	}
}

ADeliveryPoint* UInteractionComponent::FindNearbyDeliveryPoint(const APawn* OwnerPawn) const
{
	if (!OwnerPawn) return nullptr;

	TArray<AActor*> DeliveryPoints;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADeliveryPoint::StaticClass(), DeliveryPoints);

	// 서버 재검증(Server_RequestDeliverCarry)과 같은 기준 - 바운딩 박스까지의 거리.
	ADeliveryPoint* Nearest = nullptr;
	float NearestDistSq = FMath::Square(CarryDeliverRadius);

	for (AActor* Actor : DeliveryPoints)
	{
		const float DistSq = Actor->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(OwnerPawn->GetActorLocation());
		if (DistSq <= NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Cast<ADeliveryPoint>(Actor);
		}
	}

	return Nearest;
}


void UInteractionComponent::UpdateNearbyItemHints()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return;

	TArray<AActor*> AllItems;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AItemActorBase::StaticClass(), AllItems);

	const FVector OwnerLocation = OwnerPawn->GetActorLocation();
	const float RadiusSquared = FMath::Square(NearbyHintRadius);

	TSet<TObjectPtr<AActor>> NewNearbyActors;

	for (AActor* Actor : AllItems)
	{
		if (!Actor || Actor == CurrentTarget) continue; // 조준 중인 대상은 제외(조준 강조 우선).

		AItemActorBase* Item = Cast<AItemActorBase>(Actor);
		if (!Item || !Item->CanInteract(OwnerPawn) || Item->HasBeenPickedUp()) continue; // 이미 누가 들고 있거나, 한 번 집혔던(버려진) 아이템은 제외.

		if (FVector::DistSquared(OwnerLocation, Actor->GetActorLocation()) <= RadiusSquared)
		{
			NewNearbyActors.Add(Actor);
		}
	}

	// 범위를 벗어났거나(또는 방금 조준 대상이 된) 아이템 -> 힌트 off.
	for (AActor* OldActor : NearbyHintedActors)
	{
		if (OldActor && !NewNearbyActors.Contains(OldActor) && OldActor != CurrentTarget)
		{
			SetOutlineEnabled(OldActor, false, NearbyHintStencilValue);
		}
	}

	// 새로 범위에 들어온 아이템 -> 힌트 on.
	for (AActor* NewActor : NewNearbyActors)
	{
		if (!NearbyHintedActors.Contains(NewActor))
		{
			SetOutlineEnabled(NewActor, true, NearbyHintStencilValue);
		}
	}
	NearbyHintedActors = NewNearbyActors;
}




namespace
{
	// 탐사 진입 오프닝(팀 대기 ~ 탐사 개시) 동안은 상호작용하지 않는다 — 이동도 잠겨 있다. GameState 구체 클래스 대신 읽기 계약만 본다.
	bool IsExpeditionPreparing(const UObject* Context)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		const IExpeditionPreparation* Preparation = World ? Cast<IExpeditionPreparation>(World->GetGameState()) : nullptr;
		return Preparation && Preparation->IsExpeditionPreparing();
	}
}


AActor* UInteractionComponent::FindItemBehindBlocker(const FVector& Start, const FVector& End, float BlockDistance) const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());

	TArray<AActor*> AllItems;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AItemActorBase::StaticClass(), AllItems);

	const float MaxDist = FMath::Min(TraceDistance, BlockDistance + OverlapAimTolerance);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(FMath::Max(AimAssistRadius, 1.f));

	AActor* Best = nullptr;
	float BestDist = MaxDist;

	for (AActor* Actor : AllItems)
	{
		AItemActorBase* Item = Cast<AItemActorBase>(Actor);
		if (!Item || !Item->CanInteract(OwnerPawn) || !Item->MeshComponent) continue;

		// 시선 쪽에 있는 아이템만 검사 - 비용 절감.
		if (FMath::PointDistToSegmentSquared(Item->GetActorLocation(), Start, End) > FMath::Square(150.f)) continue;

		// 이 아이템의 콜리전만 따로 추적 -> 다른 액터가 가로막고 있어도 무관.
		FHitResult ItemHit;
		if (Item->MeshComponent->SweepComponent(ItemHit, Start, End, FQuat::Identity, Shape, false)
			&& ItemHit.Distance <= BestDist)
		{
			BestDist = ItemHit.Distance;
			Best = Item;
		}
	}
	return Best;
}


void UInteractionComponent::TryInteract()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (IsExpeditionPreparing(this)) return;

	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(OwnerPawn))
	{
		if (Character->IsCoopCarrying())
		{
			if (ADeliveryPoint* DP = Cast<ADeliveryPoint>(CurrentTarget))
			{
				Server_RequestDeliverCarry(DP);

			}
			return;
		}

		if (Character->IsFocusingSwitchboard())
		{
			Character->PressHighlightedKey();
			return;
		}
	}

	IInteractable* Interactable = Cast<IInteractable>(CurrentTarget);
	if (!Interactable) return;

	if (Interactable->CanInteract(OwnerPawn))
	{
		OnInteractRequested.Broadcast(CurrentTarget);
		Server_RequestInteract(CurrentTarget);
	}
}

void UInteractionComponent::Server_RequestInteract_Implementation(AActor* Target)
{
	IInteractable* Interactable = Cast<IInteractable>(Target);

	if (!Interactable) return;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !Target || IsExpeditionPreparing(this)) return;

	// 서버에서 거리 재검증 -> 클라이언트가 임의 액터나 멀리 있는 대상을 지정해도 막힘.
	const float DistSq = Target->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(OwnerPawn->GetActorLocation());
	if (DistSq > FMath::Square(MaxInteractDistance)) return;

	// 서버에서 다시 검증 -> 클라이언트가 보낸 요청을 그대로 신뢰하지 않음.
	if (Interactable->CanInteract(OwnerPawn))
	{
		Interactable->OnInteract(OwnerPawn);
	}

}

void UInteractionComponent::Server_RequestDeliverCarry_Implementation(ADeliveryPoint* DeliveryPoint)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(OwnerPawn);

	if (!Character || !DeliveryPoint || IsExpeditionPreparing(this)) return;

	ACoopCarryObjectBase* CarryObject = Character->GetCurrentCarryObject();
	if (!CarryObject) return;

	// 서버에서 거리 재검증 -> 실제로 납품 지점 근처에 있는지 확인.
	const float DistSq = DeliveryPoint->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(OwnerPawn->GetActorLocation());
	if (DistSq > FMath::Square(MaxInteractDistance)) return;

	CarryObject->ServerDeliver();
}

FText UInteractionComponent::GetInteractionPromptTextFor(AActor* Target)
{
	if (Target && Target->Implements<UInteractable>())
	{
		const FText PromptText = IInteractable::Execute_GetInteractionPromptText(Target);
		if (!PromptText.IsEmpty())
		{
			return PromptText;
		}

	}
	
	return FText::FromString(TEXT("상호작용"));
}





