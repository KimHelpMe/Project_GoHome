#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SupplyScreenBackend.generated.h"

// 보급 화면을 연 오브젝트. 로비 보급 오브젝트는 메인 분기, 탐사 아이템 박스는 보관함(꺼내기·넣기)으로 바로 들어간다.
UENUM(BlueprintType)
enum class ESupplyEntry : uint8
{
	LobbyHub,
	ExpeditionLocker
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USupplyScreenBackend : public UInterface
{
	GENERATED_BODY()
};

/**
 * 보급 화면(UI/Supply)이 아는 요청 계약. 구현체(AGoHomePlayerController)가 위젯 생성·입력 모드·제거를 소유하고,
 * 화면은 진입 경로를 묻고 닫기만 요청한다. 상점/보관함 요청·결과는 단계별로 이 계약에 추가한다.
 */
class GOHOME_API ISupplyScreenBackend
{
	GENERATED_BODY()

public:
	virtual ESupplyEntry GetSupplyEntry() const = 0;

	// 위젯 제거와 게임 입력·커서 복원(퇴장 연출은 화면이 끝낸 뒤 부른다)
	virtual void RequestCloseSupplyScreen() = 0;
};
