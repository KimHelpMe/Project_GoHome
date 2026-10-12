#pragma once

#include "CoreMinimal.h"
#include "SharedLockerTypes.generated.h"

// 상점 상품이 팀에 남는 방식. 납품 여부(IsDeliverable)와 무관하게 상품 데이터에서 명시한다.
UENUM(BlueprintType)
enum class EItemShopItemLifetime : uint8
{
	// 사용해도 수량이 줄지 않는 장비. 탐사에서 잃어버리면(사망 후 회수 실패, 잠수정 밖에 두고 귀환) 분실된다.
	Permanent UMETA(DisplayName = "Permanent"),
	// 사용 효과가 실제로 적용됐을 때만 수량이 1 줄어드는 소모품.
	Consumable UMETA(DisplayName = "Consumable")
};

// 잠수정 공유 보관함 저장 단위(호스트 세이브). 팀 전체 보유 수량 = 보관함 안 + 탐사 중 꺼내 간 것.
// 꺼내 간 수량은 런타임 장부(UItemShopSubsystem)만 알기 때문에, 언제 저장되더라도 보유 수량은 그대로 남는다.
USTRUCT(BlueprintType)
struct GOHOME_API FSharedLockerEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	FName ProductId = NAME_None;

	// 저장 시점의 상품 수명 — 카탈로그에서 상품이 빠져도 세이브만으로 영구/소모 구분이 남게 한다.
	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	EItemShopItemLifetime Lifetime = EItemShopItemLifetime::Permanent;

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	int32 OwnedQuantity = 0;
};

// 클라이언트 표시용 복제 미러(AGoHomeGameState). 서버 세이브 + 런타임 장부에서 매번 다시 만든다.
USTRUCT(BlueprintType)
struct GOHOME_API FSharedLockerViewEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	FName ProductId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	EItemShopItemLifetime Lifetime = EItemShopItemLifetime::Permanent;

	// 지금 보관함 안에 있어 꺼낼 수 있는 수량
	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	int32 StoredQuantity = 0;

	// 팀 전체 보유 수량(보관함 안 + 누군가 꺼내 간 것)
	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	int32 OwnedQuantity = 0;
};

// 라운드별 팀 구매 수량(MaxPurchasesPerRun 검증·상점 남은 구매 표시). 정산마다 비운다.
// 세이브(GoHomeSaveGame)와 GameState 복제 미러가 같이 쓰므로 가벼운 이 헤더에 둔다.
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopRoundPurchase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FName ProductId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	int32 Quantity = 0;
};

UENUM(BlueprintType)
enum class ESharedLockerAction : uint8
{
	Withdraw,
	Deposit
};
