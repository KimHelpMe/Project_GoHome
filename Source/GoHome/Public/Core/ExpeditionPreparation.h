#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ExpeditionPreparation.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UExpeditionPreparation : public UInterface
{
	GENERATED_BODY()
};

// Player/Interaction은 구체 GameState 대신 이 읽기 계약만 참조한다.
class GOHOME_API IExpeditionPreparation
{
	GENERATED_BODY()
public:
	virtual bool IsExpeditionPreparing() const = 0;
};
