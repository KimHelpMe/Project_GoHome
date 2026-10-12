#include "Interaction/ShopStation.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/GoHomePlayerController.h"
#include "Core/LobbyGameState.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

AShopStation::AShopStation()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// BP 자식에서 상점 터미널 메쉬로 바꿔 끼울 수 있는 기본 메쉬.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (DefaultMeshAsset.Succeeded())
	{
		MeshComponent->SetStaticMesh(DefaultMeshAsset.Object);
	}
}

void AShopStation::BeginPlay()
{
	Super::BeginPlay();

	// 탐사 맵: 조준 트레이스(Visibility)에 안 걸리게 해서 프롬프트·외곽선 자체가 안 뜨게 한다(CanInteract는 서버 재검증용).
	// 클라 BeginPlay는 GameState 복제 후(HasBegunPlay)라 판정이 가능하다.
	if (!IsInLobby())
	{
		TArray<UPrimitiveComponent*> Primitives;
		GetComponents<UPrimitiveComponent>(Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		}
	}
}

bool AShopStation::IsInLobby() const
{
	const UWorld* World = GetWorld();
	return World && Cast<ALobbyGameState>(World->GetGameState()) != nullptr;
}

bool AShopStation::CanInteract(APawn* InstigatorPawn) const
{
	if (!InstigatorPawn || !IsInLobby())
	{
		return false;
	}

	return Cast<AGoHomePlayerController>(InstigatorPawn->GetController()) != nullptr;
}

void AShopStation::OnInteract(APawn* InstigatorPawn)
{
	if (!HasAuthority() || !InstigatorPawn)
	{
		return;
	}

	AGoHomePlayerController* PlayerController = Cast<AGoHomePlayerController>(InstigatorPawn->GetController());
	if (!PlayerController)
	{
		return;
	}

	// 보급 화면(메인 분기 → 상점 / 조회 전용 보관함). 탐사에서는 CanInteract가 막는다.
	PlayerController->Client_OpenSupplyScreen(ESupplyEntry::LobbyHub);
}

FText AShopStation::GetInteractionPromptText_Implementation() const
{
	return FText::FromString(TEXT("상점"));
}