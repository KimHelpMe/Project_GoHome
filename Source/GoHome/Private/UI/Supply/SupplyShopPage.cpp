#include "UI/Supply/SupplyShopPage.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/GoHomeGameState.h"
#include "Core/SupplyScreenBackend.h"
#include "Item/ItemDataAsset.h"
#include "Shop/ItemShopCatalogDataAsset.h"
#include "UI/Supply/SupplyEquipmentArt.h"
#include "UI/Supply/SupplyHubPage.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyScreenWidget.h"

#define LOCTEXT_NAMESPACE "SupplyShop"

using namespace SupplyUI;

namespace
{
	constexpr int32 ShopConfirmLayerIndex = 1;

	const FName ShopListTrack(TEXT("Shop.List"));
	const FName ShopDetailTrack(TEXT("Shop.Detail"));
	const FName ShopArtTrack(TEXT("Shop.Art"));
	const FName ShopCursorTrack(TEXT("Shop.Cursor"));
	const FName ShopWalletTrack(TEXT("Shop.Wallet"));
	const FName ShopTicketTrack(TEXT("Shop.Ticket"));

	FText Money(int32 Value)
	{
		return FText::Format(LOCTEXT("Money", "{0} C"), FText::AsNumber(Value));
	}

	// CSS outline: 바깥으로 Offset 띄우고 Width 두께
	UWidget* AddOutline(const FBuilder& B, UOverlay* Card, const FLinearColor& Color, float Width, float Offset)
	{
		UWidget* Ring = B.Ring(Color, Width);
		AddOverlay(Card, Ring, HAlign_Fill, VAlign_Fill, FMargin(-(Offset + Width)));
		Ring->SetVisibility(ESlateVisibility::Hidden);
		return Ring;
	}

