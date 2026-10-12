#pragma once

#include "CoreMinimal.h"
#include "UI/Supply/SupplyPageBase.h"
#include "Shop/ItemShopTypes.h"
#include "SupplyShopPage.generated.h"

class AGoHomeGameState;
class UBorder;
class UOverlay;
class UScrollBox;
class USizeBox;
class USupplyEquipmentArt;
class UTextBlock;
class UVerticalBox;

/**
 * 보급 상점(NEXT DIVE.). 왼쪽 카탈로그 / 오른쪽 상세+밝은 주문서, 실행 직전에만 구매 전표.
 * 상품은 카탈로그가 원본(미보유도 표시), 자금·보유·보관·라운드 구매는 AGoHomeGameState 미러, 구매는 ISupplyScreenBackend 요청만 한다.
 * 선택 상품(종이색 면)과 조작 위치(라임 커서/테두리)는 따로 그린다. 출처: candidate-persona-locker README "보급 상점 추가".
 */
UCLASS()
class GOHOME_API USupplyShopPage : public USupplyPageBase
{
	GENERATED_BODY()

public:
	virtual void OnPageShown(bool bBack, FName FocusHint) override;
	virtual void OnPageHidden() override;
	virtual void HandleInput(ESupplyInput Input) override;
	virtual void HandleBack() override;
	virtual void OnViewSizeChanged(const FVector2D& ViewSize) override;

protected:
	virtual UWidget* BuildPage(const SupplyUI::FBuilder& B) override;
	virtual void NativeDestruct() override;

private:
	enum class EArea : uint8
	{
		Category,
		List,
		Quantity,
		Buy,
		Utility
	};

	struct FFilterWidgets
	{
		TObjectPtr<UWidget> Card = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UTextBlock> Label = nullptr;
		TObjectPtr<UWidget> Ring = nullptr;
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
		TObjectPtr<UTextBlock> Price = nullptr;
		TObjectPtr<UTextBlock> Stock = nullptr;
	};

	struct FButtonWidgets
	{
		TObjectPtr<UWidget> Root = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TObjectPtr<UWidget> Ring = nullptr;
		TArray<TObjectPtr<UTextBlock>> Texts;
	};

	// --- 조립 ---
	UWidget* BuildMasthead(const SupplyUI::FBuilder& B);
	UWidget* BuildCatalog(const SupplyUI::FBuilder& B);
	UWidget* BuildPanel(const SupplyUI::FBuilder& B);
	UWidget* BuildFooter(const SupplyUI::FBuilder& B);
	UWidget* BuildConfirm(const SupplyUI::FBuilder& B);
	UWidget* BuildRow(const SupplyUI::FBuilder& B, const FItemShopProduct& Product);
	void BuildRows();

	// --- 데이터 ---
	const FItemShopProduct* FindProduct(FName ProductId) const;
	TArray<const FItemShopProduct*> GetVisibleProducts() const;
	FText GetProductName(const FItemShopProduct& Product) const;
	FText GetLifetimeShort(const FItemShopProduct& Product) const;
	int32 GetOwned(FName ProductId) const;
	int32 GetStored(FName ProductId) const;
	int32 GetRoundPurchased(FName ProductId) const;
	int32 GetFunds() const;
	bool IsPending() const;

	// 비면 구매 가능. 시안 reason() 순서.
	FText GetBlockReason(FName ProductId, int32 InQuantity) const;

	// --- 상태 전이 ---
	void SetCategory(int32 InCategory, bool bAnimate);
	void Select(FName ProductId, bool bFromNavigation);
	void SetArea(EArea InArea);
	void ChangeQuantity(int32 Delta);
	void OpenConfirm();
	void CancelConfirm();
	void CommitConfirm();
	void ReturnToHub();
	void SetStatus(const FText& Message, bool bError);

	// --- 그리기 ---
	void RefreshAll();
	void RefreshFilters();
	void RefreshRows();
	void RefreshDetail();
	void RefreshConfirm();
	void RefreshFocus();
	void SetButtonFocused(FButtonWidgets& Button, bool bFocused);

	// --- 구독 ---
	void BindSources();
	void UnbindSources();
	UFUNCTION()
	void HandleProgressChanged();
	UFUNCTION()
	void HandleLockerChanged();
	void HandlePurchaseResult(const FItemShopPurchaseResult& Result, FName ProductId, int32 InQuantity);

	TArray<FItemShopProduct> Products;
	TArray<FRowWidgets> Rows;
	FFilterWidgets Filters[3];
	FButtonWidgets MinusButton;
	FButtonWidgets PlusButton;
	FButtonWidgets BuyButton;
	FButtonWidgets ExitButton;
	FButtonWidgets CancelButton;
	FButtonWidgets CommitButton;

	int32 Category = 0;	// 0 전체, 1 장비(Permanent), 2 소모품(Consumable)
	TMap<int32, FName> CategoryMemory;
	FName SelectedId = NAME_None;
	int32 Quantity = 1;
	EArea Area = EArea::List;
	bool bConfirmOpen = false;
	FName ConfirmId = NAME_None;
	int32 ConfirmQuantity = 1;
	int32 ConfirmFocus = 0;	// 0 취소, 1 구매 요청
	int32 LastFunds = INDEX_NONE;

	TWeakObjectPtr<AGoHomeGameState> BoundGameState;
	FDelegateHandle PurchaseResultHandle;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> PageFrame;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> WalletFlash;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FundsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FundsLabel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> RowsFrame;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RowsScroll;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RowsList;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> EmptyBox;

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
	TObjectPtr<UTextBlock> OwnedText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StoredText;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> QuantityShadow;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> QuantityFace;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> QuantityRing;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> QuantityText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TotalText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReasonText;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> ConfirmLayer;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Ticket;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmNameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmQuantityText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmTotalText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmAfterText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConfirmReasonText;
};
