#include "Save/GoHomeSaveSubsystem.h"
#include "Save/GoHomeSaveGame.h"
#include "Core/ExpeditionState.h"
#include "Kismet/GameplayStatics.h"
#include "Core/GoHomeGameState.h"
#include "Data/EconomyConfigDataAsset.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Upgrade/EquipmentUpgradeSubsystem.h"
#include "Shop/ItemShopTypes.h"
#include "Shop/ItemShopSubsystem.h"
#include "Shop/ItemShopCatalogDataAsset.h"

namespace
{
	const FString GoHomeSaveSlotName = TEXT("GoHomeSave");
	constexpr int32 GoHomeSaveUserIndex = 0;
}

void UGoHomeSaveSubsystem::Initialize(FSubsystemCollectionBase& CollectionBase)
{
	Super::Initialize(CollectionBase);

	// 마이그레이션이 카탈로그(상품 수명)를 읽으므로 상점 서브시스템을 먼저 초기화한다.
	CollectionBase.InitializeDependency<UItemShopSubsystem>();

	// 디스크에 그 이름의 세이브 파일이 실제로 있는지 확인
	if (UGameplayStatics::DoesSaveGameExist(GoHomeSaveSlotName, GoHomeSaveUserIndex))
	{
		SaveGame = Cast<UGoHomeSaveGame>(UGameplayStatics::LoadGameFromSlot(GoHomeSaveSlotName, GoHomeSaveUserIndex));
	}

	// 파일이 없거나, 캐스트에 실패했을 경우
	if (!SaveGame)
	{
		// 새로운 빈 SaveGame 인스턴스를 만듬
		CreateFreshSaveGame();
	}
	else
	{
		MigrateSaveGame();
	}

	EconomyConfig = LoadObject<UEconomyConfigDataAsset>(nullptr, TEXT("/Game/GoHome/Data/DA_EconomyConfig"));
	ensureMsgf(EconomyConfig, TEXT("DA_EconomyConfig 로드 실패 - 경로 확인"));

	// FCoreUObjectDelegates::PostLoadMapWithWorld : 엔진이 맵 로드를 끌낼 때마다 전역으로 쏘는 델리게이트
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UGoHomeSaveSubsystem::OnPostLoadMap);
}

void UGoHomeSaveSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	Super::Deinitialize();
}

void UGoHomeSaveSubsystem::SaveToDisk()
{
	if (!SaveGame)
	{
		return;
	}

	UGameplayStatics::SaveGameToSlot(SaveGame, GoHomeSaveSlotName, GoHomeSaveUserIndex);
}

int32 UGoHomeSaveSubsystem::AccumulateDeliveredValue(int32 Value)
{
	if (!SaveGame)
	{
		return 0;
	}

	// 탐사중인 미션 할당량 반영
	SaveGame->CurrentRoundDeliveredValue += Value;

	// 소유 자금에 납품액 반영
	SaveGame->CurrentFunds += Value;

	return SaveGame->CurrentRoundDeliveredValue;
}

int32 UGoHomeSaveSubsystem::GetCurrentFunds() const
{
	return SaveGame ? SaveGame->CurrentFunds : 0;
}

bool UGoHomeSaveSubsystem::TrySpendFunds(int32 Amount)
{
	if (!SaveGame || Amount < 0)
	{
		return false;
	}

	SaveGame->CurrentFunds -= Amount;

	return true;
}

TArray<FEquipmentUpgradeLevelState>UGoHomeSaveSubsystem::GetSavedUpgradeLevels() const
{
	if (!SaveGame)
	{
		return TArray<FEquipmentUpgradeLevelState>();
	}

	return SaveGame->SavedUpgradeLevels;
}

void UGoHomeSaveSubsystem::SetSavedUpgradeLevels(const TArray<FEquipmentUpgradeLevelState>& InUpgradeLevels)
{
	if (!SaveGame)
	{
		return;
	}

	SaveGame->SavedUpgradeLevels = InUpgradeLevels;
}

