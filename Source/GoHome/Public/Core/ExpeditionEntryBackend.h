#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/ExpeditionEntryTypes.h"
#include "ExpeditionEntryBackend.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnExpeditionEntryViewChanged);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UExpeditionEntryBackend : public UInterface
{
	GENERATED_BODY()
};

// UI는 소유 컨트롤러의 FindComponentByInterface로 찾는다. GameMode를 호출하지 않는다.
class GOHOME_API IExpeditionEntryBackend
{
	GENERATED_BODY()
public:
	virtual const FExpeditionEntryView& GetEntryView() const = 0;
	virtual FOnExpeditionEntryViewChanged& OnEntryViewChanged() = 0;
	virtual double GetEntryServerTime() const = 0;
	virtual void FinishOpening(UObject* Context) = 0;
};
