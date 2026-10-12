#include "Shop/ItemShopSubsystem.h"
#include "Shop/ItemShopCatalogDataAsset.h"
#include "Shop/ItemShopTypes.h"
#include "Core/GoHomeGameState.h"
#include "Core/ExpeditionState.h"
#include "Core/Actors/Submarine.h"
#include "Save/GoHomeSaveSubsystem.h"
#include "Interaction/InventoryComponent.h"
#include "Interaction/SharedLockerActor.h"
#include "Item/ItemActorBase.h"
#include "Item/UsableItemBase.h"
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
	const TCHAR* ItemShopCatalogPath =
		TEXT("/Game/GoHome/Developers/LSA/Data/DA_ItemShopCatalog.DA_ItemShopCatalog");

	FText MakeShopMessage(const TCHAR* Message)
	{
		return FText::FromString(Message);
	}
}

void UItemShopSubsystem::Initialize(
	FSubsystemCollectionBase& CollectionBase
)
{
	Super::Initialize(CollectionBase);

	Catalog = LoadObject<UItemShopCatalogDataAsset>(
		nullptr,
		ItemShopCatalogPath
	);

	if (!Catalog)
	{
		// 상점 데이터가 없어도 기존 게임은 계속 실행되어야 함.
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[ItemShop] Catalog load failed. Shop is unavailable.")
		);

		bShopAvailable = false;
		return;
	}

	bShopAvailable = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[ItemShop] Catalog load success. Product count: %d"),
		Catalog->Products.Num()
	);
}

void UItemShopSubsystem::Deinitialize()
{
	ClearCheckouts();

	Catalog = nullptr;
	bShopAvailable = false;

	Super::Deinitialize();
}

const FItemShopProduct* UItemShopSubsystem::FindProduct(
	FName ProductId
) const
{
	if (!bShopAvailable || !Catalog)
	{
		return nullptr;
	}

	return Catalog->FindProduct(ProductId);
}

UGoHomeSaveSubsystem* UItemShopSubsystem::GetSaveSubsystem() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UGoHomeSaveSubsystem>() : nullptr;
}

bool UItemShopSubsystem::TryResolveShopContext(EItemShopContext& OutContext) const
{
	const UWorld* World = GetWorld();
	const AGoHomeGameState* GameState =
		World ? World->GetGameState<AGoHomeGameState>() : nullptr;

	if (!GameState)
	{
		return false;
	}

	switch (GameState->GetCurrentState())
	{
	case EExpeditionState::Lobby:
		OutContext = EItemShopContext::LobbyLoadout;
		return true;
	case EExpeditionState::Exploration:
		OutContext = EItemShopContext::InGameImmediate;
		return true;
	default:
		return false;
	}
}

bool UItemShopSubsystem::TryCalculateOrderTotal(
	const FItemShopPurchaseRequest& Request,
	EItemShopContext Context,
	int32& OutTotalPrice,
	EItemShopResult& OutResult
) const
{
	OutTotalPrice = 0;
	OutResult = EItemShopResult::ShopUnavailable;

	// 카탈로그가 없으면 상점만 사용할 수 없게 한다.
	if (!bShopAvailable || !Catalog)
	{
		return false;
	}

	// 장바구니가 비어 있으면 잘못된 주문이다.
	if (Request.Lines.Num() == 0)
	{
		OutResult = EItemShopResult::InvalidQuantity;
		return false;
	}

	for (const FItemShopCartLine& Line : Request.Lines)
	{
		if (Line.ProductId.IsNone())
		{
			OutResult = EItemShopResult::InvalidProduct;
			return false;
		}

		if (Line.Quantity <= 0)
		{
			OutResult = EItemShopResult::InvalidQuantity;
			return false;
		}

		// 보관함은 팀 공유라 받는 사람을 고를 수 없다.
		if (!Line.TargetPlayerKey.IsEmpty())
		{
			OutResult = EItemShopResult::InvalidTarget;
			return false;
		}

		const FItemShopProduct* Product =
			FindProduct(Line.ProductId);

		if (!Product || !Product->ItemData || !Product->ActorClass || Product->PurchasePrice < 0)
		{
			OutResult = EItemShopResult::InvalidProduct;
			return false;
		}

		if (Line.Quantity > Product->MaxQuantityPerOrder)
		{
			OutResult = EItemShopResult::InvalidQuantity;
			return false;
		}

		const bool bAllowedInContext =
			Context == EItemShopContext::LobbyLoadout
			? Product->bCanBuyInLobby
			: Product->bCanBuyInGame;

		if (!bAllowedInContext)
		{
			OutResult = EItemShopResult::NotAllowedInContext;
			return false;
		}

		const int64 NewTotal =
			static_cast<int64>(OutTotalPrice)
			+ static_cast<int64>(Product->PurchasePrice) * static_cast<int64>(Line.Quantity);

		// 가격 계산이 int32 범위를 넘지 않는지 확인한다.
		if (NewTotal > MAX_int32)
		{
			OutResult = EItemShopResult::InvalidQuantity;
			return false;
		}

		OutTotalPrice = static_cast<int32>(NewTotal);
	}

	OutResult = EItemShopResult::Success;
	return true;
}

