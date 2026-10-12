#pragma once

#include "CoreMinimal.h"
#include "UI/Supply/SupplyPageBase.h"
#include "Shop/SharedLockerTypes.h"
#include "SupplyLockerPage.generated.h"

class AGoHomeGameState;
class UBorder;
class UScrollBox;
class USizeBox;
class USpacer;
class USupplyEquipmentArt;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UInventoryComponent;
class UVerticalBoxSlot;
struct FItemShopProduct;
struct FSharedLockerResult;

/**
 * 팀 보관함(ON BOARD.). 왼쪽 장비 도해·상세 / 오른쪽 큰 목록. 로비 분기에서 들어오면 조회 전용, 탐사 아이템 박스로 들어오면
 * 꺼내기·넣기(supply-ui-4). 목록은 AGoHomeGameState::SharedLockerItems 중 OwnedQuantity > 0만 — 카탈로그는 이름·설명·도해 보완용.
 * 수량이 바뀌면 선택 ProductId·영역을 유지하고, 선택 항목이 사라지면 같은 자리의 다음 행(없으면 마지막, 비면 분류)으로.
 */
UCLASS()
class GOHOME_API USupplyLockerPage : public USupplyPageBase
{
	GENERATED_BODY()

public:
	virtual void OnPageShown(bool bBack, FName FocusHint) override;
	virtual void OnPageHidden() override;
	virtual void HandleInput(ESupplyInput Input) override;
	virtual void HandleBack() override;
	virtual void OnViewSizeChanged(const FVector2D& ViewSize) override;
	virtual void PlayOpeningLift() override;

protected:
	virtual UWidget* BuildPage(const SupplyUI::FBuilder& B) override;
	virtual void NativeDestruct() override;

private:
	enum class EArea : uint8
	{
		Category,
		List,
		Detail,	// 탐사: 꺼내기·넣기 버튼
		Utility
	};

	struct FActionWidgets
	{
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UWidget> Edge = nullptr;	// 1px 테두리(보조·비활성)
		TObjectPtr<UWidget> FocusRing = nullptr;
		TObjectPtr<UWidget> Cursor = nullptr;
		TArray<TObjectPtr<UTextBlock>> Texts;
	};

	struct FDialogButton
	{
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UWidget> Edge = nullptr;
		TObjectPtr<UWidget> FocusRing = nullptr;
		TObjectPtr<UTextBlock> Text = nullptr;
	};

	struct FCategoryWidgets
	{
		TObjectPtr<UWidget> Card = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UWidget> Content = nullptr;
		TObjectPtr<UWidget> Ring = nullptr;
		TArray<TObjectPtr<UTextBlock>> Texts;
		TObjectPtr<UTextBlock> Count = nullptr;
	};

	struct FRowWidgets
	{
		FName ProductId;
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UWidget> Ring = nullptr;
		TObjectPtr<UWidget> Cursor = nullptr;
		TObjectPtr<UTextBlock> Index = nullptr;
		TObjectPtr<UTextBlock> Name = nullptr;
		TObjectPtr<UTextBlock> Kind = nullptr;
		TObjectPtr<UTextBlock> Quantity = nullptr;
		TObjectPtr<UTextBlock> State = nullptr;
	};

	UWidget* BuildMasthead(const SupplyUI::FBuilder& B);
	UWidget* BuildCategories(const SupplyUI::FBuilder& B);
	UWidget* BuildDetail(const SupplyUI::FBuilder& B);
	UWidget* BuildManifest(const SupplyUI::FBuilder& B);
	UWidget* BuildFooter(const SupplyUI::FBuilder& B);
	UWidget* BuildRow(const SupplyUI::FBuilder& B);
	UWidget* BuildTransfer(const SupplyUI::FBuilder& B);
	UWidget* BuildConfirm(const SupplyUI::FBuilder& B);

	// --- 데이터 ---
	TArray<FSharedLockerViewEntry> GetOwnedEntries() const;
	TArray<FSharedLockerViewEntry> GetVisibleEntries() const;
	const FSharedLockerViewEntry* FindEntry(FName ProductId) const;
	const FItemShopProduct* FindProduct(FName ProductId) const;
	FText GetEntryName(const FSharedLockerViewEntry& Entry) const;
	bool IsCategoryMatch(int32 InCategory, const FSharedLockerViewEntry& Entry) const;
	int32 GetHeld(FName ProductId) const;
	bool IsPending() const;
	bool CanTransfer(ESharedLockerAction InAction, FName ProductId) const;
	FText GetTransferReason(ESharedLockerAction InAction, FName ProductId) const;

	// --- 탐사 조작 ---
	void SetAction(ESharedLockerAction InAction);
	void OpenConfirm(ESharedLockerAction InAction);
	void CancelConfirm();
	void CommitConfirm();
	void RefreshTransfer();
	void RefreshConfirm();
	void HandleLockerResult(const FSharedLockerResult& Result);
	UFUNCTION()
	void HandleInventoryChanged();

	// --- 상태 ---
	void SetCategory(int32 InCategory, bool bAnimate);
	void Select(FName ProductId);
	void SetArea(EArea InArea);
	void Reconcile();
	void ReturnFromLocker();
	void SetStatus(const FText& Message, bool bError);

	// --- 그리기 ---
	void RefreshAll(bool bDetailChanged);
	void RefreshCategories();
	void RefreshRows();
	void RefreshDetail(bool bChanged);
	void RefreshFocus();
	void PlayDetailChange();

	void BindSources();
	void UnbindSources();
	UFUNCTION()
	void HandleLockerChanged();

	TArray<FSharedLockerViewEntry> Entries;
	TArray<FRowWidgets> Rows;
	FCategoryWidgets Categories[3];

	bool bExpedition = false;
	int32 Category = 0;
	TMap<int32, FName> CategoryMemory;
	FName SelectedId = NAME_None;
	EArea Area = EArea::List;
	int32 LastStored = INDEX_NONE;

	TWeakObjectPtr<AGoHomeGameState> BoundGameState;
	TWeakObjectPtr<UInventoryComponent> BoundInventory;
	FDelegateHandle LockerResultHandle;

	ESharedLockerAction Action = ESharedLockerAction::Withdraw;
	bool bConfirmOpen = false;
	ESharedLockerAction ConfirmAction = ESharedLockerAction::Withdraw;
	FName ConfirmId = NAME_None;
	int32 ConfirmFocus = 0;	// 0 취소, 1 요청 확인

	FActionWidgets WithdrawButton;
	FActionWidgets DepositButton;
	FDialogButton CancelButton;
	FDialogButton CommitButton;
	TArray<TObjectPtr<UTextBlock>> ZoneTags;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> PageFrame;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> BackBand;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> CategoryBar;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> CategoryCaret;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> DetailColumn;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ManifestColumn;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> FooterRow;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ViewContextText;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ExitRing;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> ArtBoard;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ArtIndexText;

	UPROPERTY(Transient)
	TObjectPtr<USupplyEquipmentArt> Art;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> DetailCopy;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KindText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> StoredBlock;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StoredText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StoredLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CheckedOutText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeldText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> OwnedText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ActionNoteText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CategoryTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TotalText;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RowsFrame;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RowsScroll;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RowsList;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> EmptyBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyBody;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PositionText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> HelpBox;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> TransferBlock;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBoxSlot> NoteSlot;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ConfirmLayer;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ConfirmDialog;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmTitleText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmDescriptionText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmItemText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmReasonText;
};