	bool IsCategoryMatch(int32 Category, const FItemShopProduct& Product)
	{
		return Category == 0
			|| (Category == 1 && Product.Lifetime == EItemShopItemLifetime::Permanent)
			|| (Category == 2 && Product.Lifetime == EItemShopItemLifetime::Consumable);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// 조립

UWidget* USupplyShopPage::BuildPage(const FBuilder& B)
{
	// .supply: max-width 1920 가운데, padding clamp(20px,3vw,54px)=54, gap 20, 남청 사선 면 inset 25% -10% 20% / -8°
	PagePadding = FMargin(54.f);

	UOverlay* Root = B.Overlay();
	AddFill(Root, B.Fill(Ink()));

	UOverlay* Frame = B.Overlay();
	Frame->SetClipping(EWidgetClipping::ClipToBounds);
	PageFrame = Frame;
	AddFill(Root, Frame);
	AddFill(Frame, MakeBand(B, Navy(), -0.10f, 0.25f, 1.10f, 0.80f, -8.f));

	UVerticalBox* Column = B.VBox();
	AddOverlay(Frame, Column, HAlign_Fill, VAlign_Fill, PagePadding);

	Masthead = BuildMasthead(B);
	AddV(Column, Masthead);

	// .supply-grid: minmax(0,1.25fr) / clamp(28px,4vw,70px)=70 / minmax(300px,.85fr)
	UHorizontalBox* Grid = B.HBox();
	AddH(Grid, BuildCatalog(B), FMargin(0.f), VAlign_Top, 1.25f);
	AddH(Grid, B.Spacer(70.f, 0.f));
	AddH(Grid, BuildPanel(B), FMargin(0.f), VAlign_Top, .85f);
	AddV(Column, Grid, FMargin(0.f, 20.f), HAlign_Fill, true);

	Footer = BuildFooter(B);
	AddV(Column, Footer);

	ConfirmLayer = BuildConfirm(B);
	AddFill(Root, ConfirmLayer);
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	return Root;
}

UWidget* USupplyShopPage::BuildMasthead(const FBuilder& B)
{
	UHorizontalBox* Head = B.HBox();

	// heading-block: 브랜드 / 기울어진 라임 막대 / 제목 (gap 24, 세로 가운데)
	UHorizontalBox* Heading = B.HBox();
	{
		UVerticalBox* Brand = B.VBox();
		AddV(Brand, B.LineBox(B.Text(LOCTEXT("Kicker", "LOBBY / SUPPLY DESK"), Font(EFace::Bold, 11.f, .18f), Lime()), 16.5f, ERef::Segoe), FMargin(0.f, 0.f, 0.f, 7.f));
		AddV(Brand, B.LineBox(B.Text(LOCTEXT("Wordmark", "GOHOME"), Font(EFace::Display, 48.f, -.08f, true), Paper()), 45.6f, ERef::ArialBlack));
		AddH(Heading, Brand, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		USizeBox* Slash = B.Size(5.f, 55.f);
		Slash->AddChild(B.Fill(Lime()));
		Slash->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		Slash->SetRenderTransformAngle(15.f);
		AddH(Heading, Slash, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		UTextBlock* Title = B.Text(LOCTEXT("Title", "보급 상점"), Font(EFace::Bold, 30.f, -.07f), Paper());
		AddH(Heading, B.LineBox(Title, 34.5f), FMargin(0.f, 4.f, 0.f, 0.f), VAlign_Center);
	}
	AddH(Head, Heading, FMargin(0.f), VAlign_Top);
	AddH(Head, B.Spacer(24.f, 0.f), FMargin(0.f), VAlign_Top, 1.f);

	// header-side: 팀 공동 자금 / 메인 분기 (gap 24, 세로 가운데)
	UHorizontalBox* Side = B.HBox();
	{
		UOverlay* Wallet = B.Overlay();
		UBorder* Flash = B.Fill(FLinearColor::Transparent);
		WalletFlash = Flash;
		AddFill(Wallet, Flash);
		UVerticalBox* WalletColumn = B.VBox();
		FundsLabel = B.LineBox(B.Text(LOCTEXT("FundsLabel", "팀 공동 자금"), Font(EFace::Regular, 11.f), Lime()));
		AddV(WalletColumn, FundsLabel, FMargin(16.f, 5.f, 16.f, 0.f), HAlign_Right);
		// strong(900 italic 32px/1.2)이 부모 줄(맑은 고딕 15px 기준) 안에 들어가 줄 상자 43
		FundsText = B.Text(Dash(), Font(EFace::Black, 32.f, 0.f, true), Paper());
		Typeset(FundsText, ERef::Segoe, 43.f);
		AddV(WalletColumn, FundsText, FMargin(16.f, 0.f, 16.f, 0.f), HAlign_Right);
		AddV(WalletColumn, Rule(B, Lime(), 3.f));
		AddFill(Wallet, WalletColumn);
		AddH(Side, Wallet, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		// .exit: 13px, padding 10 0 10 12, b 22px(ml 10), border-bottom 1px — 두 글자가 한 기준선을 공유
		UOverlay* ExitCard = B.Overlay();
		UVerticalBox* Exit = B.VBox();
		UHorizontalBox* ExitRow = B.HBox();
		const float ExitBaseline = CssBaseline(22.f, ERef::Malgun, 29.f);
		UTextBlock* ExitLabel = B.Text(LOCTEXT("Exit", "메인 분기"), Font(EFace::Regular, 13.f), Paper());
		Typeset(ExitLabel, ERef::Malgun, 29.f, ExitBaseline);
		UTextBlock* ExitArrow = B.Text(FText::FromString(TEXT("↗")), Font(EFace::Bold, 22.f), Paper());
		Typeset(ExitArrow, ERef::Malgun, 29.f, ExitBaseline);
		AddH(ExitRow, ExitLabel, FMargin(0.f), VAlign_Top);
		AddH(ExitRow, ExitArrow, FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Top);
		AddV(Exit, ExitRow, FMargin(12.f, 10.f, 0.f, 10.f));
		AddV(Exit, Rule(B, Paper()));
		AddFill(ExitCard, Exit);
		ExitButton.Root = ExitCard;
		ExitButton.Ring = AddOutline(B, ExitCard, Lime(), 3.f, 4.f);
		AddH(Side, ExitCard, FMargin(0.f), VAlign_Center);
		AddHitTarget(ExitCard, [this]() { ReturnToHub(); });
	}
	AddH(Head, Side, FMargin(0.f), VAlign_Top);
	return Head;
}

UWidget* USupplyShopPage::BuildCatalog(const FBuilder& B)
{
	UVerticalBox* Catalog = B.VBox();

	// .shop-title: 900 italic clamp(46px,6vw,88px)/.95, margin 4 0 18
	UTextBlock* Title = B.Text(LOCTEXT("NextDive", "NEXT DIVE."), Font(EFace::Display, 88.f, -.065f, true), Paper());
	AddV(Catalog, B.LineBox(Title, 83.6f, ERef::ArialBlack), FMargin(0.f, 4.f, 0.f, 18.f));

	// .shop-filters: gap 4, padding-bottom 12, border-bottom 1px line
	UHorizontalBox* FilterRow = B.HBox();
	const FText FilterLabels[3] = { LOCTEXT("All", "전체"), LOCTEXT("Permanent", "장비"), LOCTEXT("Consumable", "소모품") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FFilterWidgets& Filter = Filters[Index];
		UOverlay* Card = B.Overlay();
		Card->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Filter.Card = Card;
		UBorder* Face = B.Fill(FLinearColor::Transparent, FMargin(15.f, 9.f));
		Filter.Face = Face;
		Filter.Label = B.LineBox(B.Text(FilterLabels[Index], Font(EFace::Bold, 13.f), Muted()));
		Face->SetContent(Filter.Label);
		AddFill(Card, Face);
		Filter.Ring = AddOutline(B, Card, Lime(), 3.f, 4.f);
		AddH(FilterRow, Card, FMargin(0.f, 0.f, Index < 2 ? 4.f : 0.f, 0.f), VAlign_Top);
		AddHitTarget(Card, [this, Index]()
		{
			SetCategory(Index, true);
			SetArea(EArea::Category);
		});
	}
	AddV(Catalog, FilterRow, FMargin(0.f, 0.f, 0.f, 12.f));
	AddV(Catalog, Rule(B, Line()));

	// .column-head: padding 15 17 9 30, 10px .08em
	UHorizontalBox* ColumnHead = B.HBox();
	AddH(ColumnHead, B.LineBox(B.Text(LOCTEXT("CatalogHead", "보급 카탈로그"), Font(EFace::Regular, 10.f, .08f), Muted())), FMargin(0.f), VAlign_Top, 1.f);
	AddH(ColumnHead, B.LineBox(B.Text(LOCTEXT("PriceHead", "단가"), Font(EFace::Regular, 10.f, .08f), Muted())), FMargin(0.f), VAlign_Top);
	AddV(Catalog, ColumnHead, FMargin(30.f, 15.f, 17.f, 9.f));

	// .shop-rows: max-height 43vh, padding 8 12 12 17, 얇은 종이색 스크롤바
	RowsFrame = B.Size();
	RowsScroll = B.Tree->ConstructWidget<UScrollBox>();
	RowsScroll->SetScrollbarThickness(FVector2D(4.f, 4.f));
	RowsScroll->SetScrollbarPadding(FMargin(0.f));
	RowsScroll->SetAnimateWheelScrolling(false);
	{
		FScrollBarStyle BarStyle = RowsScroll->GetWidgetBarStyle();
		FSlateBrush Thumb;
		Thumb.DrawAs = ESlateBrushDrawType::Box;
		Thumb.TintColor = FSlateColor(Paper());
		Thumb.ImageSize = FVector2D(4.f, 4.f);
		// 트랙 폭은 위/아래 슬롯 이미지 크기(기본 32)가 정한다 — 썸이 그 폭으로 늘어나므로 0으로
		FSlateBrush Clear;
		Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
		Clear.ImageSize = FVector2D::ZeroVector;
		BarStyle.SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDraggedThumbImage(Thumb);
		BarStyle.SetVerticalBackgroundImage(Clear).SetVerticalTopSlotImage(Clear).SetVerticalBottomSlotImage(Clear);
		BarStyle.SetThickness(4.f);
		RowsScroll->SetWidgetBarStyle(BarStyle);
	}
	UBorder* RowsPad = B.Box(FMargin(17.f, 8.f, 12.f, 12.f));
	RowsList = B.VBox();
	RowsPad->SetContent(RowsList);
	RowsScroll->AddChild(RowsPad);
	RowsFrame->AddChild(RowsScroll);
	AddV(Catalog, RowsFrame);

	// .shop-empty: padding 38 15
	UVerticalBox* Empty = B.VBox();
	AddV(Empty, B.LineBox(B.Text(LOCTEXT("EmptyTitle", "이 분류에는 상품이 없습니다"), Font(EFace::Bold, 24.f), Paper())), FMargin(0.f, 0.f, 0.f, 8.f));
	AddV(Empty, B.LineBox(B.Text(LOCTEXT("EmptyBody", "다른 분류로 이동하거나 메인 분기로 돌아갈 수 있습니다."), Font(EFace::Regular, 15.f), Muted()), 15.f * 1.7f));
	UBorder* EmptyPad = B.Box(FMargin(15.f, 38.f));
	EmptyPad->SetContent(Empty);
	EmptyBox = EmptyPad;
	EmptyBox->SetVisibility(ESlateVisibility::Collapsed);
	AddV(Catalog, EmptyPad);

	// .shop-extra
	AddV(Catalog, B.LineBox(B.Text(LOCTEXT("Extra", "구매품은 팀 보관함에 추가됩니다. 장비 꺼내기는 탐사 아이템 박스에서."), Font(EFace::Regular, 10.f), Muted())), FMargin(0.f, 12.f, 0.f, 0.f));
	return Catalog;
}

UWidget* USupplyShopPage::BuildRow(const FBuilder& B, const FItemShopProduct& Product)
{
	FRowWidgets& Row = Rows.AddDefaulted_GetRef();
	Row.ProductId = Product.ProductId;

	// .shop-row: min-height 81, grid 25px/1fr/auto gap 12, padding 12 15, border-bottom 1px
	USizeBox* Root = B.Size();
	Root->SetMinDesiredHeight(81.f);
	Row.Root = Root;
	UOverlay* Card = B.Overlay();
	Root->AddChild(Card);

	// :before inset 3px 4px, skewX(-5deg) — 선택 면 / 포커스 inset 3px lime
	UBorder* Face = B.Fill(FLinearColor::Transparent);
	Face->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Face->SetRenderShear(FVector2D(-5.f, 0.f));
	Row.Face = Face;
	AddOverlay(Card, Face, HAlign_Fill, VAlign_Fill, FMargin(4.f, 3.f));
	UWidget* Ring = B.Ring(Lime(), 3.f);
	Ring->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Ring->SetRenderShear(FVector2D(-5.f, 0.f));
	Ring->SetVisibility(ESlateVisibility::Hidden);
	Row.Ring = Ring;
	AddOverlay(Card, Ring, HAlign_Fill, VAlign_Fill, FMargin(4.f, 3.f));

	UHorizontalBox* Content = B.HBox();
	USizeBox* IndexCell = B.Size(25.f);
	Row.Index = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 15.f), Paper()));
	SetContent(IndexCell, Row.Index, HAlign_Left, VAlign_Center);
	AddH(Content, IndexCell, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);

	UVerticalBox* Words = B.VBox();
	Row.Name = B.LineBox(B.Text(GetProductName(Product), Font(EFace::Bold, 20.f), Paper()));
	Row.Kind = B.LineBox(B.Text(GetLifetimeShort(Product), Font(EFace::Regular, 10.f), Paper()));
	Row.Kind->SetRenderOpacity(.8f);
	AddV(Words, Row.Name);
	AddV(Words, Row.Kind, FMargin(0.f, 5.f, 0.f, 0.f));
	AddH(Content, Words, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center, 1.f);

	UVerticalBox* PriceColumn = B.VBox();
	Row.Price = B.LineBox(B.Text(Money(Product.PurchasePrice), Font(EFace::Black, 25.f, 0.f, true), Paper()), -1.f, ERef::Segoe);
	Row.Stock = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 10.f), Paper()));
	Row.Stock->SetRenderOpacity(.8f);
	AddV(PriceColumn, Row.Price, FMargin(0.f), HAlign_Right);
	AddV(PriceColumn, Row.Stock, FMargin(0.f, 5.f, 0.f, 0.f), HAlign_Right);
	AddH(Content, PriceColumn, FMargin(0.f), VAlign_Center);

	AddOverlay(Card, Content, HAlign_Fill, VAlign_Center, FMargin(15.f, 12.f, 15.f, 13.f));
	AddOverlay(Card, Rule(B, Line()), HAlign_Fill, VAlign_Bottom);

	// :focus:after ▶ left -16px
	UTextBlock* CursorMark = B.LineBox(B.Text(FText::FromString(TEXT("▶")), Font(EFace::Regular, 15.f), Lime()));
	CursorMark->SetVisibility(ESlateVisibility::Hidden);
	Row.Cursor = CursorMark;
	UBorder* CursorHolder = B.Box();
	CursorHolder->SetContent(CursorMark);
	AddOverlay(Card, CursorHolder, HAlign_Left, VAlign_Center, FMargin(-16.f, 0.f, 0.f, 0.f));

	const FName ProductId = Product.ProductId;
	AddHitTarget(Root, [this, ProductId]()
	{
		Select(ProductId, false);
		SetArea(EArea::Quantity);
	});
	return Root;
}