bool UItemShopSubsystem::TryProcessPurchase(
	APlayerController* Requester,
	const FItemShopPurchaseRequest& Request,
	FItemShopPurchaseResult& OutResult)
{
	OutResult = FItemShopPurchaseResult();

	// 보관함으로 들어가므로 폰은 필요 없다(관전 중 구매 차단은 상점 단말 상호작용이 담당).
	if (!Requester || !Requester->HasAuthority())
	{
		OutResult.Result = EItemShopResult::InvalidRequester;
		OutResult.Message = MakeShopMessage(TEXT("구매자를 찾을 수 없습니다."));
		return false;
	}

	UGoHomeSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!SaveSubsystem || !SaveSubsystem->GetSaveGame())
	{
		OutResult.Result = EItemShopResult::ShopUnavailable;
		return false;
	}

	const int32 CurrentFunds = SaveSubsystem->GetCurrentFunds();
	OutResult.RemainingFunds = CurrentFunds;

	// 클라가 보낸 Context가 아니라 서버의 실제 게임 단계로 판정한다.
	EItemShopContext Context = EItemShopContext::InGameImmediate;
	if (!TryResolveShopContext(Context))
	{
		OutResult.Result = EItemShopResult::NotAllowedInContext;
		OutResult.Message = MakeShopMessage(TEXT("지금은 구매할 수 없습니다."));
		return false;
	}

	int32 TotalPrice = 0;
	EItemShopResult ValidationResult = EItemShopResult::ShopUnavailable;
	if (!TryCalculateOrderTotal(Request, Context, TotalPrice, ValidationResult))
	{
		OutResult.Result = ValidationResult;
		return false;
	}

	// 상품별 이번 구매 수량을 합친다.
	TMap<FName, int32> RequestedByProduct;
	for (const FItemShopCartLine& Line : Request.Lines)
	{
		RequestedByProduct.FindOrAdd(Line.ProductId) += Line.Quantity;
	}

	// 라운드 한도·보유 상한 확인(팀 공유 기준).
	for (const TPair<FName, int32>& Pair : RequestedByProduct)
	{
		const FItemShopProduct* Product = FindProduct(Pair.Key);
		check(Product); // TryCalculateOrderTotal에서 이미 검증됨

		const int32 RoundPurchased = SaveSubsystem->GetRoundPurchaseQuantity(Pair.Key);
		if (Product->MaxPurchasesPerRun > 0 &&
			RoundPurchased > Product->MaxPurchasesPerRun - Pair.Value)
		{
			OutResult.Result = EItemShopResult::PurchaseLimitReached;
			OutResult.Message = MakeShopMessage(TEXT("이번 라운드의 구매 한도를 초과했습니다."));
			return false;
		}

		const int32 Owned = SaveSubsystem->GetLockerOwnedQuantity(Pair.Key);
		if (Product->MaxOwnedQuantity > 0 &&
			Owned > Product->MaxOwnedQuantity - Pair.Value)
		{
			OutResult.Result = EItemShopResult::OwnedLimitReached;
			OutResult.Message = MakeShopMessage(TEXT("보유 한도를 초과했습니다."));
			return false;
		}
	}

	if (CurrentFunds < TotalPrice)
	{
		OutResult.Result = EItemShopResult::NotEnoughFunds;
		OutResult.Message = MakeShopMessage(TEXT("자금이 부족합니다."));
		return false;
	}

	// --- 커밋: 자금 차감 + 보관함 수량 증가. 실패하면 둘 다 원래 값으로 되돌린다. ---
	struct FLockerUndo
	{
		FName ProductId;
		EItemShopItemLifetime Lifetime;
		int32 OwnedQuantity;
		int32 RoundQuantity;
	};
	TArray<FLockerUndo> Undo;

	if (!SaveSubsystem->TrySpendFunds(TotalPrice))
	{
		OutResult.Result = EItemShopResult::NotEnoughFunds;
		return false;
	}

	bool bCommitted = true;
	for (const TPair<FName, int32>& Pair : RequestedByProduct)
	{
		const FItemShopProduct* Product = FindProduct(Pair.Key);
		const int32 Owned = SaveSubsystem->GetLockerOwnedQuantity(Pair.Key);
		const int32 RoundPurchased = SaveSubsystem->GetRoundPurchaseQuantity(Pair.Key);

		Undo.Add({ Pair.Key, Product->Lifetime, Owned, RoundPurchased });

		if (!SaveSubsystem->SetLockerOwnedQuantity(Pair.Key, Product->Lifetime, Owned + Pair.Value))
		{
			bCommitted = false;
			break;
		}

		SaveSubsystem->SetRoundPurchaseQuantity(Pair.Key, RoundPurchased + Pair.Value);
	}

	if (!bCommitted)
	{
		for (const FLockerUndo& Entry : Undo)
		{
			SaveSubsystem->SetLockerOwnedQuantity(Entry.ProductId, Entry.Lifetime, Entry.OwnedQuantity);
			SaveSubsystem->SetRoundPurchaseQuantity(Entry.ProductId, Entry.RoundQuantity);
		}
		SaveSubsystem->RefundFunds(TotalPrice);

		OutResult.Result = EItemShopResult::ShopUnavailable;
		OutResult.RemainingFunds = SaveSubsystem->GetCurrentFunds();
		OutResult.Message = MakeShopMessage(TEXT("구매를 처리하지 못했습니다."));
		return false;
	}

	// 로비·탐사 공통 진행도 HUD 자금 + 보관함 미러 갱신
	if (UWorld* World = GetWorld())
	{
		if (AGoHomeGameState* GameState = World->GetGameState<AGoHomeGameState>())
		{
			GameState->SetCurrentFunds(SaveSubsystem->GetCurrentFunds());
		}
	}
	RefreshLockerView();

	OutResult.Result = EItemShopResult::Success;
	OutResult.RemainingFunds = SaveSubsystem->GetCurrentFunds();
	OutResult.Message = MakeShopMessage(TEXT("구매 완료 — 잠수정 보관함에 넣었습니다."));
	return true;
}

