#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/AnnouncerTypes.h"
#include "AnnouncerBackend.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnAnnouncerDisplayChanged);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UAnnouncerBackend : public UInterface
{
	GENERATED_BODY()
};

/**
 * 아나운서 위젯(UI)이 보는 계약. 큐·우선순위·수명은 구현 측(UAnnouncerComponent)이, 등장·퇴장 모션은 위젯이 소유한다.
 * 위젯은 소유 컨트롤러에서 FindComponentByInterface 로 찾는다 — Core 구체 클래스를 include 하지 않는다.
 */
class GOHOME_API IAnnouncerBackend
{
	GENERATED_BODY()

public:
	virtual const FAnnouncerDisplay& GetAnnouncerDisplay() const = 0;
	virtual FOnAnnouncerDisplayChanged& OnAnnouncerDisplayChanged() = 0;
};