UWidget* USupplyShopPage::BuildPanel(const FBuilder& B)
{
	UVerticalBox* Panel = B.VBox();

	// .shop-art: 150px, 코발트 + 2px #7185f5, 종이색 띠(130%×30, bottom 10, -14°), 도해 .75, 라벨
	USizeBox* ArtBox = B.Size(-1.f, 150.f);
	UBorder* ArtFrame = B.Fill(Hex(0x7185f5), FMargin(2.f));
	ArtFrame->SetClipping(EWidgetClipping::ClipToBounds);
	ArtBox->AddChild(ArtFrame);
	UOverlay* ArtInner = B.Overlay();
	ArtFrame->SetContent(ArtInner);
	AddFill(ArtInner, B.Fill(Blue()));

	UCanvasPanel* BandCanvas = B.Canvas();
	UBorder* Band = B.Fill(Paper());
	Band->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Band->SetRenderTransformAngle(-14.f);
	UCanvasPanelSlot* BandSlot = BandCanvas->AddChildToCanvas(Band);
	BandSlot->SetAnchors(FAnchors(-.15f, 1.f, 1.15f, 1.f));
	BandSlot->SetOffsets(FMargin(0.f, -40.f, 0.f, 30.f));
	AddFill(ArtInner, BandCanvas);

	Art = B.Tree->ConstructWidget<USupplyEquipmentArt>(USupplyEquipmentArt::StaticClass());
	Art->SetArtScale(.75f);
	AddOverlay(ArtInner, Art, HAlign_Center, VAlign_Center);

	UBorder* ArtLabel = B.Fill(Ink(), FMargin(6.f, 3.f));
	ArtLabel->SetContent(B.LineBox(B.Text(LOCTEXT("ArtLabel", "SUPPLY / EQUIPMENT STUDY"), Font(EFace::Bold, 10.f, .18f), Paper())));
	AddOverlay(ArtInner, ArtLabel, HAlign_Left, VAlign_Bottom, FMargin(18.f, 0.f, 0.f, 19.f));
	AddV(Panel, ArtBox);

	// .shop-details: padding 16 0
	UVerticalBox* Copy = B.VBox();
	DetailCopy = Copy;
	KindText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 11.f, .1f), Lime()));
	NameText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 30.f), Paper()), 37.5f);
	NameText->SetAutoWrapText(true);
	DescriptionText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Hex(0xcad4e5)), 19.2f);
	DescriptionText->SetAutoWrapText(true);
	AddV(Copy, KindText);
	AddV(Copy, NameText, FMargin(0.f, 6.f));
	AddV(Copy, DescriptionText);
	AddV(Panel, Copy, FMargin(0.f, 16.f));

	// .shop-stock: gap 22, 11px, padding 12 0, border-bottom — b 17px lime(ml 5)가 같은 기준선
	UHorizontalBox* Stock = B.HBox();
	const float StockBaseline = CssBaseline(17.f, ERef::Malgun, 23.f);
	auto AddStock = [&](const FText& Label, TObjectPtr<UTextBlock>& OutValue, float RightGap)
	{
		UTextBlock* LabelText = B.Text(Label, Font(EFace::Regular, 11.f), Paper());
		Typeset(LabelText, ERef::Malgun, 23.f, StockBaseline);
		OutValue = B.Text(Dash(), Font(EFace::Bold, 17.f), Lime());
		Typeset(OutValue, ERef::Malgun, 23.f, StockBaseline);
		AddH(Stock, LabelText, FMargin(0.f), VAlign_Top);
		AddH(Stock, OutValue, FMargin(5.f, 0.f, RightGap, 0.f), VAlign_Top);
	};
	AddStock(LOCTEXT("Owned", "팀 보유"), OwnedText, 22.f);
	AddStock(LOCTEXT("Stored", "보관 중"), StoredText, 0.f);
	AddV(Panel, Stock, FMargin(0.f, 12.f));
	AddV(Panel, Rule(B, Line()));

	// .shop-order: mt 16, padding 18, 종이색, box-shadow 7px 7px 0 blue
	UOverlay* Order = B.Overlay();
	UBorder* OrderShadow = B.Fill(Blue());
	OrderShadow->SetRenderTranslation(FVector2D(7.f, 7.f));
	AddFill(Order, OrderShadow);
	UBorder* Sheet = B.Fill(Paper(), FMargin(18.f));
	AddFill(Order, Sheet);
	UVerticalBox* SheetColumn = B.VBox();
	Sheet->SetContent(SheetColumn);
	AddV(Panel, Order, FMargin(0.f, 16.f, 0.f, 0.f));

	AddV(SheetColumn, B.LineBox(B.Text(LOCTEXT("OrderMicro", "ORDER / 팀 공유 보관함으로"), Font(EFace::Bold, 10.f, .18f), Hex(0x3b5056)), 15.f, ERef::Segoe));

	// .quantity-picker: margin 12 0, gap 12 — 라벨(margin-right auto) − [수량] +
	UHorizontalBox* Picker = B.HBox();
	AddH(Picker, B.LineBox(B.Text(LOCTEXT("QuantityLabel", "주문 수량"), Font(EFace::Regular, 12.f), Ink())), FMargin(0.f), VAlign_Center, 1.f);
	auto MakeStepper = [&](FButtonWidgets& Button, const TCHAR* Glyph, int32 Delta)
	{
		USizeBox* Size = B.Size(36.f, 36.f);
		UOverlay* Card = B.Overlay();
		Size->AddChild(Card);
		AddFill(Card, B.Ring(Hex(0x657477), 1.f));
		UTextBlock* Text = B.LineBox(B.Text(FText::FromString(Glyph), Font(EFace::Regular, 22.f), Ink()));
		AddOverlay(Card, Text, HAlign_Center, VAlign_Center);
		Button.Root = Size;
		Button.Texts = { Text };
		Button.Ring = AddOutline(B, Card, Blue(), 3.f, 4.f);
		AddHitTarget(Size, [this, Delta]()
		{
			ChangeQuantity(Delta);
			SetArea(EArea::Quantity);
		});
		return Size;
	};
	AddH(Picker, MakeStepper(MinusButton, TEXT("−"), -1), FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);
	{
		// #shopQuantity: 44×40, border 4 transparent → 포커스면 ink 테두리 + 4px 코발트 그림자 + 라임 바탕
		USizeBox* Size = B.Size(44.f, 40.f);
		UOverlay* Card = B.Overlay();
		Size->AddChild(Card);
		UBorder* Shadow = B.Fill(Blue());
		Shadow->SetRenderTranslation(FVector2D(4.f, 4.f));
		QuantityShadow = Shadow;
		AddFill(Card, Shadow);
		QuantityFace = B.Fill(FLinearColor::Transparent);
		AddFill(Card, QuantityFace);
		QuantityRing = B.Ring(Ink(), 4.f);
		AddFill(Card, QuantityRing);
		QuantityText = B.LineBox(B.Text(FText::AsNumber(1), Font(EFace::Bold, 24.f), Ink()));
		AddOverlay(Card, QuantityText, HAlign_Center, VAlign_Center);
		AddH(Picker, Size, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);
		AddHitTarget(Size, [this]() { SetArea(EArea::Quantity); });
	}
	AddH(Picker, MakeStepper(PlusButton, TEXT("+"), 1), FMargin(0.f), VAlign_Center);
	AddV(SheetColumn, Picker, FMargin(0.f, 12.f));

	// .order-total: padding 10 0, border-top 1px #718187
	AddV(SheetColumn, Rule(B, Hex(0x718187)));
	UHorizontalBox* Total = B.HBox();
	AddH(Total, B.LineBox(B.Text(LOCTEXT("TotalLabel", "주문 합계"), Font(EFace::Regular, 13.f), Ink())), FMargin(0.f), VAlign_Center, 1.f);
	TotalText = B.LineBox(B.Text(Dash(), Font(EFace::Black, 30.f, 0.f, true), Ink()), -1.f, ERef::Segoe);
	AddH(Total, TotalText, FMargin(0.f), VAlign_Center);
	AddV(SheetColumn, Total, FMargin(0.f, 10.f));

	// .shop-buy: padding 13 16, ink / 비활성 #46515b
	{
		UOverlay* Card = B.Overlay();
		UBorder* Face = B.Fill(Ink(), FMargin(16.f, 13.f));
		UHorizontalBox* Row = B.HBox();
		UTextBlock* Label = B.LineBox(B.Text(LOCTEXT("Buy", "주문 확인"), Font(EFace::Bold, 15.f), Paper()));
		UTextBlock* Arrow = B.LineBox(B.Text(FText::FromString(TEXT("↗")), Font(EFace::Bold, 15.f), Paper()));
		AddH(Row, Label, FMargin(0.f), VAlign_Center, 1.f);
		AddH(Row, Arrow, FMargin(0.f), VAlign_Center);
		Face->SetContent(Row);
		AddFill(Card, Face);
		BuyButton.Root = Card;
		BuyButton.Face = Face;
		BuyButton.Texts = { Label, Arrow };
		BuyButton.Ring = AddOutline(B, Card, Blue(), 3.f, 4.f);
		AddV(SheetColumn, Card);
		AddHitTarget(Card, [this]()
		{
			SetArea(EArea::Buy);
			OpenConfirm();
		});
	}

	ReasonText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 11.f), Ink()), 16.5f);
	ReasonText->SetAutoWrapText(true);
	AddV(SheetColumn, ReasonText, FMargin(0.f, 10.f, 0.f, 0.f));
	return Panel;
}

