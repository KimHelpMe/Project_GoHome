#include "Interaction/SharedLockerActor.h"

#include "Components/StaticMeshComponent.h"
#include "Core/GoHomePlayerController.h"
#include "Core/LobbyGameState.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

ASharedLockerActor::ASharedLockerActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// BP 자식에서 상자 메쉬로 바꿔 끼울 수 있는 기본 메쉬.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (DefaultMeshAsset.Succeeded())
	{
		MeshComponent->SetStaticMesh(DefaultMeshAsset.Object);
	}
}

bool ASharedLockerActor::CanInteract(APawn* InstigatorPawn) const
{
	return InstigatorPawn && Cast<AGoHomePlayerController>(InstigatorPawn->GetController()) != nullptr;
}

void ASharedLockerActor::OnInteract(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn)
	{
		return;
	}

	// 같은 상자가 맵 단계로 역할을 바꾼다(사용자 결정 2026-10-12): 로비 = 보급 메인 분기(상점 / 조회 전용 보관함),
	// 탐사 = 아이템 박스(보관함 꺼내기·넣기 직행). 로비 조회 전용은 UI 규칙 — 서버 꺼내기·넣기 검증은 그대로.
	if (AGoHomePlayerController* PlayerController =
		Cast<AGoHomePlayerController>(InstigatorPawn->GetController()))
	{
		const bool bLobby = GetWorld() && Cast<ALobbyGameState>(GetWorld()->GetGameState());
		PlayerController->Client_OpenSupplyScreen(bLobby ? ESupplyEntry::LobbyHub : ESupplyEntry::ExpeditionLocker);
	}
}

FText ASharedLockerActor::GetInteractionPromptText_Implementation() const
{
	return FText::FromString(TEXT("보관함"));
}

bool ASharedLockerActor::IsPawnInUseRange(const APawn* Pawn) const
{
	return Pawn && FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(UseRadius);
}
