#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Shop/ItemShopTypes.h"
#include "SupplyScreenBackend.generated.h"

// 보급 화면을 연 오브젝트. 로비 보급 오브젝트는 메인 분기, 탐사 아이템 박스는 보관함(꺼내기·넣기)으로 바로 들어간다.
UENUM(BlueprintType)
enum class ESupplyEntry : uint8
{
	LobbyHub,
	ExpeditionLocker
};

class UItemShopCatalogDataAsset;

// 구매 결과 + 그 요청의 상품·수량(서버 결과에는 상품이 없어 요청자가 기억해 둔 값)
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSupplyPurchaseResultEvent, const FItemShopPurchaseResult& /*Result*/, FName /*ProductId*/, int32 /*Quantity*/);

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

	// --- 보급 상점 ---
	virtual const UItemShopCatalogDataAsset* GetShopCatalog() const = 0;

	// 한 상품을 수량만큼 주문(장바구니 없음). 진행 중인 요청이 있으면 무시하고 false.
	virtual bool RequestShopPurchase(FName ProductId, int32 Quantity) = 0;

	// 요청 소유는 화면보다 오래 사는 구현체 — 닫았다 다시 열어도 진행 중이면 새 요청을 막는다.
	virtual bool IsShopPurchasePending() const = 0;
	virtual FSupplyPurchaseResultEvent& OnSupplyPurchaseResult() = 0;
};