UWidget* USupplyShopPage::BuildFooter(const FBuilder& B)
{
	// .shop-footer: margin-top auto, border-top 1px, padding-top 12 — 상태(12px/1.5) + 안내(11px, mt 8)
	UVerticalBox* Foot = B.VBox();
	AddV(Foot, Rule(B, Line()));
	UVerticalBox* Texts = B.VBox();
	StatusText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Paper()), 18.f);
	AddV(Texts, StatusText);
	HintText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 11.f), Muted()));
	AddV(Texts, HintText, FMargin(0.f, 8.f, 0.f, 0.f));
	AddV(Foot, Texts, FMargin(0.f, 12.f, 0.f, 0.f));
	return Foot;
}

UWidget* USupplyShopPage::BuildConfirm(const FBuilder& B)
{
	// .layer: 화면 전체 #030817d9 — 배경 입력 차단(층 1)
	UOverlay* Layer = B.Overlay();
	AddFill(Layer, B.Fill(Hex(0x030817, 0xd9 / 255.f)));

	// .dialog#shopConfirm: 540, 종이색, border 4 ink(위 8 blue), box-shadow 12px 12px 0 blue, padding 28
	USizeBox* Width = B.Size(540.f);
	AddOverlay(Layer, Width, HAlign_Center, VAlign_Center);
	UOverlay* Card = B.Overlay();
	Card->SetRenderTransformPivot(FVector2D(1.f, 1.f));
	Ticket = Card;
	Width->AddChild(Card);

	UBorder* Shadow = B.Fill(Blue());
	Shadow->SetRenderTranslation(FVector2D(12.f, 12.f));
	AddFill(Card, Shadow);
	UBorder* Frame = B.Fill(Ink(), FMargin(4.f, 8.f, 4.f, 4.f));
	AddFill(Card, Frame);
	UOverlay* FrameInner = B.Overlay();
	Frame->SetContent(FrameInner);
	AddFill(FrameInner, B.Fill(Paper()));
	// 위 테두리(8px)는 코발트
	USizeBox* TopSize = B.Size(-1.f, 8.f);
	TopSize->AddChild(B.Fill(Blue()));
	AddOverlay(Card, TopSize, HAlign_Fill, VAlign_Top);
	// :before 라임 탭 66×8 (left 22 = 패딩 상자 기준 → 바깥 기준 26)
	USizeBox* Tab = B.Size(66.f, 8.f);
	Tab->AddChild(B.Fill(Lime()));
	AddOverlay(Card, Tab, HAlign_Left, VAlign_Top, FMargin(26.f, 0.f, 0.f, 0.f));

	UVerticalBox* Column = B.VBox();
	AddOverlay(FrameInner, Column, HAlign_Fill, VAlign_Fill, FMargin(28.f));

	// .dialog p 규칙이 .micro 크기를 덮어 14px/1.7로 그려진다(시안 그대로)
	AddV(Column, B.LineBox(B.Text(LOCTEXT("ConfirmMicro", "SUPPLY ORDER / FINAL CHECK"), Font(EFace::Bold, 14.f, .18f), Blue()), 23.8f, ERef::Segoe));
	AddV(Column, B.LineBox(B.Text(LOCTEXT("ConfirmTitle", "팀 장비를 보급할까요?"), Font(EFace::Bold, 40.f, -.05f), Ink()), 48.f), FMargin(0.f, 11.f));
	AddV(Column, B.LineBox(B.Text(LOCTEXT("ConfirmBody", "구매품은 잠수정 공유 보관함에 추가됩니다."), Font(EFace::Regular, 14.f), Ink()), 23.8f));

	// .shop-confirm-values: grid 1fr auto, gap 10, border-block 1px #7e8988, padding 15 0, margin 18 0
	UVerticalBox* Values = B.VBox();
	AddV(Values, Rule(B, Hex(0x7e8988)));
	auto AddValue = [&](const FText& Label, TObjectPtr<UTextBlock>* OutLabel, TObjectPtr<UTextBlock>& OutValue, bool bMinor, float TopGap)
	{
		UHorizontalBox* ValueLine = B.HBox();
		const float RowHeight = bMinor ? 16.f : 28.f;
		const float ValuePx = bMinor ? 12.f : 21.f;
		const float Baseline = CssBaseline(ValuePx, ERef::Malgun, RowHeight);
		UTextBlock* LabelText = B.Text(Label, Font(EFace::Regular, bMinor ? 12.f : 14.f), Ink());
		Typeset(LabelText, ERef::Malgun, RowHeight, Baseline);
		if (OutLabel)
		{
			*OutLabel = LabelText;
		}
		OutValue = B.Text(FText::GetEmpty(), Font(EFace::Bold, ValuePx), Ink());
		Typeset(OutValue, ERef::Malgun, RowHeight, Baseline);
		AddH(ValueLine, LabelText, FMargin(0.f, 0.f, 10.f, 0.f), VAlign_Top, 1.f);
		AddH(ValueLine, OutValue, FMargin(0.f), VAlign_Top);
		AddV(Values, ValueLine, FMargin(0.f, TopGap, 0.f, 0.f));
	};
	AddValue(FText::GetEmpty(), &ConfirmNameText, ConfirmQuantityText, false, 15.f);
	AddValue(LOCTEXT("ConfirmTotal", "주문 합계"), nullptr, ConfirmTotalText, false, 10.f);
	AddValue(LOCTEXT("ConfirmAfter", "구매 후 예상 팀 자금"), nullptr, ConfirmAfterText, true, 10.f);
	AddV(Values, B.Spacer(0.f, 15.f));
	AddV(Values, Rule(B, Hex(0x7e8988)));
	AddV(Column, Values, FMargin(0.f, 18.f));

	ConfirmReasonText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Hex(0x6e3019)), 20.4f);
	ConfirmReasonText->SetAutoWrapText(true);
	AddV(Column, ConfirmReasonText);

	// .dialog-actions: gap 10, mt 20, 버튼 padding 14 / border 2 ink / min-height 52
	UHorizontalBox* Actions = B.HBox();
	auto MakeAction = [&](FButtonWidgets& Button, const FText& Label, bool bCommit, int32 Index)
	{
		USizeBox* Size = B.Size(-1.f, 52.f);
		UOverlay* Card = B.Overlay();
		Size->AddChild(Card);
		UBorder* Face = B.Fill(bCommit ? Ink() : FLinearColor::Transparent);
		AddFill(Card, Face);
		AddFill(Card, B.Ring(Ink(), 2.f));
		UTextBlock* Text = B.LineBox(B.Text(Label, Font(EFace::Bold, 15.f), bCommit ? Paper() : Ink()));
		AddOverlay(Card, Text, HAlign_Center, VAlign_Center);
		Button.Root = Size;
		Button.Face = Face;
		Button.Texts = { Text };
		Button.Ring = AddOutline(B, Card, Blue(), 3.f, 3.f);
		AddH(Actions, Size, FMargin(Index == 0 ? 0.f : 10.f, 0.f, 0.f, 0.f), VAlign_Fill, 1.f);
		AddHitTarget(Size, [this, Index]()
		{
			ConfirmFocus = Index;
			if (Index == 0)
			{
				CancelConfirm();
			}
			else
			{
				CommitConfirm();
			}
		}, ShopConfirmLayerIndex);
	};
	MakeAction(CancelButton, LOCTEXT("Cancel", "취소"), false, 0);
	MakeAction(CommitButton, LOCTEXT("Commit", "구매 요청"), true, 1);
	AddV(Column, Actions, FMargin(0.f, 20.f, 0.f, 0.f));
	return Layer;
}

