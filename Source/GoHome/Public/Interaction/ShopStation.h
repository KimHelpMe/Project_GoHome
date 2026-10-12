

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "ShopStation.generated.h"


class APawn;
class UStaticMeshComponent;


UCLASS()
class GOHOME_API AShopStation : public AActor, public IInteractable
{
	GENERATED_BODY()
	
public:
	AShopStation();

	virtual bool CanInteract(APawn* InstigatorPawn) const override;
	virtual void OnInteract(APawn* InstigatorPawn) override;
	virtual FText GetInteractionPromptText_Implementation() const override;

protected:
	virtual void BeginPlay() override;

private:
	// 상점(보급 화면)은 로비 전용. 탐사 맵에 놓인 단말은 상호작용 대상이 아니다.
	bool IsInLobby() const;

	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

};