// --- 공유 보관함 ---

int32 UItemShopSubsystem::GetCheckedOutQuantity(FName ProductId) const
{
	int32 Count = 0;
	for (const FSharedLockerCheckout& Checkout : Checkouts)
	{
		if (Checkout.ProductId == ProductId)
		{
			++Count;
		}
	}
	return Count;
}

TArray<FSharedLockerViewEntry> UItemShopSubsystem::BuildLockerView() const
{
	TArray<FSharedLockerViewEntry> View;

	const UGoHomeSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!SaveSubsystem)
	{
		return View;
	}

	for (const FSharedLockerEntry& Entry : SaveSubsystem->GetSharedLockerEntries())
	{
		FSharedLockerViewEntry& ViewEntry = View.AddDefaulted_GetRef();
		ViewEntry.ProductId = Entry.ProductId;
		ViewEntry.Lifetime = Entry.Lifetime;
		ViewEntry.OwnedQuantity = Entry.OwnedQuantity;
		ViewEntry.StoredQuantity = FMath::Max(0, Entry.OwnedQuantity - GetCheckedOutQuantity(Entry.ProductId));
	}

	return View;
}

void UItemShopSubsystem::RefreshLockerView()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || World->bIsTearingDown)
	{
		return;
	}

	if (AGoHomeGameState* GameState = World->GetGameState<AGoHomeGameState>())
	{
		GameState->SetSharedLockerItems(BuildLockerView());
		if (const UGoHomeSaveSubsystem* SaveSubsystem = GetSaveSubsystem())
		{
			GameState->SetRoundShopPurchases(SaveSubsystem->GetRoundShopPurchases());
		}
	}
}