FSettlementResult UGoHomeSaveSubsystem::FinalizeRound(bool bForfeited, const TArray<FString>& CasualtyNames)
{
	if (!SaveGame || !EconomyConfig)
	{
		return FSettlementResult();
	}

	FSettlementResult Result;

	// 정산 전에 보관함에서 꺼내 간 상품을 회수/분실 처리한다 — 게임오버로 ResetSave되더라도 순서는 같다.
	UItemShopSubsystem* ItemShopSubsystem = nullptr;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		ItemShopSubsystem =
			GameInstance->GetSubsystem<UItemShopSubsystem>();
	}

	if (ItemShopSubsystem)
	{
		ItemShopSubsystem->ResolveCheckoutsForRoundEnd();
	}

	// 라운드 구매 한도는 정산마다 새로 시작한다.
	SaveGame->RoundShopPurchases.Reset();

	// 이번 턴 납품액 확정
	const int32 RoundDeliveredValue = SaveGame->CurrentRoundDeliveredValue;
	const int32 EffectiveDelivered = bForfeited ? 0 : RoundDeliveredValue;

	// forfeit 면 계산 롤백
	if (bForfeited)
	{
		SaveGame->CurrentFunds -= RoundDeliveredValue;
	}

	// 사망 패널티
	const int32 CasualtyPenalty = CasualtyNames.Num() * EconomyConfig->CasualtyFee;
	SaveGame->CurrentFunds -= CasualtyPenalty;

	// 실질적인 획득량
	const int32 NetGain = EffectiveDelivered - CasualtyPenalty;

	// 라운드 완료
	const int32 CompletedRound = ++(SaveGame->CurrentRound);

	// 스트라이크 판정
	if (EffectiveDelivered < CurrentMapQuota)
	{
		SaveGame->QuotaMissCount++;
	}

	// 체크포인트 판정
	const FCheckPoint* CheckPoint = EconomyConfig->FindCheckPoint(CompletedRound);
	ESettlementOutcome Outcome = DetermineOutcome(CheckPoint, CompletedRound);
	
	// 결과 스냅샷
	Result.bForfeited = bForfeited;
	Result.Outcome = Outcome;
	Result.RoundDeliveredValue = RoundDeliveredValue;
	Result.MapQuota = CurrentMapQuota;
	Result.CasualtyNames = CasualtyNames;
	Result.CasualtyPenalty = CasualtyPenalty;
	Result.NetGain = NetGain;
	Result.bWasCheckPoint = CheckPoint != nullptr;
	Result.CheckPointQuota = CheckPoint ? CheckPoint->TargetQuota : 0;
	Result.CheckPointSchedule = EconomyConfig->CheckPoints;
	Result.ExpeditionProgress = BuildProgress();

	// 상태 반영
	const bool bTerminal =
		Outcome == ESettlementOutcome::GameOver_Strike ||
		Outcome == ESettlementOutcome::GameOver_CheckPoint ||
		Outcome == ESettlementOutcome::Ending;

	if (bTerminal)
	{
		ResetSave();
	}
	else
	{
		SaveGame->CurrentRoundDeliveredValue = 0;
	}
	
	// 트래블 전에 디스크에 남긴다
	SaveToDisk();

	// 회수/분실·초기화 결과를 클라 보관함 미러에 반영한다.
	if (ItemShopSubsystem)
	{
		ItemShopSubsystem->RefreshLockerView();
	}

	// 다음 라운드 출발 때 다시 세팅되므로 필수는 아니지만, 0으로 초기화
	CurrentMapQuota = 0;
	
	return Result;
}

FExpeditionProgress UGoHomeSaveSubsystem::BuildProgress() const
{
	if (!SaveGame)
	{
		return FExpeditionProgress();
	}

	FExpeditionProgress Progress;
	Progress.CurrentRound = SaveGame->CurrentRound;
	Progress.CurrentFunds = SaveGame->CurrentFunds;
	Progress.StrikeCount = SaveGame->QuotaMissCount;

	if (EconomyConfig)
	{
		Progress.FinalRound = EconomyConfig->GetFinalRound();

		if (const FCheckPoint* Next = EconomyConfig->FindNextCheckPoint(SaveGame->CurrentRound))
		{
			Progress.NextCheckPointRound = Next->Round;
			Progress.NextCheckPointQuota = Next->TargetQuota;
		}
	}

	return Progress;
}

bool UGoHomeSaveSubsystem::HasResumableProgress() const
{
	return SaveGame && SaveGame->CurrentRound > 0;
}

bool UGoHomeSaveSubsystem::StartNewExpedition()
{
	ResetSave();

	return SaveGame && UGameplayStatics::SaveGameToSlot(SaveGame, GoHomeSaveSlotName, GoHomeSaveUserIndex);
}

