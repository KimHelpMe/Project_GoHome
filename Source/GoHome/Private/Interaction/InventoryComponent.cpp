//THE

#include "Interaction/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Item/ItemActorBase.h"
#include "Item/UsableItemBase.h"
#include "Player/DeathNotifier.h"
#include "Player/GoHomeCharacter.h"
#include "Interaction/CoopCarryObjectBase.h"
#include "Core/GoHomeGameState.h"
#include "Core/HudNoticeSubsystem.h"
#include "Item/ItemDataAsset.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IDeathNotifier* DeathNotifier = GetOwner()->FindComponentByInterface<IDeathNotifier>())
	{
		DeathNotifier->GetOnDeathDelegate().AddUObject(this, &UInventoryComponent::ServerDropAllItems);
	}
}

float UInventoryComponent::GetTotalWeight() const
{
	float TotalWeight = 0.f;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Item)
		{
			TotalWeight += Slot.Item->GetTotalWeight() * Slot.Quantity;

		}
	}
	return TotalWeight;
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UInventoryComponent, Slots);
	DOREPLIFETIME(UInventoryComponent, ActiveSlotIndex);
}

void UInventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}


bool UInventoryComponent::TryAddItem(AItemActorBase* Item)
{
	if (!Item) return false;

	// 이미 인벤토리 어딘가에 있는 아이템이면 중복 등록 방지.

	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Item == Item) return false;
	}

	const int32 EmptyIndex = FindEmptySlotIndex();
	if (EmptyIndex == INDEX_NONE)
	{
		return false;
	}

	Slots[EmptyIndex].Item = Item;
	Slots[EmptyIndex].Quantity = 1;
	Item->NotifyPickedUp();
	OnInventoryChanged.Broadcast(); // 추가: 서버 자신의 UI도 즉시 갱신함.
	return true;
}

bool UInventoryComponent::RemoveItem(AItemActorBase* Item)
{
	if (!Item) return false;

	for (FInventorySlot& Slot : Slots)
	{
		if (Slot.Item == Item)
		{
			Slot.Item = nullptr;
			Slot.Quantity = 0;
			Item->NotifyDropped();

			// 방금 제거된 아이템이 활성 슬롯이었을 수 있으니, 캐릭터 홀드 상태를 여기서 단일 소스로 재계산.
			if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(GetOwner()))
			{
				Character->SetHoldingItem(GetActiveItem() != nullptr);
			}

			OnInventoryChanged.Broadcast();
			return true;
		}
	}
	return false;
}


int32 UInventoryComponent::FindEmptySlotIndex() const
{
	for (int32 Index = 0; Index < GoHomeInventorySlotCount; ++Index)
	{
		if (Slots[Index].Item == nullptr)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UInventoryComponent::ServerDropAllItems()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	for (FInventorySlot& Slot : Slots)
	{
		if (Slot.Item)
		{
			Slot.Item->ServerDrop();
		}
	}
}

void UInventoryComponent::ServerDeliverAllItems()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	AGoHomeGameState* GameState = GetWorld()->GetGameState<AGoHomeGameState>();
	if (!GameState) return;

	int32 DeliveredCount = 0;
	int32 DeliveredSum = 0;
	UTexture2D* FirstIcon = nullptr;

	for (FInventorySlot& Slot : Slots)
	{
		AItemActorBase* Item = Slot.Item;
		// 보관함 상품은 팀 장비라 납품되면 안 된다(IsDeliverable을 재정의하지 않은 장비가 있어도 안전하게).
		if (!Item || !Item->IsDeliverable() || Item->IsSharedLockerItem()) continue;

		const int32 Value = FMath::RoundToInt(Item->GetCurrentValue());
		GameState->AddDeliveredValue(Value);
		++DeliveredCount;
		DeliveredSum += Value;
		if (!FirstIcon && Item->ItemData)
		{
			FirstIcon = Item->ItemData->Icon;
		}
		// 슬롯 비우기 + NotifyDropped() (소음 타이머 정지).
		RemoveItem(Item);
		Item->Destroy();
	}

	// 납품 1회 = 알림 1줄(아이템별 아님, 합산). 납품한 본인에게만.
	if (DeliveredCount > 0)
	{
		FHudNotice Notice;
		Notice.Severity = EHudNoticeSeverity::Good;
		Notice.Icon = FirstIcon;
		Notice.Badge = FText::FromString(TEXT("✓"));
		Notice.Title = FText::Format(NSLOCTEXT("ItemNotice", "Delivered", "유물 {0}개 납품"), FText::AsNumber(DeliveredCount));
		Notice.Subtitle = FText::Format(NSLOCTEXT("ItemNotice", "DeliveredSum", "+{0}"), FText::AsNumber(DeliveredSum));
		UHudNoticeSubsystem::SendToPawnOwner(Cast<APawn>(GetOwner()), Notice);
	}

}