void UItemShopSubsystem::RemoveOwnedOne(FName ProductId)
{
	UGoHomeSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!SaveSubsystem)
	{
		return;
	}

	const FSharedLockerEntry* Entry = SaveSubsystem->GetSharedLockerEntries().FindByPredicate(
		[ProductId](const FSharedLockerEntry& It) { return It.ProductId == ProductId; });

	if (Entry)
	{
		SaveSubsystem->SetLockerOwnedQuantity(ProductId, Entry->Lifetime, Entry->OwnedQuantity - 1);
	}
}

void UItemShopSubsystem::ReleaseCheckout(int32 CheckoutIndex)
{
	if (!Checkouts.IsValidIndex(CheckoutIndex))
	{
		return;
	}

	if (AItemActorBase* Item = Checkouts[CheckoutIndex].Item.Get())
	{
		Item->OnEndPlay.RemoveDynamic(this, &UItemShopSubsystem::HandleCheckedOutItemEndPlay);
	}

	Checkouts.RemoveAtSwap(CheckoutIndex);
}

void UItemShopSubsystem::ClearCheckouts()
{
	while (Checkouts.Num() > 0)
	{
		ReleaseCheckout(Checkouts.Num() - 1);
	}
}

bool UItemShopSubsystem::IsLockerUsableInCurrentState() const
{
	EItemShopContext UnusedContext;
	return TryResolveShopContext(UnusedContext);
}

bool UItemShopSubsystem::IsNearSharedLocker(const APawn* Pawn) const
{
	UWorld* World = GetWorld();
	if (!World || !Pawn)
	{
		return false;
	}

	for (TActorIterator<ASharedLockerActor> It(World); It; ++It)
	{
		if (It->IsPawnInUseRange(Pawn))
		{
			return true;
		}
	}
	return false;
}

bool UItemShopSubsystem::IsSpentConsumable(const AItemActorBase* Item, EItemShopItemLifetime Lifetime)
{
	if (Lifetime != EItemShopItemLifetime::Consumable)
	{
		return false;
	}

	const AUsableItemBase* Usable = Cast<AUsableItemBase>(Item);
	return Usable && Usable->IsDepleted();
}