void UGoHomeSaveSubsystem::ResetSave()
{
	// 세이브 데이터뿐만 아니라
	// 런타임에 남아 있는 강화 레벨도 함께 초기화한다.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UEquipmentUpgradeSubsystem* UpgradeSubsystem =
			GameInstance->GetSubsystem<UEquipmentUpgradeSubsystem>())
		{
			UpgradeSubsystem->ResetUpgradeLevels();
		}
	}

	// 깔끔하게 새로 만들어서 기본값으로 초기화 (공유 보관함·라운드 구매 기록 포함)
	CreateFreshSaveGame();

	// 보관함에서 꺼내 간 런타임 장부도 함께 버린다 — 새 세이브에 없는 수량을 나중에 되돌리지 않도록.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UItemShopSubsystem* ItemShopSubsystem =
			GameInstance->GetSubsystem<UItemShopSubsystem>())
		{
			ItemShopSubsystem->ClearCheckouts();
		}
	}
}

void UGoHomeSaveSubsystem::CreateFreshSaveGame()
{
	SaveGame = Cast<UGoHomeSaveGame>(UGameplayStatics::CreateSaveGameObject(UGoHomeSaveGame::StaticClass()));

	if (SaveGame)
	{
		SaveGame->SaveVersion = UGoHomeSaveGame::CurrentSaveVersion;
	}
}

void UGoHomeSaveSubsystem::MigrateSaveGame()
{
	if (!SaveGame || SaveGame->SaveVersion >= UGoHomeSaveGame::CurrentSaveVersion)
	{
		return;
	}

	// 0 → 1: 플레이어별 상점 보유 기록을 팀 공유 보관함으로 합친다.
	// 옛 키(Player_<PlayerId>)는 세션마다 바뀌어 원래 주인을 찾을 수 없으므로, 소유자 구분 없이 상품별로 합산한다.
	if (SaveGame->SaveVersion < 1)
	{
		const UItemShopSubsystem* ItemShopSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UItemShopSubsystem>()
			: nullptr;

		TMap<FName, int32> OwnedByProduct;
		for (const FItemShopLoadoutEntry& Legacy : SaveGame->ItemShopPurchaseStates)
		{
			if (!Legacy.ProductId.IsNone() && Legacy.OwnedQuantity > 0)
			{
				OwnedByProduct.FindOrAdd(Legacy.ProductId) += Legacy.OwnedQuantity;
			}
		}

		for (const TPair<FName, int32>& Pair : OwnedByProduct)
		{
			const FItemShopProduct* Product = ItemShopSubsystem
				? ItemShopSubsystem->FindProduct(Pair.Key)
				: nullptr;

			// 카탈로그에서 사라진 상품은 수명을 알 수 없어 옮기지 않는다.
			if (!Product)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Save] Migration dropped unknown shop product: %s x%d"),
					*Pair.Key.ToString(), Pair.Value);
				continue;
			}

			int32 Quantity = GetLockerOwnedQuantity(Pair.Key) + Pair.Value;
			if (Product->MaxOwnedQuantity > 0)
			{
				Quantity = FMath::Min(Quantity, Product->MaxOwnedQuantity);
			}

			SetLockerOwnedQuantity(Pair.Key, Product->Lifetime, Quantity);
		}

		SaveGame->ItemShopPurchaseStates.Reset();

		UE_LOG(LogTemp, Log, TEXT("[Save] Migrated shop loadout to shared locker. Products: %d"),
			OwnedByProduct.Num());
	}

	SaveGame->SaveVersion = UGoHomeSaveGame::CurrentSaveVersion;
}

ESettlementOutcome UGoHomeSaveSubsystem::DetermineOutcome(const FCheckPoint* CheckPoint, int32 CompletedRound) const
{
	if (SaveGame->QuotaMissCount >= 3)
		return ESettlementOutcome::GameOver_Strike;
	if (CheckPoint && SaveGame->CurrentFunds < CheckPoint->TargetQuota)
		return ESettlementOutcome::GameOver_CheckPoint;
	if (CheckPoint && EconomyConfig->IsEndingRound(CompletedRound))
		return ESettlementOutcome::Ending;
	if (CheckPoint)
		return ESettlementOutcome::CheckPointPassed;
	return ESettlementOutcome::Normal;
}