void UInventoryComponent::TryDropItem(AItemActorBase* ItemToDrop)
{
	if (!ItemToDrop) return;
	Server_RequestDrop(ItemToDrop);
}

void UInventoryComponent::Server_RequestDrop_Implementation(AItemActorBase* ItemToDrop)
{
	if (!ItemToDrop) return;

	// 클라이언트 요청을 그대로 신뢰하지 않음 -> 실제 내 인벤토리에 있는 아이템인지 검증함.
	bool bOwnsItem = false;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Item == ItemToDrop)
		{
			bOwnsItem = true;
			break;
		}
	}

	if (!bOwnsItem) return;

	ItemToDrop->ServerDrop();
}

void UInventoryComponent::TryDropOrReleaseCarry()
{
	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(GetOwner()))
	{
		if (Character->IsCoopCarrying())
		{
			Server_RequestReleaseCarry();
			return;
		}
	}

	TryDropItem(GetActiveItem());
}

void UInventoryComponent::Server_RequestReleaseCarry_Implementation()
{
	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(GetOwner()))
	{
		if (ACoopCarryObjectBase* CarryObject = Character->GetCurrentCarryObject())
		{
			CarryObject->ReleaseCarriers();
		}
	}
}

AItemActorBase* UInventoryComponent::GetItemInSlot(int32 SlotIndex) const
{
	// Slots는 고정 C 배열이라 수동 범위 체크.
	if (SlotIndex < 0 || SlotIndex >= GoHomeInventorySlotCount) return nullptr;
	return Slots[SlotIndex].Item;
}


void UInventoryComponent::SetActiveSlot(int32 NewIndex)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (NewIndex < 0 || NewIndex >= GoHomeInventorySlotCount) return;

	AItemActorBase* OldActive = GetItemInSlot(ActiveSlotIndex);
	AItemActorBase* NewActive = GetItemInSlot(NewIndex);

	if (OldActive && OldActive != NewActive)
	{
		OldActive->SetActiveHeld(false);
	}

	ActiveSlotIndex = NewIndex;

	if (NewActive)
	{
		NewActive->SetActiveHeld(true);
	}

	// bIsHoldingItem을 여기서 단일 소스로 결정.
	// 아이템별 OnRep 경합(리플리케이션 순서 꼬임) 방지.
	if (AGoHomeCharacter* Character = Cast<AGoHomeCharacter>(GetOwner()))
	{
		Character->SetHoldingItem(NewActive != nullptr);
	}
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::TrySetActiveSlot(int32 NewIndex)
{
	Server_RequestSetActiveSlot(NewIndex);
}

void UInventoryComponent::Server_RequestSetActiveSlot_Implementation(int32 NewIndex)
{
	SetActiveSlot(NewIndex);
}

void UInventoryComponent::OnRep_ActiveSlotIndex()
{
	OnInventoryChanged.Broadcast();
}

AItemActorBase* UInventoryComponent::GetActiveItem() const
{
	return GetItemInSlot(ActiveSlotIndex);
}

int32 UInventoryComponent::FindSlotIndexOf(AItemActorBase* Item) const
{
	for (int32 Index = 0; Index < GoHomeInventorySlotCount; ++Index)
	{
		if (Slots[Index].Item == Item) return Index;
	}
	return INDEX_NONE;
}


void UInventoryComponent::TryUseActiveItem()
{
	Server_RequestUseActiveItem();
}

void UInventoryComponent::Server_RequestUseActiveItem_Implementation()
{
	if (AUsableItemBase* UsableItem = Cast<AUsableItemBase>(GetActiveItem()))
	{
		if (UsableItem->CanUse())
		{
			UsableItem->ServerUseSpecialAction();
		}
	}
}

void UInventoryComponent::TryCancelUseActiveItem()
{
	Server_RequestCancelUseActiveItem();
}

void UInventoryComponent::Server_RequestCancelUseActiveItem_Implementation()
{
	// 취소는 CanUse() 검사를 하지 않는다.
	// 채널링 중엔 CanUse()가 false라서(중복 시작 방지) 검사하면 취소가 막힌다.
	if (AUsableItemBase* UsableItem = Cast<AUsableItemBase>(GetActiveItem()))
	{
		UsableItem->ServerCancelSpecialAction();
	}
}