bool UItemShopSubsystem::TryWithdrawFromLocker(
	APlayerController* Requester,
	FName ProductId,
	FSharedLockerResult& OutResult)
{
	OutResult = FSharedLockerResult();
	OutResult.Action = ESharedLockerAction::Withdraw;
	OutResult.ProductId = ProductId;

	APawn* Pawn = Requester ? Requester->GetPawn() : nullptr;
	if (!Requester || !Requester->HasAuthority() || !Pawn)
	{
		OutResult.Result = EItemShopResult::InvalidRequester;
		OutResult.Message = MakeShopMessage(TEXT("꺼낼 수 있는 상태가 아닙니다."));
		return false;
	}

	if (!IsLockerUsableInCurrentState())
	{
		OutResult.Result = EItemShopResult::NotAllowedInContext;
		OutResult.Message = MakeShopMessage(TEXT("지금은 보관함을 쓸 수 없습니다."));
		return false;
	}

	if (!IsNearSharedLocker(Pawn))
	{
		OutResult.Result = EItemShopResult::TooFarFromLocker;
		OutResult.Message = MakeShopMessage(TEXT("보관함에서 너무 멉니다."));
		return false;
	}

	const FItemShopProduct* Product = FindProduct(ProductId);
	UGoHomeSaveSubsystem* SaveSubsystem = GetSaveSubsystem();
	if (!Product || !Product->ActorClass || !Product->ItemData || !SaveSubsystem)
	{
		OutResult.Result = EItemShopResult::InvalidProduct;
		return false;
	}

	if (SaveSubsystem->GetLockerOwnedQuantity(ProductId) - GetCheckedOutQuantity(ProductId) <= 0)
	{
		OutResult.Result = EItemShopResult::LockerEmpty;
		OutResult.Message = MakeShopMessage(TEXT("보관함에 남은 수량이 없습니다."));
		return false;
	}

	UInventoryComponent* Inventory = Pawn->FindComponentByClass<UInventoryComponent>();
	bool bHasEmptySlot = false;
	for (int32 SlotIndex = 0; Inventory && SlotIndex < Inventory->GetInventorySlotCount(); ++SlotIndex)
	{
		bHasEmptySlot |= Inventory->GetItemInSlot(SlotIndex) == nullptr;
	}

	if (!bHasEmptySlot)
	{
		OutResult.Result = EItemShopResult::InventoryFull;
		OutResult.Message = MakeShopMessage(TEXT("손이 가득 찼습니다."));
		return false;
	}

	UWorld* World = GetWorld();
	const FTransform SpawnTransform(Pawn->GetActorQuat(), Pawn->GetActorLocation());

	AItemActorBase* SpawnedItem = World
		? World->SpawnActorDeferred<AItemActorBase>(
			Product->ActorClass,
			SpawnTransform,
			nullptr,
			Pawn,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn)
		: nullptr;

	if (!SpawnedItem)
	{
		OutResult.Result = EItemShopResult::SpawnFailed;
		return false;
	}

	SpawnedItem->ItemData = Product->ItemData;
	SpawnedItem->SetSharedLockerItem(true);
	SpawnedItem->FinishSpawning(SpawnTransform);

	// 회복 아이템의 OnInteract(즉시 회복)를 타지 않도록 기본 픽업 경로로 직접 지급한다.
	if (!SpawnedItem->ServerGrantToPawn(Pawn))
	{
		// 아직 장부에 없으므로 이 파괴는 분실로 세지 않는다.
		SpawnedItem->Destroy();
		OutResult.Result = EItemShopResult::InventoryFull;
		OutResult.Message = MakeShopMessage(TEXT("손이 가득 찼습니다."));
		return false;
	}

	FSharedLockerCheckout& Checkout = Checkouts.AddDefaulted_GetRef();
	Checkout.Item = SpawnedItem;
	Checkout.ProductId = ProductId;
	SpawnedItem->OnEndPlay.AddDynamic(this, &UItemShopSubsystem::HandleCheckedOutItemEndPlay);

	RefreshLockerView();

	OutResult.Result = EItemShopResult::Success;
	return true;
}

bool UItemShopSubsystem::TryDepositToLocker(
	APlayerController* Requester,
	FName ProductId,
	FSharedLockerResult& OutResult)
{
	OutResult = FSharedLockerResult();
	OutResult.Action = ESharedLockerAction::Deposit;
	OutResult.ProductId = ProductId;

	APawn* Pawn = Requester ? Requester->GetPawn() : nullptr;
	if (!Requester || !Requester->HasAuthority() || !Pawn)
	{
		OutResult.Result = EItemShopResult::InvalidRequester;
		OutResult.Message = MakeShopMessage(TEXT("넣을 수 있는 상태가 아닙니다."));
		return false;
	}

	if (!IsLockerUsableInCurrentState())
	{
		OutResult.Result = EItemShopResult::NotAllowedInContext;
		OutResult.Message = MakeShopMessage(TEXT("지금은 보관함을 쓸 수 없습니다."));
		return false;
	}

	if (!IsNearSharedLocker(Pawn))
	{
		OutResult.Result = EItemShopResult::TooFarFromLocker;
		OutResult.Message = MakeShopMessage(TEXT("보관함에서 너무 멉니다."));
		return false;
	}

	UInventoryComponent* Inventory = Pawn->FindComponentByClass<UInventoryComponent>();
	if (!Inventory)
	{
		OutResult.Result = EItemShopResult::InvalidRequester;
		return false;
	}

	// 요청자 핫바에 있는 같은 상품 중 손에 든 것을 먼저 고른다.
	int32 FoundIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Checkouts.Num(); ++Index)
	{
		AItemActorBase* Item = Checkouts[Index].Item.Get();
		if (!Item || Checkouts[Index].ProductId != ProductId || Inventory->FindSlotIndexOf(Item) == INDEX_NONE)
		{
			continue;
		}

		FoundIndex = Index;
		if (Item == Inventory->GetActiveItem())
		{
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		OutResult.Result = EItemShopResult::NotLockerItem;
		OutResult.Message = MakeShopMessage(TEXT("넣을 수 있는 아이템을 들고 있지 않습니다."));
		return false;
	}

	AItemActorBase* Item = Checkouts[FoundIndex].Item.Get();
	const FItemShopProduct* Product = FindProduct(ProductId);
	const bool bSpent = Product && IsSpentConsumable(Item, Product->Lifetime);

	ReleaseCheckout(FoundIndex);

	// 다 쓴 소모품은 되돌리지 않고 소모 처리한다(충전량 소진 후 넣었다 빼서 새것으로 바꾸는 우회 방지).
	if (bSpent)
	{
		RemoveOwnedOne(ProductId);
	}

	Inventory->RemoveItem(Item);
	Item->Destroy();

	RefreshLockerView();

	OutResult.Result = EItemShopResult::Success;
	if (bSpent)
	{
		OutResult.Message = MakeShopMessage(TEXT("다 쓴 소모품이라 폐기했습니다."));
	}
	return true;
}