void UGoHomeSaveSubsystem::OnExpeditionStateChanged(EExpeditionState NewState)
{
	if (NewState == EExpeditionState::Lobby)
	{
		SaveToDisk();
	}
}

void UGoHomeSaveSubsystem::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld != GetGameInstance()->GetWorld() || LoadedWorld->GetNetMode() == NM_Client)
	{
		return;
	}

	AGoHomeGameState* GameState = LoadedWorld->GetGameState<AGoHomeGameState>();
	if (!GameState)
	{
		return;
	}

	GameState->OnStateChanged.AddUniqueDynamic(this, &UGoHomeSaveSubsystem::OnExpeditionStateChanged);

	if (GameState->GetCurrentState() == EExpeditionState::Lobby)
	{
		SaveToDisk();
	}
}

void UGoHomeSaveSubsystem::RefundFunds(int32 Amount)
{
	if (!SaveGame || Amount <= 0)
	{
		return;
	}

	SaveGame->CurrentFunds += Amount;
}

const TArray<FSharedLockerEntry>& UGoHomeSaveSubsystem::GetSharedLockerEntries() const
{
	static const TArray<FSharedLockerEntry> Empty;
	return SaveGame ? SaveGame->SharedLockerItems : Empty;
}

int32 UGoHomeSaveSubsystem::GetLockerOwnedQuantity(FName ProductId) const
{
	if (!SaveGame)
	{
		return 0;
	}

	const FSharedLockerEntry* Entry = SaveGame->SharedLockerItems.FindByPredicate(
		[ProductId](const FSharedLockerEntry& It) { return It.ProductId == ProductId; });

	return Entry ? Entry->OwnedQuantity : 0;
}

bool UGoHomeSaveSubsystem::SetLockerOwnedQuantity(FName ProductId, EItemShopItemLifetime Lifetime, int32 OwnedQuantity)
{
	if (!SaveGame || ProductId.IsNone())
	{
		return false;
	}

	const int32 Index = SaveGame->SharedLockerItems.IndexOfByPredicate(
		[ProductId](const FSharedLockerEntry& It) { return It.ProductId == ProductId; });

	if (OwnedQuantity <= 0)
	{
		if (Index != INDEX_NONE)
		{
			SaveGame->SharedLockerItems.RemoveAt(Index);
		}
		return true;
	}

	FSharedLockerEntry& Entry = Index != INDEX_NONE
		? SaveGame->SharedLockerItems[Index]
		: SaveGame->SharedLockerItems.AddDefaulted_GetRef();

	Entry.ProductId = ProductId;
	Entry.Lifetime = Lifetime;
	Entry.OwnedQuantity = OwnedQuantity;
	return true;
}

const TArray<FItemShopRoundPurchase>& UGoHomeSaveSubsystem::GetRoundShopPurchases() const
{
	static const TArray<FItemShopRoundPurchase> Empty;
	return SaveGame ? SaveGame->RoundShopPurchases : Empty;
}

int32 UGoHomeSaveSubsystem::GetRoundPurchaseQuantity(FName ProductId) const
{
	if (!SaveGame)
	{
		return 0;
	}

	const FItemShopRoundPurchase* Entry = SaveGame->RoundShopPurchases.FindByPredicate(
		[ProductId](const FItemShopRoundPurchase& It) { return It.ProductId == ProductId; });

	return Entry ? Entry->Quantity : 0;
}

void UGoHomeSaveSubsystem::SetRoundPurchaseQuantity(FName ProductId, int32 Quantity)
{
	if (!SaveGame || ProductId.IsNone())
	{
		return;
	}

	const int32 Index = SaveGame->RoundShopPurchases.IndexOfByPredicate(
		[ProductId](const FItemShopRoundPurchase& It) { return It.ProductId == ProductId; });

	if (Quantity <= 0)
	{
		if (Index != INDEX_NONE)
		{
			SaveGame->RoundShopPurchases.RemoveAt(Index);
		}
		return;
	}

	FItemShopRoundPurchase& Entry = Index != INDEX_NONE
		? SaveGame->RoundShopPurchases[Index]
		: SaveGame->RoundShopPurchases.AddDefaulted_GetRef();

	Entry.ProductId = ProductId;
	Entry.Quantity = Quantity;
}
