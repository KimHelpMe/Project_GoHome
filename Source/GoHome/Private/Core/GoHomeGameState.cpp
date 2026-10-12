

#include "Core/GoHomeGameState.h"
#include "Core/DockingDoorComponent.h"
#include "Core/ExplorationGameMode.h"
#include "Core/ExplorationGameState.h"
#include "Data/FExpeditionProgress.h"
#include "Net/UnrealNetwork.h"
#include "Save/GoHomeSaveSubsystem.h"
#include "Shop/ItemShopSubsystem.h"

AGoHomeGameState::AGoHomeGameState()
{
	DockingDoorComponent = CreateDefaultSubobject<UDockingDoorComponent>(TEXT("DockingDoorComponent"));
}

void AGoHomeGameState::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UGoHomeSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UGoHomeSaveSubsystem>();
	if (!SaveSubsystem)
	{
		return;
	}

	SetExpeditionProgress(SaveSubsystem->BuildProgress());

	// GameState는 트래블마다 새로 스폰되므로 보관함 미러도 맵마다 다시 싣는다.
	if (const UItemShopSubsystem* ShopSubsystem = GameInstance->GetSubsystem<UItemShopSubsystem>())
	{
		SetSharedLockerItems(ShopSubsystem->BuildLockerView());
	}
	SetRoundShopPurchases(SaveSubsystem->GetRoundShopPurchases());
}

void AGoHomeGameState::SetSharedLockerItems(const TArray<FSharedLockerViewEntry>& InItems)
{
	if (!HasAuthority())
	{
		return;
	}

	SharedLockerItems = InItems;

	// 리슨 서버 호스트는 OnRep이 불리지 않으므로 서버에서 직접 브로드캐스트
	OnSharedLockerChanged.Broadcast();
}

void AGoHomeGameState::SetRoundShopPurchases(const TArray<FItemShopRoundPurchase>& InPurchases)
{
	if (!HasAuthority())
	{
		return;
	}

	RoundShopPurchases = InPurchases;
	OnSharedLockerChanged.Broadcast();
}

int32 AGoHomeGameState::GetRoundShopPurchaseQuantity(FName ProductId) const
{
	const FItemShopRoundPurchase* Entry = RoundShopPurchases.FindByPredicate(
		[ProductId](const FItemShopRoundPurchase& It) { return It.ProductId == ProductId; });
	return Entry ? Entry->Quantity : 0;
}

void AGoHomeGameState::OnRep_SharedLockerItems()
{
	OnSharedLockerChanged.Broadcast();
}

void AGoHomeGameState::SetExpeditionProgress(const FExpeditionProgress& Progress)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentRound = Progress.CurrentRound;
	FinalRound = Progress.FinalRound;
	CurrentFunds = Progress.CurrentFunds;
	NextCheckPointRound = Progress.NextCheckPointRound;
	NextCheckPointQuota = Progress.NextCheckPointQuota;

	// 리슨 서버 호스트는 OnRep이 불리지 않으므로 서버에서 직접 브로드캐스트
	NotifyExpeditionProgressChanged();
}

void AGoHomeGameState::SetCurrentFunds(int32 InCurrentFunds)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentFunds = InCurrentFunds;
	NotifyExpeditionProgressChanged();
}

void AGoHomeGameState::OnRep_ExpeditionProgress()
{
	NotifyExpeditionProgressChanged();
}

void AGoHomeGameState::NotifyExpeditionProgressChanged()
{
	OnExpeditionProgressChanged.Broadcast();
}

void AGoHomeGameState::SetState(EExpeditionState NewState)
{
	
	CurrentState = NewState;
	OnStateChanged.Broadcast((CurrentState));
	
	// UE_LOG(LogTemp, Warning, TEXT("AGoHomeGameState::SetState(): Class=%s CurrentState=%s"), *GetClass()->GetName(), *UEnum::GetValueAsString(CurrentState));
}

void AGoHomeGameState::AddDeliveredValue(int32 Value)
{
	if (!HasAuthority())
	{
		return;
	}
	
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	
	UGoHomeSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UGoHomeSaveSubsystem>();
	if (!SaveSubsystem)
	{
		return;
	}
	
	const int32 RoundDeliveredTotal = SaveSubsystem->AccumulateDeliveredValue(Value);

	// 호스트 전용 세이브 값을 복제 미러로 밀어 라이브 HUD(자금·할당량)가 받게 한다.
	// 자금을 먼저 실어야 SetRoundDeliveredValue의 호스트 브로드캐스트 시점에 최신 자금이 보인다.
	SetCurrentFunds(SaveSubsystem->GetCurrentFunds());
	if (AExplorationGameState* ExplorationGameState = Cast<AExplorationGameState>(this))
	{
		ExplorationGameState->SetRoundDeliveredValue(RoundDeliveredTotal);
	}
}

void AGoHomeGameState::Fail(EFailReason Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	// GameMode는 서버 전용, 클라는 null
	if (AExplorationGameMode* ExplorationGameMode = GetWorld()->GetAuthGameMode<AExplorationGameMode>())
	{
		ExplorationGameMode->HandleFail(Reason);
	}

	
}

void AGoHomeGameState::OnPlayerRemovedFromParty(APlayerState* PlayerState)
{
}



void AGoHomeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGoHomeGameState, CurrentState);
	DOREPLIFETIME(AGoHomeGameState, CurrentRound);
	DOREPLIFETIME(AGoHomeGameState, FinalRound);
	DOREPLIFETIME(AGoHomeGameState, CurrentFunds);
	DOREPLIFETIME(AGoHomeGameState, NextCheckPointRound);
	DOREPLIFETIME(AGoHomeGameState, NextCheckPointQuota);
	DOREPLIFETIME(AGoHomeGameState, SharedLockerItems);
	DOREPLIFETIME(AGoHomeGameState, RoundShopPurchases);
}

void AGoHomeGameState::OnRep_State()
{
	OnStateChanged.Broadcast(CurrentState);
}