void USupplyShopPage::BuildRows()
{
	if (!RowsList || !WidgetTree)
	{
		return;
	}
	RowsList->ClearChildren();
	Rows.Reset();
	const FBuilder B(WidgetTree);
	for (const FItemShopProduct& Product : Products)
	{
		AddV(RowsList, BuildRow(B, Product));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// 데이터

const FItemShopProduct* USupplyShopPage::FindProduct(FName ProductId) const
{
	return Products.FindByPredicate([ProductId](const FItemShopProduct& Product) { return Product.ProductId == ProductId; });
}

TArray<const FItemShopProduct*> USupplyShopPage::GetVisibleProducts() const
{
	TArray<const FItemShopProduct*> Result;
	for (const FItemShopProduct& Product : Products)
	{
		if (IsCategoryMatch(Category, Product))
		{
			Result.Add(&Product);
		}
	}
	return Result;
}

FText USupplyShopPage::GetProductName(const FItemShopProduct& Product) const
{
	if (!Product.ShopDisplayName.IsEmpty())
	{
		return Product.ShopDisplayName;
	}
	if (Product.ItemData && !Product.ItemData->DisplayName.IsEmpty())
	{
		return Product.ItemData->DisplayName;
	}
	return FText::FromName(Product.ProductId);
}

FText USupplyShopPage::GetLifetimeShort(const FItemShopProduct& Product) const
{
	return Product.Lifetime == EItemShopItemLifetime::Permanent ? LOCTEXT("KindPermanent", "장비") : LOCTEXT("KindConsumable", "소모품");
}

int32 USupplyShopPage::GetOwned(FName ProductId) const
{
	if (const AGoHomeGameState* GameState = BoundGameState.Get())
	{
		for (const FSharedLockerViewEntry& Entry : GameState->GetSharedLockerItems())
		{
			if (Entry.ProductId == ProductId)
			{
				return Entry.OwnedQuantity;
			}
		}
	}
	return 0;
}

int32 USupplyShopPage::GetStored(FName ProductId) const
{
	if (const AGoHomeGameState* GameState = BoundGameState.Get())
	{
		for (const FSharedLockerViewEntry& Entry : GameState->GetSharedLockerItems())
		{
			if (Entry.ProductId == ProductId)
			{
				return Entry.StoredQuantity;
			}
		}
	}
	return 0;
}

int32 USupplyShopPage::GetRoundPurchased(FName ProductId) const
{
	const AGoHomeGameState* GameState = BoundGameState.Get();
	return GameState ? GameState->GetRoundShopPurchaseQuantity(ProductId) : 0;
}

int32 USupplyShopPage::GetFunds() const
{
	const AGoHomeGameState* GameState = BoundGameState.Get();
	return GameState ? GameState->GetCurrentFunds() : 0;
}

bool USupplyShopPage::IsPending() const
{
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	return Backend && Backend->IsShopPurchasePending();
}

FText USupplyShopPage::GetBlockReason(FName ProductId, int32 InQuantity) const
{
	const FItemShopProduct* Product = FindProduct(ProductId);
	if (!BoundGameState.IsValid())
	{
		return LOCTEXT("ReasonLoading", "보급 정보를 불러오는 중입니다.");
	}
	if (IsPending())
	{
		return LOCTEXT("ReasonPending", "이전 요청 결과를 기다리는 중입니다.");
	}
	if (!Product)
	{
		return LOCTEXT("ReasonNone", "구매할 상품을 선택하세요.");
	}
	if (!Product->bCanBuyInLobby)
	{
		return LOCTEXT("ReasonLocked", "이 상품은 현재 로비에서 구매할 수 없습니다.");
	}
	if (InQuantity < 1 || InQuantity > FMath::Max(1, Product->MaxQuantityPerOrder))
	{
		return LOCTEXT("ReasonQuantity", "주문 수량 한도를 확인하세요.");
	}
	if (Product->MaxPurchasesPerRun > 0 && GetRoundPurchased(ProductId) + InQuantity > Product->MaxPurchasesPerRun)
	{
		return LOCTEXT("ReasonRound", "이번 라운드 구매 한도를 초과합니다.");
	}
	if (Product->MaxOwnedQuantity > 0 && GetOwned(ProductId) + InQuantity > Product->MaxOwnedQuantity)
	{
		return LOCTEXT("ReasonOwned", "팀 보유 상한을 초과합니다.");
	}
	if (GetFunds() < Product->PurchasePrice * InQuantity)
	{
		return LOCTEXT("ReasonFunds", "팀 자금이 부족합니다. 수량을 줄이거나 다른 상품을 선택하세요.");
	}
	return FText::GetEmpty();
}

// ---------------------------------------------------------------------------------------------------------------------
// 수명·구독

void USupplyShopPage::OnPageShown(bool bBack, FName FocusHint)
{
	// 시안 openShop: 매번 전체 분류·첫 상품·수량 1·목록 포커스로 시작
	Products.Reset();
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	if (const UItemShopCatalogDataAsset* Catalog = Backend ? Backend->GetShopCatalog() : nullptr)
	{
		Products = Catalog->Products;
	}
	BuildRows();

	BindSources();
	LastFunds = BoundGameState.IsValid() ? GetFunds() : INDEX_NONE;

	bConfirmOpen = false;
	HitLayer = 0;
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	CategoryMemory.Reset();
	SetCategory(0, false);
	Area = SelectedId != NAME_None ? EArea::List : EArea::Category;

	SetStatus(!BoundGameState.IsValid() ? LOCTEXT("StatusLoading", "보급 정보를 불러오는 중입니다.")
		: IsPending() ? LOCTEXT("StatusPending", "이전 요청의 결과를 기다리는 중입니다.")
		: LOCTEXT("StatusReady", "다음 잠수에 필요한 보급을 준비하세요."), false);
	RefreshAll();
}

void USupplyShopPage::OnPageHidden()
{
	UnbindSources();
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		for (const FName& Track : { ShopListTrack, ShopDetailTrack, ShopArtTrack, ShopCursorTrack, ShopWalletTrack, ShopTicketTrack })
		{
			Animator->Finish(Track);
		}
	}
}

void USupplyShopPage::NativeDestruct()
{
	UnbindSources();
	Super::NativeDestruct();
}

void USupplyShopPage::BindSources()
{
	UnbindSources();
	if (AGoHomeGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AGoHomeGameState>() : nullptr)
	{
		BoundGameState = GameState;
		GameState->OnExpeditionProgressChanged.AddUniqueDynamic(this, &USupplyShopPage::HandleProgressChanged);
		GameState->OnSharedLockerChanged.AddUniqueDynamic(this, &USupplyShopPage::HandleLockerChanged);
	}
	USupplyScreenWidget* Screen = GetHost();
	if (ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr)
	{
		PurchaseResultHandle = Backend->OnSupplyPurchaseResult().AddUObject(this, &USupplyShopPage::HandlePurchaseResult);
	}
}

void USupplyShopPage::UnbindSources()
{
	if (AGoHomeGameState* GameState = BoundGameState.Get())
	{
		GameState->OnExpeditionProgressChanged.RemoveDynamic(this, &USupplyShopPage::HandleProgressChanged);
		GameState->OnSharedLockerChanged.RemoveDynamic(this, &USupplyShopPage::HandleLockerChanged);
	}
	BoundGameState.Reset();

	USupplyScreenWidget* Screen = GetHost();
	if (ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr)
	{
		Backend->OnSupplyPurchaseResult().Remove(PurchaseResultHandle);
	}
	PurchaseResultHandle.Reset();
}

void USupplyShopPage::HandleProgressChanged()
{
	const int32 Funds = GetFunds();
	if (LastFunds != INDEX_NONE && Funds != LastFunds)
	{
		// .shop-wallet.changed: 라임 바탕/짙은 글자 → 기본, 400ms ease-out
		if (FSupplyAnimator* Animator = GetAnimator())
		{
			TWeakObjectPtr<USupplyShopPage> WeakThis = this;
			Animator->Play(ShopWalletTrack, 0.f, .4f, [WeakThis](float T)
			{
				USupplyShopPage* Page = WeakThis.Get();
				if (!Page || !Page->WalletFlash)
				{
					return;
				}
				const float E = FSupplyEase::EaseOut().Eval(T);
				FLinearColor Back = Lime();
				Back.A = 1.f - E;
				Cast<UBorder>(Page->WalletFlash)->SetBrushColor(Back);
				Page->FundsText->SetColorAndOpacity(FSlateColor(FMath::Lerp(Ink(), Paper(), E)));
				Page->FundsLabel->SetColorAndOpacity(FSlateColor(FMath::Lerp(Ink(), Lime(), E)));
			});
		}
	}
	LastFunds = Funds;
	RefreshAll();
}

void USupplyShopPage::HandleLockerChanged()
{
	RefreshAll();
}

void USupplyShopPage::HandlePurchaseResult(const FItemShopPurchaseResult& Result, FName ProductId, int32 InQuantity)
{
	if (Result.Result == EItemShopResult::Success)
	{
		const FItemShopProduct* Product = FindProduct(ProductId);
		const FText Name = Product ? GetProductName(*Product) : FText::FromName(ProductId);
		SetStatus(FText::Format(LOCTEXT("StatusSuccess", "{0} {1}개를 팀 보관함에 추가했습니다."), Name, FText::AsNumber(InQuantity)), false);
	}
	else
	{
		SetStatus(Result.Message.IsEmpty() ? LOCTEXT("StatusError", "구매를 처리하지 못했습니다. 다시 요청해 주세요.") : Result.Message, true);
	}
	// 결과는 포커스를 빼앗지 않는다 — 문구와 버튼 상태만
	RefreshAll();
}

// ---------------------------------------------------------------------------------------------------------------------
// 상태 전이

void USupplyShopPage::SetCategory(int32 InCategory, bool bAnimate)
{
	Category = FMath::Clamp(InCategory, 0, 2);
	const TArray<const FItemShopProduct*> Visible = GetVisibleProducts();
	const FName* Remembered = CategoryMemory.Find(Category);
	const FItemShopProduct* const* Kept = Remembered ? Visible.FindByPredicate([Remembered](const FItemShopProduct* Product) { return Product->ProductId == *Remembered; }) : nullptr;
	SelectedId = Kept ? (*Kept)->ProductId : (Visible.Num() > 0 ? Visible[0]->ProductId : NAME_None);
	Quantity = 1;

	RefreshAll();
	if (RowsScroll)
	{
		RowsScroll->SetScrollOffset(0.f);
	}

	FSupplyAnimator* Animator = GetAnimator();
	if (bAnimate && Animator && RowsFrame)
	{
		// .shop-rows.changing: Y 7→0, opacity .6→1, 140ms snap
		TWeakObjectPtr<UWidget> Target = RowsFrame.Get();
		Animator->Play(ShopListTrack, 0.f, .14f, [Target](float T)
		{
			if (UWidget* Widget = Target.Get())
			{
				const float E = FSupplyEase::Snap().Eval(T);
				Widget->SetRenderOpacity(FMath::Lerp(.6f, 1.f, E));
				Widget->SetRenderTranslation(FVector2D(0.f, 7.f * (1.f - E)));
			}
		});
	}
}

void USupplyShopPage::Select(FName ProductId, bool bFromNavigation)
{
	const bool bChanged = SelectedId != ProductId;
	SelectedId = ProductId;
	CategoryMemory.Add(Category, ProductId);
	if (bChanged)
	{
		Quantity = 1;
	}
	Area = EArea::List;
	RefreshAll();

	for (const FRowWidgets& Row : Rows)
	{
		if (Row.ProductId == ProductId && RowsScroll)
		{
			RowsScroll->ScrollWidgetIntoView(Row.Root, false, EDescendantScrollDestination::IntoView);
		}
	}

	FSupplyAnimator* Animator = GetAnimator();
	if (bChanged && Animator)
	{
		// .shop-panel.changing: 상세 X -8→0 / opacity .65→1 130ms, 도해 Y 14→0 / opacity .35→1 160ms
		TWeakObjectPtr<UWidget> Copy = DetailCopy.Get();
		Animator->Play(ShopDetailTrack, 0.f, .13f, [Copy](float T)
		{
			if (UWidget* Widget = Copy.Get())
			{
				const float E = FSupplyEase::Snap().Eval(T);
				Widget->SetRenderOpacity(FMath::Lerp(.65f, 1.f, E));
				Widget->SetRenderTranslation(FVector2D(-8.f * (1.f - E), 0.f));
			}
		});
		TWeakObjectPtr<UWidget> ArtMotion = Art ? Art->GetMotionTarget() : nullptr;
		Animator->Play(ShopArtTrack, 0.f, .16f, [ArtMotion](float T)
		{
			if (UWidget* Widget = ArtMotion.Get())
			{
				const float E = FSupplyEase::Snap().Eval(T);
				Widget->SetRenderOpacity(FMath::Lerp(.35f, 1.f, E));
				Widget->SetRenderTranslation(FVector2D(0.f, 14.f * (1.f - E)));
			}
		});
	}
}

void USupplyShopPage::SetArea(EArea InArea)
{
	if (SelectedId == NAME_None && InArea != EArea::Utility)
	{
		InArea = EArea::Category;
	}
	Area = InArea;
	RefreshFocus();
	if (Area == EArea::List)
	{
		// 커서 진입(▶ X -7→0, 100ms) — 행 이동마다 다시
		for (const FRowWidgets& Row : Rows)
		{
			if (Row.ProductId != SelectedId)
			{
				continue;
			}
			if (FSupplyAnimator* Animator = GetAnimator())
			{
				TWeakObjectPtr<UWidget> CursorMark = Row.Cursor.Get();
				Animator->Play(ShopCursorTrack, 0.f, .1f, [CursorMark](float T)
				{
					if (UWidget* Widget = CursorMark.Get())
					{
						Widget->SetRenderTranslation(FVector2D(-7.f * (1.f - FSupplyEase::Snap().Eval(T)), 0.f));
					}
				});
			}
		}
	}
}

void USupplyShopPage::ChangeQuantity(int32 Delta)
{
	const FItemShopProduct* Product = FindProduct(SelectedId);
	if (!Product)
	{
		return;
	}
	Quantity = FMath::Clamp(Quantity + Delta, 1, FMath::Max(1, Product->MaxQuantityPerOrder));
	RefreshDetail();
}

void USupplyShopPage::OpenConfirm()
{
	const FText Reason = GetBlockReason(SelectedId, Quantity);
	if (!Reason.IsEmpty())
	{
		SetStatus(Reason, true);
		return;
	}

	bConfirmOpen = true;
	ConfirmId = SelectedId;
	ConfirmQuantity = Quantity;
	ConfirmFocus = 0;
	HitLayer = ShopConfirmLayerIndex;
	ConfirmLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshConfirm();
	RefreshFocus();

	// #shopConfirm.enter ticket-in 240ms: X 72→-2(78%)→0, Y 24→0, Angle -6→.6→0, 피벗 우하단
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		TWeakObjectPtr<UWidget> Target = Ticket.Get();
		Animator->Play(ShopTicketTrack, 0.f, .24f, [Target](float T)
		{
			UWidget* Widget = Target.Get();
			if (!Widget)
			{
				return;
			}
			static const FSupplyKey XKeys[] = { { 0.f, 72.f }, { .78f, -2.f }, { 1.f, 0.f } };
			static const FSupplyKey YKeys[] = { { 0.f, 24.f }, { .78f, 0.f }, { 1.f, 0.f } };
			static const FSupplyKey AngleKeys[] = { { 0.f, -6.f }, { .78f, .6f }, { 1.f, 0.f } };
			const FSupplyEase Ease = FSupplyEase::Page();
			FWidgetTransform Transform;
			Transform.Translation = FVector2D(SampleSupplyKeys(XKeys, T, Ease), SampleSupplyKeys(YKeys, T, Ease));
			Transform.Angle = SampleSupplyKeys(AngleKeys, T, Ease);
			Widget->SetRenderTransform(Transform);
		});
	}
}

