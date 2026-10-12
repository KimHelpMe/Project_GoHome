#pragma once

#include "CoreMinimal.h"
#include "Item/ItemActorBase.h"
#include "Shop/SharedLockerTypes.h"
#include "ItemShopTypes.generated.h"

class UItemDataAsset;

// 공용 데이터 구조
UENUM(BlueprintType)
enum class EItemShopContext : uint8
{
	LobbyLoadout UMETA(DisplayName = "Lobby Loadout"),
	InGameImmediate UMETA(DisplayName = "In Game Immediate")
};

UENUM(BlueprintType)
enum class EItemShopCategory : uint8
{
	Tools UMETA(DisplayName = "Tools"),
	Survival UMETA(DisplayName = "Survival"),
	Gear UMETA(DisplayName = "Gear"),
	Defense UMETA(DisplayName = "Defense")
};

UENUM(BlueprintType)
enum class EItemShopResult : uint8
{
	Success,
	ShopUnavailable,
	InvalidRequester,
	InvalidProduct,
	InvalidQuantity,
	InvalidTarget,
	NotEnoughFunds,
	InventoryFull,
	Overweight,
	NotAllowedInContext,
	SpawnFailed,
	PurchaseLimitReached,
	// 아래는 공유 보관함 전환 후 추가(기존 값 순서 유지)
	OwnedLimitReached,
	LockerEmpty,
	NotLockerItem,
	TooFarFromLocker
};

// 보급 상점 상세의 평면 장비 도해(시안 PRESSURE INDEX의 .equipment[data-art]). 최종 아이콘이 아니라 상점 표시용 그림.
UENUM(BlueprintType)
enum class EItemShopArt : uint8
{
	Unknown,
	Hammer,
	Sonar,
	Capsule,
	Oxygen,
	Remote,
	Harpoon,
	Lamp
};

// 상품 하나의 구조
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopProduct
{
	GENERATED_BODY()

public:
	// 아이템 고유 이름 (ex. Hammer)
	// 서버와 UI가 서로 아이템을 찾을 때 사용 (닉네임이나 화면 표시 이름을 ID로 사용 x)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	FName ProductId = NAME_None;

	// 기존 아이템 데이터
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TObjectPtr<UItemDataAsset> ItemData = nullptr;

	// 실제로 생성할 아이템BP (Hammer -> BP_Hammer)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	TSubclassOf<AItemActorBase> ActorClass;

	// 상점 구매 가격 (ItemData.Value 와 다름)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	int32 PurchasePrice = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	EItemShopCategory Category = EItemShopCategory::Tools;

	// 영구 장비(사용해도 안 줄어듦) / 소모품(사용 성공 시 1 감소). 공유 보관함 수명 규칙의 기준.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	EItemShopItemLifetime Lifetime = EItemShopItemLifetime::Permanent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	bool bCanBuyInLobby = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	bool bCanBuyInGame = true;

	// 한번에 몇 개까지 살 수 있는지 (2개 사면 인벤토리 2칸 차지)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop")
	int32 MaxQuantityPerOrder = 1;

	// 한 라운드(로비 구매 + 그 탐사) 동안 팀 전체가 살 수 있는 수량. 정산(FinalizeRound)마다 리셋. 0 이하면 제한 없음.
	// 이름은 기존 에셋/위젯 바인딩 호환을 위해 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop", meta = (ToolTip = "라운드마다 리셋되는 팀 전체 구매 한도. 0 이하면 제한 없음."))
	int32 MaxPurchasesPerRun = 3;

	// 팀이 동시에 보유할 수 있는 최대 수량(보관함 안 + 꺼내 간 것). 0 이하면 제한 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop", meta = (ToolTip = "팀 전체 보유 상한. 0 이하면 제한 없음."))
	int32 MaxOwnedQuantity = 0;

	// --- 보급 상점·보관함 화면 표시용(서버 로직은 읽지 않음) ---

	// 상점·보관함에 보일 이름. 비면 ItemData.DisplayName, 그것도 없으면 ProductId.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Display")
	FText ShopDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Display", meta = (MultiLine = true))
	FText ShopDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop|Display")
	EItemShopArt ShopArt = EItemShopArt::Unknown;

	// [미사용] 공유 보관함 전환 후 플레이어별 소유가 없어져 서버가 읽지 않는다. 기존 상점 위젯의 Break 핀 호환용으로만 남김.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shop", meta = (ToolTip = "사용 안 함 — 팀 보유 상한은 MaxOwnedQuantity"))
	bool bUniquePerPlayer = false;
};

// 장바구니 구조
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopCartLine
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	FName ProductId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	int32 Quantity = 1;

	// 닉네임이 아니라 SteamID 문자열을 저장합니다.
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	FString TargetPlayerKey;
};

// 구매요청 구조 (UI -> 서버 요청)
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopPurchaseRequest
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	TArray<FItemShopCartLine> Lines;

	// 서버는 이 값을 믿지 않고 자기 GameState(로비/탐사)로 구매 장소를 판정한다. 기존 위젯 호환용으로만 남김.
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	EItemShopContext Context = EItemShopContext::InGameImmediate;
};


// [레거시] 공유 보관함 이전의 플레이어별 구매/보유 기록. 세이브 마이그레이션(SaveVersion 0 → 1) 입력으로만 읽는다.
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopLoadoutEntry
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	FString OwnerPlayerKey;

	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	FName ProductId = NAME_None;

	// 지금까지 구매한 총 수량
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	int32 PurchasedQuantity = 0;

	// 현재 실제로 보유한 수량
	UPROPERTY(BlueprintReadWrite, Category = "Shop")
	int32 OwnedQuantity = 0;
};

// 보관함 꺼내기/넣기 결과 (서버 → 요청한 클라이언트). 수량 자체는 GameState 복제 미러로 따로 온다.
USTRUCT(BlueprintType)
struct GOHOME_API FSharedLockerResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	ESharedLockerAction Action = ESharedLockerAction::Withdraw;

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	FName ProductId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	EItemShopResult Result = EItemShopResult::ShopUnavailable;

	UPROPERTY(BlueprintReadOnly, Category = "Shared Locker")
	FText Message;
};

// 구매 결과 구조 (서버가 UI에게 보냄)
USTRUCT(BlueprintType)
struct GOHOME_API FItemShopPurchaseResult
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	EItemShopResult Result = EItemShopResult::ShopUnavailable;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	int32 RemainingFunds = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Shop")
	FText Message;
};