void UItemShopSubsystem::HandleCheckedOutItemEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	const int32 Index = Checkouts.IndexOfByPredicate(
		[Actor](const FSharedLockerCheckout& Checkout) { return Checkout.Item.Get() == Actor; });

	if (Index == INDEX_NONE)
	{
		return;
	}

	const FName ProductId = Checkouts[Index].ProductId;
	Checkouts.RemoveAtSwap(Index);

	// 맵 정리(트래블·PIE 종료)로 사라진 건 보관함에 그대로 남은 것으로 본다.
	// 플레이 중 명시적 파괴 = 사용 성공 소모(회복·텔레포트) 또는 분실(납품·소멸)이라 보유 수량을 줄인다.
	const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	const bool bWorldTeardown =
		EndPlayReason != EEndPlayReason::Destroyed ||
		!World ||
		World->bIsTearingDown ||
		World->IsInSeamlessTravel();

	if (bWorldTeardown)
	{
		return;
	}

	RemoveOwnedOne(ProductId);
	RefreshLockerView();
}

void UItemShopSubsystem::ResolveCheckoutsForRoundEnd()
{
	UWorld* World = GetWorld();

	auto IsHeldByPlayer = [World](AItemActorBase* Item)
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PlayerController = It->Get();
				const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
				const UInventoryComponent* Inventory =
					Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr;

				// 사망한 폰은 이미 ServerDropAllItems로 다 떨어뜨렸으므로 핫바에 있다 = 살아서 들고 있다.
				if (Inventory && Inventory->FindSlotIndexOf(Item) != INDEX_NONE)
				{
					return true;
				}
			}
			return false;
		};

	auto IsInsideSubmarine = [World](const AItemActorBase* Item)
		{
			for (TActorIterator<ASubmarine> It(World); It; ++It)
			{
				if (It->IsLocationInsideInterior(Item->GetActorLocation()))
				{
					return true;
				}
			}
			return false;
		};

	int32 ReturnedCount = 0;
	int32 LostCount = 0;

	while (Checkouts.Num() > 0)
	{
		const int32 Index = Checkouts.Num() - 1;
		const FName ProductId = Checkouts[Index].ProductId;
		AItemActorBase* Item = Checkouts[Index].Item.Get();

		const FItemShopProduct* Product = FindProduct(ProductId);
		const EItemShopItemLifetime Lifetime = Product ? Product->Lifetime : EItemShopItemLifetime::Permanent;

		const bool bHeld = World && Item && IsHeldByPlayer(Item);
		const bool bReturned = World && Item &&
			!IsSpentConsumable(Item, Lifetime) &&
			(bHeld || IsInsideSubmarine(Item));

		ReleaseCheckout(Index);

		if (bReturned)
		{
			++ReturnedCount;
		}
		else
		{
			RemoveOwnedOne(ProductId);
			++LostCount;
		}

		// 정산 중 다시 쓰거나 납품해 수량이 이중으로 움직이지 않도록 꺼내 간 액터는 모두 거둔다.
		if (Item)
		{
			if (bHeld)
			{
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
					if (UInventoryComponent* Inventory = Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr)
					{
						Inventory->RemoveItem(Item);
					}
				}
			}
			Item->Destroy();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ItemShop] Locker round end. Returned: %d, Lost/Consumed: %d"),
		ReturnedCount, LostCount);
}