void USupplyShopPage::CancelConfirm()
{
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		Animator->Finish(ShopTicketTrack);
	}
	bConfirmOpen = false;
	HitLayer = 0;
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	SetArea(EArea::Buy);
}

void USupplyShopPage::CommitConfirm()
{
	if (!bConfirmOpen)
	{
		return;
	}
	const FText Reason = GetBlockReason(ConfirmId, ConfirmQuantity);
	if (!Reason.IsEmpty())
	{
		SetStatus(Reason, true);
		RefreshConfirm();
		return;
	}

	const FName ProductId = ConfirmId;
	const int32 OrderQuantity = ConfirmQuantity;
	CancelConfirm();

	// 리슨 서버 호스트는 결과가 요청 호출 안에서 바로 돌아온다 — "요청 중" 문구를 먼저 쓰고, 결과가 그 위를 덮게 한다.
	SetStatus(LOCTEXT("StatusRequested", "구매 요청 중입니다. 다른 상품을 계속 살펴볼 수 있습니다."), false);
	SetArea(EArea::List);
	USupplyScreenWidget* Screen = GetHost();
	ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	if (!Backend || !Backend->RequestShopPurchase(ProductId, OrderQuantity))
	{
		SetStatus(LOCTEXT("ReasonPending", "이전 요청 결과를 기다리는 중입니다."), true);
	}
	RefreshAll();
}

