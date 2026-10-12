#include "Interaction/ShopStation.h"

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

bool AShopStation::CanInteract(APawn* InstigatorPawn) const
{
	if (!InstigatorPawn)
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

	// 로비: 보급 화면(메인 분기 → 상점 / 조회 전용 보관함). 탐사의 상점 진입 정책은 supply-ui-4에서 정리 — 그때까지 기존 상점 BP.
	if (GetWorld() && Cast<ALobbyGameState>(GetWorld()->GetGameState()))
	{
		PlayerController->Client_OpenSupplyScreen(ESupplyEntry::LobbyHub);
	}
	else
	{
		PlayerController->Client_OpenShop();
	}
}

FText AShopStation::GetInteractionPromptText_Implementation() const
{
	return FText::FromString(TEXT("상점"));
}