void USupplyShopPage::ReturnToHub()
{
	if (USupplyScreenWidget* Screen = GetHost())
	{
		Screen->NavigateTo(ESupplyPage::Hub, true, USupplyHubPage::FocusShop);
	}
}

void USupplyShopPage::SetStatus(const FText& Message, bool bError)
{
	if (StatusText)
	{
		StatusText->SetText(Message);
		StatusText->SetColorAndOpacity(FSlateColor(bError ? Alert() : Paper()));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// 입력

void USupplyShopPage::HandleInput(ESupplyInput Input)
{
	if (bConfirmOpen)
	{
		switch (Input)
		{
		case ESupplyInput::Left:
		case ESupplyInput::Right:
		case ESupplyInput::TabNext:
		case ESupplyInput::TabPrev:
			ConfirmFocus = 1 - ConfirmFocus;
			RefreshFocus();
			break;
		case ESupplyInput::Accept:
			if (ConfirmFocus == 0)
			{
				CancelConfirm();
			}
			else
			{
				CommitConfirm();
			}
			break;
		default:
			break;
		}
		return;
	}

	if (Input == ESupplyInput::TabNext || Input == ESupplyInput::TabPrev)
	{
		// 선택 분류 → 선택 상품 → 수량 → 주문 확인 → 메인 분기
		TArray<EArea> Stops = { EArea::Category };
		if (SelectedId != NAME_None)
		{
			Stops.Append({ EArea::List, EArea::Quantity, EArea::Buy });
		}
		Stops.Add(EArea::Utility);
		const int32 Current = FMath::Max(0, Stops.IndexOfByKey(Area));
		const int32 Next = (Current + (Input == ESupplyInput::TabNext ? 1 : -1) + Stops.Num()) % Stops.Num();
		SetArea(Stops[Next]);
		return;
	}

	const TArray<const FItemShopProduct*> Visible = GetVisibleProducts();
	switch (Area)
	{
	case EArea::Category:
		if (Input == ESupplyInput::Left || Input == ESupplyInput::Right)
		{
			const int32 Next = FMath::Clamp(Category + (Input == ESupplyInput::Right ? 1 : -1), 0, 2);
			if (Next != Category)
			{
				SetCategory(Next, true);
			}
			SetArea(EArea::Category);
		}
		else if (Input == ESupplyInput::Down || Input == ESupplyInput::Accept)
		{
			SetArea(EArea::List);
		}
		break;
	case EArea::List:
		if (Input == ESupplyInput::Up || Input == ESupplyInput::Down)
		{
			const int32 Index = Visible.IndexOfByPredicate([this](const FItemShopProduct* Product) { return Product->ProductId == SelectedId; });
			const int32 Next = FMath::Clamp(Index + (Input == ESupplyInput::Down ? 1 : -1), 0, Visible.Num() - 1);
			if (Visible.IsValidIndex(Next))
			{
				Select(Visible[Next]->ProductId, true);
				SetArea(EArea::List);
			}
		}
		else if (Input == ESupplyInput::Right || Input == ESupplyInput::Accept)
		{
			SetArea(EArea::Quantity);
		}
		break;
	case EArea::Quantity:
		if (Input == ESupplyInput::Left || Input == ESupplyInput::Right)
		{
			ChangeQuantity(Input == ESupplyInput::Right ? 1 : -1);
		}
		else if (Input == ESupplyInput::Up)
		{
			SetArea(EArea::List);
		}
		else if (Input == ESupplyInput::Accept || Input == ESupplyInput::Down)
		{
			SetArea(EArea::Buy);
		}
		break;
	case EArea::Buy:
		if (Input == ESupplyInput::Accept)
		{
			OpenConfirm();
		}
		else if (Input == ESupplyInput::Up)
		{
			SetArea(EArea::Quantity);
		}
		break;
	case EArea::Utility:
		if (Input == ESupplyInput::Accept)
		{
			ReturnToHub();
		}
		break;
	}
}

void USupplyShopPage::HandleBack()
{
	// 확인 → 주문 확인 버튼 → 수량 → 목록 → 분류 → 메인 분기
	if (bConfirmOpen)
	{
		CancelConfirm();
		return;
	}
	switch (Area)
	{
	case EArea::Buy:
		SetArea(EArea::Quantity);
		break;
	case EArea::Quantity:
	case EArea::Utility:
		SetArea(EArea::List);
		break;
	case EArea::List:
		SetArea(EArea::Category);
		break;
	default:
		ReturnToHub();
		break;
	}
}

void USupplyShopPage::OnViewSizeChanged(const FVector2D& ViewSize)
{
	// max-width 1920 가운데 정렬
	if (UOverlaySlot* FrameSlot = PageFrame ? Cast<UOverlaySlot>(PageFrame->Slot) : nullptr)
	{
		const float Side = FMath::Max(0.f, (ViewSize.X - 1920.f) * .5f);
		FrameSlot->SetPadding(FMargin(Side, 0.f));
	}
	if (RowsFrame)
	{
		RowsFrame->SetMaxDesiredHeight(ViewSize.Y * .43f);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// 그리기

void USupplyShopPage::RefreshAll()
{
	RefreshFilters();
	RefreshRows();
	RefreshDetail();
	RefreshConfirm();
	RefreshFocus();
}

void USupplyShopPage::RefreshFilters()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FFilterWidgets& Filter = Filters[Index];
		const bool bPressed = Index == Category;
		Filter.Face->SetBrushColor(bPressed ? Paper() : FLinearColor::Transparent);
		Filter.Label->SetColorAndOpacity(FSlateColor(bPressed ? Ink() : Muted()));
		Filter.Card->SetRenderShear(FVector2D(bPressed ? -7.f : 0.f, 0.f));
	}
}

void USupplyShopPage::RefreshRows()
{
	int32 VisibleIndex = 0;
	for (FRowWidgets& Row : Rows)
	{
		const FItemShopProduct* Product = FindProduct(Row.ProductId);
		const bool bVisible = Product && IsCategoryMatch(Category, *Product);
		Row.Root->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (!bVisible)
		{
			continue;
		}
		++VisibleIndex;
		const bool bSelected = Row.ProductId == SelectedId;
		const FLinearColor TextColor = bSelected ? Ink() : Paper();
		Row.Face->SetBrushColor(bSelected ? Paper() : FLinearColor::Transparent);
		Row.Index->SetText(FText::FromString(FString::Printf(TEXT("%02d"), VisibleIndex)));
		Row.Stock->SetText(FText::Format(LOCTEXT("RowStock", "팀 보유 {0}"), FText::AsNumber(GetOwned(Row.ProductId))));
		for (UTextBlock* Text : { Row.Index.Get(), Row.Name.Get(), Row.Kind.Get(), Row.Price.Get(), Row.Stock.Get() })
		{
			Text->SetColorAndOpacity(FSlateColor(TextColor));
		}
	}
	const bool bEmpty = VisibleIndex == 0;
	RowsFrame->SetVisibility(bEmpty ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	EmptyBox->SetVisibility(bEmpty ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void USupplyShopPage::RefreshDetail()
{
	const bool bReady = BoundGameState.IsValid();
	const FItemShopProduct* Product = bReady ? FindProduct(SelectedId) : nullptr;

	FundsText->SetText(bReady ? Money(GetFunds()) : Dash());

	if (Product)
	{
		KindText->SetText(Product->Lifetime == EItemShopItemLifetime::Permanent ? LOCTEXT("KindLongPermanent", "PERMANENT / 장비") : LOCTEXT("KindLongConsumable", "CONSUMABLE / 소모품"));
		NameText->SetText(GetProductName(*Product));
		DescriptionText->SetText(Product->ShopDescription.IsEmpty() ? LOCTEXT("NoDescription", "상품 설명이 등록되지 않았습니다.") : Product->ShopDescription);
		Art->SetArt(Product->ShopArt);
		OwnedText->SetText(FText::AsNumber(GetOwned(SelectedId)));
		StoredText->SetText(FText::AsNumber(GetStored(SelectedId)));
		TotalText->SetText(Money(Product->PurchasePrice * Quantity));
	}
	else
	{
		KindText->SetText(LOCTEXT("KindCatalog", "SUPPLY CATALOG"));
		NameText->SetText(bReady ? LOCTEXT("NoSelection", "상품 선택") : LOCTEXT("Waiting", "수신 대기"));
		DescriptionText->SetText(LOCTEXT("PickHint", "목록에서 보급품을 선택하세요."));
		Art->SetArt(EItemShopArt::Unknown);
		OwnedText->SetText(Dash());
		StoredText->SetText(Dash());
		TotalText->SetText(Dash());
	}
	QuantityText->SetText(FText::AsNumber(Quantity));

	const FText Reason = GetBlockReason(SelectedId, Quantity);
	const bool bBlocked = !Reason.IsEmpty();
	BuyButton.Face->SetBrushColor(bBlocked ? Hex(0x46515b) : Ink());
	for (UTextBlock* Text : BuyButton.Texts)
	{
		Text->SetColorAndOpacity(FSlateColor(bBlocked ? Hex(0xd2d7db) : Paper()));
	}

	if (bBlocked || !Product)
	{
		ReasonText->SetText(Reason);
	}
	else if (Product->MaxPurchasesPerRun > 0)
	{
		ReasonText->SetText(FText::Format(LOCTEXT("ReasonDefault", "주문당 최대 {0}개 · 이번 라운드 남은 구매 {1}개"),
			FText::AsNumber(FMath::Max(1, Product->MaxQuantityPerOrder)),
			FText::AsNumber(FMath::Max(0, Product->MaxPurchasesPerRun - GetRoundPurchased(SelectedId)))));
	}
	else
	{
		ReasonText->SetText(FText::Format(LOCTEXT("ReasonDefaultUnlimited", "주문당 최대 {0}개 · 라운드 구매 제한 없음"),
			FText::AsNumber(FMath::Max(1, Product->MaxQuantityPerOrder))));
	}
}

void USupplyShopPage::RefreshConfirm()
{
	if (!bConfirmOpen)
	{
		return;
	}
	const FItemShopProduct* Product = FindProduct(ConfirmId);
	const int32 Total = Product ? Product->PurchasePrice * ConfirmQuantity : 0;
	const FText Reason = GetBlockReason(ConfirmId, ConfirmQuantity);

	ConfirmNameText->SetText(Product ? GetProductName(*Product) : LOCTEXT("Removed", "판매 목록에서 제외된 상품"));
	ConfirmQuantityText->SetText(FText::Format(LOCTEXT("TimesQuantity", "× {0}"), FText::AsNumber(ConfirmQuantity)));
	ConfirmTotalText->SetText(Product ? Money(Total) : Dash());
	ConfirmAfterText->SetText(Product && GetFunds() >= Total ? Money(GetFunds() - Total) : LOCTEXT("Insufficient", "자금 부족"));
	ConfirmReasonText->SetText(Reason.IsEmpty() ? LOCTEXT("ConfirmDefault", "공동 자금으로 구매합니다. 수량은 서버 응답 이후 반영됩니다.") : Reason);

	const bool bBlocked = !Reason.IsEmpty();
	CommitButton.Face->SetBrushColor(bBlocked ? Hex(0xc5c7bd) : Ink());
	for (UTextBlock* Text : CommitButton.Texts)
	{
		Text->SetColorAndOpacity(FSlateColor(bBlocked ? Hex(0x535951) : Paper()));
	}
}

void USupplyShopPage::SetButtonFocused(FButtonWidgets& Button, bool bFocused)
{
	ShowIf(Button.Ring, bFocused);
}

void USupplyShopPage::RefreshFocus()
{
	const bool bPage = !bConfirmOpen;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ShowIf(Filters[Index].Ring, bPage && Area == EArea::Category && Index == Category);
	}
	for (FRowWidgets& Row : Rows)
	{
		const bool bFocused = bPage && Area == EArea::List && Row.ProductId == SelectedId;
		ShowIf(Row.Ring, bFocused);
		ShowIf(Row.Cursor, bFocused);
	}

	const bool bQuantity = bPage && Area == EArea::Quantity;
	ShowIf(QuantityShadow, bQuantity);
	ShowIf(QuantityRing, bQuantity);
	QuantityFace->SetBrushColor(bQuantity ? Lime() : FLinearColor::Transparent);

	SetButtonFocused(MinusButton, false);
	SetButtonFocused(PlusButton, false);
	SetButtonFocused(BuyButton, bPage && Area == EArea::Buy);
	SetButtonFocused(ExitButton, bPage && Area == EArea::Utility);
	SetButtonFocused(CancelButton, bConfirmOpen && ConfirmFocus == 0);
	SetButtonFocused(CommitButton, bConfirmOpen && ConfirmFocus == 1);

	FText Hint;
	switch (Area)
	{
	case EArea::Category: Hint = LOCTEXT("HintCategory", "← → 분류　↓ / Enter 목록　Esc 메인 분기"); break;
	case EArea::List: Hint = LOCTEXT("HintList", "↑ ↓ 상품　→ / Enter 주문 수량　Esc 분류"); break;
	case EArea::Quantity: Hint = LOCTEXT("HintQuantity", "← → 수량　Enter 주문 확인으로　Esc 목록"); break;
	case EArea::Buy: Hint = LOCTEXT("HintBuy", "Enter 확인 열기　Esc 수량"); break;
	default: Hint = LOCTEXT("HintUtility", "Enter 실행　Esc 목록"); break;
	}
	HintText->SetText(Hint);
}

#undef LOCTEXT_NAMESPACE
