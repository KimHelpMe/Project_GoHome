#include "UI/Supply/SupplyLockerPage.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/GoHomeGameState.h"
#include "Core/SupplyScreenBackend.h"
#include "Interaction/InventoryComponent.h"
#include "Item/ItemDataAsset.h"
#include "Shop/ItemShopCatalogDataAsset.h"
#include "UI/Supply/SupplyEquipmentArt.h"
#include "UI/Supply/SupplyHubPage.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyScreenWidget.h"

#define LOCTEXT_NAMESPACE "SupplyLocker"

using namespace SupplyUI;

namespace
{
	const FName LockerListTrack(TEXT("Locker.List"));
	const FName LockerDetailTrack(TEXT("Locker.Detail"));
	const FName LockerArtTrack(TEXT("Locker.Art"));
	const FName LockerCursorTrack(TEXT("Locker.Cursor"));
	const FName LockerStoredTrack(TEXT("Locker.Stored"));
	const FName LockerConfirmTrack(TEXT("Locker.Confirm"));
	constexpr int32 LockerConfirmLayerIndex = 1;

	FText TwoDigits(int32 Value)
	{
		return FText::FromString(FString::Printf(TEXT("%02d"), Value));
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// 조립

UWidget* USupplyLockerPage::BuildPage(const FBuilder& B)
{
	// .screen: max-width 1920, padding clamp(18px,3.2vw,60px)=60, gap 25(≥1500px)
	// 압축(@media max-height 850): padding 20px 3vw, gap 12 — 이하 Css(기본, 압축) 쌍은 이 규칙이 ≥1500 규칙을 덮은 결과
	Rows.Reset();
	PagePadding = IsCompact() ? FMargin(GetViewSize().X * .03f, 20.f) : FMargin(60.f);
	const float Gap = Css(25.f, 12.f);

	UOverlay* Root = B.Overlay();
	AddFill(Root, B.Fill(Ink()));

	UOverlay* Frame = B.Overlay();
	Frame->SetClipping(EWidgetClipping::ClipToBounds);
	PageFrame = Frame;
	AddFill(Root, Frame);

	// :before 남청 띠(110vw × 36vh, top 24%, left -4%, -9°) / :after 세로 줄(left 3%, top 22%, 66%)
	UCanvasPanel* Decor = B.Canvas();
	Decor->SetVisibility(ESlateVisibility::HitTestInvisible);
	UBorder* Band = B.Fill(Navy());
	Band->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Band->SetRenderTransformAngle(-9.f);
	AddCanvas(Decor, Band, FVector2D(-76.8f, 259.2f), FVector2D(2112.f, 388.8f));
	BackBand = Band;
	UBorder* SideLine = B.Fill(Line());
	UCanvasPanelSlot* LineSlot = Decor->AddChildToCanvas(SideLine);
	LineSlot->SetAnchors(FAnchors(.03f, .22f, .03f, .88f));
	LineSlot->SetOffsets(FMargin(0.f, 0.f, 1.f, 0.f));
	AddFill(Frame, Decor);

	UVerticalBox* Column = B.VBox();
	AddOverlay(Frame, Column, HAlign_Fill, VAlign_Fill, PagePadding);
	PageColumn = Column;

	Masthead = BuildMasthead(B);
	AddV(Column, Masthead);
	CategoryBar = BuildCategories(B);
	AddV(Column, CategoryBar, FMargin(0.f, Gap, 0.f, 0.f));

	// .workspace: minmax(280px,.92fr) / clamp(24px,4vw,70px)=70 / minmax(360px,1.35fr), align-items start
	UHorizontalBox* Workspace = B.HBox();
	DetailColumn = BuildDetail(B);
	ManifestColumn = BuildManifest(B);
	AddH(Workspace, DetailColumn, FMargin(0.f), VAlign_Top, .92f);
	AddH(Workspace, B.Spacer(70.f, 0.f));
	AddH(Workspace, ManifestColumn, FMargin(0.f), VAlign_Top, 1.35f);
	AddV(Column, Workspace, FMargin(0.f, Gap), HAlign_Fill, true);

	Footer = BuildFooter(B);
	AddV(Column, Footer);

	ConfirmLayer = BuildConfirm(B);
	AddFill(Root, ConfirmLayer);
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	return Root;
}

UWidget* USupplyLockerPage::BuildMasthead(const FBuilder& B)
{
	UHorizontalBox* Head = B.HBox();

	UHorizontalBox* Heading = B.HBox();
	{
		UVerticalBox* Brand = B.VBox();
		// 압축: kicker 10, wordmark 34(line-height .95), slash 42, h1 36
		const float KickerPx = Css(11.f, 10.f);
		const float WordmarkPx = Css(48.f, 34.f);
		AddV(Brand, B.LineBox(B.Text(LOCTEXT("Kicker", "SUBMARINE / CREW EQUIPMENT"), Font(EFace::Bold, KickerPx, .18f), Lime()), KickerPx * 1.5f, ERef::Segoe), FMargin(0.f, 0.f, 0.f, 7.f));
		AddV(Brand, B.LineBox(B.Text(LOCTEXT("Wordmark", "GOHOME"), Font(EFace::Display, WordmarkPx, -.08f, true), Paper()), WordmarkPx * .95f, ERef::ArialBlack));
		AddH(Heading, Brand, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		USizeBox* Slash = B.Size(5.f, Css(55.f, 42.f));
		Slash->AddChild(B.Fill(Lime()));
		Slash->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Slash->SetRenderTransformAngle(15.f);
		AddH(Heading, Slash, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		// h1 clamp(28px,3.4vw,56px)=56, 950, -.07em, line-height 1.15, margin-top 4
		const float TitlePx = Css(56.f, 36.f);
		UTextBlock* Title = B.Text(LOCTEXT("Title", "팀 보관함"), Font(EFace::Bold, TitlePx, -.07f), Paper());
		AddH(Heading, B.LineBox(Title, TitlePx * 1.15f), FMargin(0.f, 4.f, 0.f, 0.f), VAlign_Center);
	}
	AddH(Head, Heading, FMargin(0.f), VAlign_Top);
	AddH(Head, B.Spacer(24.f, 0.f), FMargin(0.f), VAlign_Top, 1.f);

	UHorizontalBox* Side = B.HBox();
	{
		ViewContextText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 11.f, .04f), Lime()));
		AddH(Side, ViewContextText, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		// .exit "나가기 ↗"(시안 상점과 같은 규격)
		UOverlay* ExitCard = B.Overlay();
		UVerticalBox* Exit = B.VBox();
		UHorizontalBox* ExitRow = B.HBox();
		const float ExitBaseline = CssBaseline(22.f, ERef::Malgun, 29.f);
		UTextBlock* ExitLabel = B.Text(LOCTEXT("Exit", "나가기"), Font(EFace::Regular, 13.f), Paper());
		Typeset(ExitLabel, ERef::Malgun, 29.f, ExitBaseline);
		UTextBlock* ExitArrow = B.Text(FText::FromString(TEXT("↗")), Font(EFace::Bold, 22.f), Paper());
		Typeset(ExitArrow, ERef::Malgun, 29.f, ExitBaseline);
		AddH(ExitRow, ExitLabel, FMargin(0.f), VAlign_Top);
		AddH(ExitRow, ExitArrow, FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Top);
		AddV(Exit, ExitRow, FMargin(12.f, 10.f, 0.f, 10.f));
		AddV(Exit, Rule(B, Paper()));
		AddFill(ExitCard, Exit);
		UWidget* Ring = B.Ring(Lime(), 3.f);
		AddOverlay(ExitCard, Ring, HAlign_Fill, VAlign_Fill, FMargin(-7.f));
		Ring->SetVisibility(ESlateVisibility::Hidden);
		ExitRing = Ring;
		AddH(Side, ExitCard, FMargin(0.f), VAlign_Center);
		AddHitTarget(ExitCard, [this]() { ReturnFromLocker(); });
	}
	AddH(Head, Side, FMargin(0.f), VAlign_Top);
	return Head;
}

UWidget* USupplyLockerPage::BuildCategories(const FBuilder& B)
{
	// .categories: gap 5, border-bottom 2px paper, padding-bottom 8 / .channel 오른쪽
	UOverlay* Bar = B.Overlay();
	UVerticalBox* Stack = B.VBox();
	AddFill(Bar, Stack);
	UHorizontalBox* Row = B.HBox();
	AddV(Stack, Row, FMargin(0.f, 0.f, 0.f, 8.f));
	AddV(Stack, Rule(B, Paper(), 2.f));

	const FText Labels[3] = { LOCTEXT("All", "전체"), LOCTEXT("Permanent", "장비"), LOCTEXT("Consumable", "소모품") };
	// 한 줄 안 크기가 다른 글자(13/15/12px)가 15px 기준선을 공유한다(줄 상자 20)
	const float Baseline = CssBaseline(15.f, ERef::Malgun, 20.f);
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FCategoryWidgets& Tab = Categories[Index];
		USizeBox* Size = B.Size();
		Size->SetMinDesiredWidth(135.f);
		UOverlay* Card = B.Overlay();
		Card->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Size->AddChild(Card);
		Tab.Card = Card;
		Tab.Face = B.Fill(FLinearColor::Transparent);
		AddFill(Card, Tab.Face);

		UHorizontalBox* Content = B.HBox();
		Content->SetRenderTransformPivot(FVector2D(.5f, .5f));
		Tab.Content = Content;
		UTextBlock* IndexText = B.Text(TwoDigits(Index + 1), Font(EFace::ExtraBold, 13.f, 0.f, true), Muted());
		Typeset(IndexText, ERef::Segoe, 20.f, Baseline);
		UTextBlock* LabelText = B.Text(Labels[Index], Font(EFace::Bold, 15.f), Muted());
		Typeset(LabelText, ERef::Malgun, 20.f, Baseline);
		Tab.Count = B.Text(Dash(), Font(EFace::ExtraBold, 12.f), Muted());
		Typeset(Tab.Count, ERef::Segoe, 20.f, Baseline);
		AddH(Content, IndexText, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Top);
		AddH(Content, LabelText, FMargin(0.f, 0.f, 20.f, 0.f), VAlign_Top);
		AddH(Content, Tab.Count, FMargin(0.f), VAlign_Top);
		Tab.Texts = { IndexText, LabelText, Tab.Count };
		AddOverlay(Card, Content, HAlign_Left, VAlign_Fill, FMargin(22.f, Css(12.f, 9.f)));

		// .category:focus — outline 3px lime, offset 0
		UWidget* Ring = B.Ring(Lime(), 3.f);
		AddOverlay(Card, Ring, HAlign_Fill, VAlign_Fill, FMargin(-3.f));
		Ring->SetVisibility(ESlateVisibility::Hidden);
		Tab.Ring = Ring;

		AddH(Row, Size, FMargin(0.f, 0.f, Index < 2 ? 5.f : 0.f, 0.f), VAlign_Fill);
		AddHitTarget(Size, [this, Index]()
		{
			SetCategory(Index, true);
			SetArea(EArea::Category);
		});
	}
	AddH(Row, B.Spacer(0.f, 0.f), FMargin(0.f), VAlign_Fill, 1.f);

	UVerticalBox* Channel = B.VBox();
	AddV(Channel, B.LineBox(B.Text(LOCTEXT("Channel", "PRESSURE INDEX"), Font(EFace::Bold, 11.f, .12f), Muted()), 17.6f, ERef::Segoe), FMargin(0.f), HAlign_Right);
	AddV(Channel, B.LineBox(B.Text(LOCTEXT("ChannelSub", "공유 장비 · 팀 전체"), Font(EFace::Bold, 14.f, .12f), Paper()), 22.4f), FMargin(0.f), HAlign_Right);
	AddH(Row, Channel, FMargin(0.f), VAlign_Center);

	// .category:focus:after ▼ — 분류 띠의 오른쪽 위(right 8, top -11)
	UTextBlock* Caret = B.LineBox(B.Text(FText::FromString(TEXT("▼")), Font(EFace::Regular, 16.f), Lime()));
	AddOverlay(Bar, Caret, HAlign_Right, VAlign_Top, FMargin(0.f, -11.f, 8.f, 0.f));
	Caret->SetVisibility(ESlateVisibility::Hidden);
	CategoryCaret = Caret;
	return Bar;
}

UWidget* USupplyLockerPage::BuildDetail(const FBuilder& B)
{
	UVerticalBox* Detail = B.VBox();

	// .artboard: clamp(170px,27vh,310px), 코발트 + 2px #7185f5, 안쪽 틀(inset 20, -6°), 종이색 띠, 큰 번호, +, 도해, 라벨
	// 압축: 높이 100, 도해 .58, 큰 번호 100px, 라벨 8px(bottom 12)
	ArtBoard = B.Size(-1.f, Css(FMath::Clamp(GetViewSize().Y * .27f, 170.f, 310.f), 100.f));
	UBorder* Frame = B.Fill(Hex(0x7185f5), FMargin(2.f));
	Frame->SetClipping(EWidgetClipping::ClipToBounds);
	ArtBoard->AddChild(Frame);
	UOverlay* Inner = B.Overlay();
	Frame->SetContent(Inner);
	AddFill(Inner, B.Fill(Blue()));

	UWidget* InnerFrame = B.Ring(MixSRGB(0xbac7ff, 0x55 / 255.f, 0x2747d8), 1.f);
	InnerFrame->SetRenderTransformPivot(FVector2D(.5f, .5f));
	InnerFrame->SetRenderTransformAngle(-6.f);
	AddOverlay(Inner, InnerFrame, HAlign_Fill, VAlign_Fill, FMargin(20.f));

	UCanvasPanel* Decor = B.Canvas();
	UBorder* Band = B.Fill(Paper());
	Band->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Band->SetRenderTransformAngle(-16.f);
	UCanvasPanelSlot* BandSlot = Decor->AddChildToCanvas(Band);
	// width 120%, height 34, left -6%, bottom -12
	BandSlot->SetAnchors(FAnchors(-.06f, 1.f, 1.14f, 1.f));
	BandSlot->SetOffsets(FMargin(0.f, -22.f, 0.f, 34.f));
	const float ArtIndexPx = Css(175.f, 100.f);
	ArtIndexText = B.Text(LOCTEXT("ArtIndex", "01"), Font(EFace::Display, ArtIndexPx, -.1f, true), MixSRGB(0xffffff, 0x18 / 255.f, 0x2747d8));
	Typeset(ArtIndexText, ERef::ArialBlack, ArtIndexPx);
	UCanvasPanelSlot* IndexSlot = Decor->AddChildToCanvas(ArtIndexText);
	IndexSlot->SetAutoSize(true);
	IndexSlot->SetPosition(FVector2D(14.f, -12.f));
	AddFill(Inner, Decor);

	UTextBlock* Cross = B.LineBox(B.Text(FText::FromString(TEXT("+")), Font(EFace::Regular, 25.f), Paper()));
	AddOverlay(Inner, Cross, HAlign_Right, VAlign_Top, FMargin(0.f, 10.f, 16.f, 0.f));

	Art = B.Tree->ConstructWidget<USupplyEquipmentArt>(USupplyEquipmentArt::StaticClass());
	Art->SetArtScale(Css(1.f, .58f));
	AddOverlay(Inner, Art, HAlign_Center, VAlign_Center);

	AddOverlay(Inner, B.LineBox(B.Text(LOCTEXT("ArtLabel", "GOHOME / EQUIPMENT STUDY"), Font(EFace::Regular, Css(10.f, 8.f), .18f), Paper())), HAlign_Left, VAlign_Bottom, FMargin(18.f, 0.f, 0.f, Css(19.f, 12.f)));
	AddV(Detail, ArtBoard);

	// .detail-copy: padding-top 22(≥1500)
	UVerticalBox* Copy = B.VBox();
	DetailCopy = Copy;
	// 압축: kind 10, h2 25/1.15(margin 5 0 7), 설명 12/1.4(min-height 34)
	KindText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, Css(11.f, 10.f), .1f), Lime()));
	const float NamePx = Css(39.f, 25.f);
	NameText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, NamePx, -.05f), Paper()), NamePx * Css(1.2f, 1.15f));
	NameText->SetAutoWrapText(true);
	const float DescriptionPx = Css(15.f, 12.f);
	DescriptionText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, DescriptionPx), Hex(0xcad4e5)), DescriptionPx * Css(1.65f, 1.4f));
	DescriptionText->SetAutoWrapText(true);
	USizeBox* DescriptionBox = B.Size();
	DescriptionBox->SetMinDesiredHeight(Css(44.f, 34.f));
	DescriptionBox->AddChild(DescriptionText);
	AddV(Copy, KindText);
	AddV(Copy, NameText, FMargin(0.f, Css(7.f, 5.f), 0.f, Css(9.f, 7.f)));
	AddV(Copy, DescriptionBox);

	// .quantities: margin 18 0 14, border-block 1px, 4칸(flex 1, padding 10 12, 첫 칸 padding-left 0)
	UVerticalBox* QuantityBlock = B.VBox();
	AddV(QuantityBlock, Rule(B, Line()));
	UHorizontalBox* Cells = B.HBox();
	auto AddCell = [&](const FText& Label, TObjectPtr<UTextBlock>& OutValue, TObjectPtr<UTextBlock>* OutLabel, bool bFirst, bool bLast, TObjectPtr<UBorder>* OutBlock)
	{
		// 압축: padding-block 6, dt 10(mb 3), dd 25
		// 가로 패딩은 슬롯에 — flex:1은 패딩 뺀 내용 폭을 똑같이 나눈다(첫 칸 padding-left 0이라 첫 칸만 12 좁다). 박스 패널 채움도 슬롯 패딩은 고정분으로 뺀다.
		UBorder* Cell = B.Fill(FLinearColor::Transparent, FMargin(0.f, Css(10.f, 6.f)));
		UVerticalBox* Stack = B.VBox();
		UTextBlock* LabelText = B.LineBox(B.Text(Label, Font(EFace::Regular, Css(11.f, 10.f)), Muted()));
		AddV(Stack, LabelText, FMargin(0.f, 0.f, 0.f, Css(5.f, 3.f)));
		const float ValuePx = Css(29.f, 25.f);
		OutValue = B.LineBox(B.Text(Dash(), Font(EFace::ExtraBold, ValuePx), Paper()), ValuePx, ERef::Segoe);
		AddV(Stack, OutValue);
		Cell->SetContent(Stack);
		AddH(Cells, Cell, FMargin(bFirst ? 0.f : 12.f, 0.f, 12.f, 0.f), VAlign_Fill, 1.f);
		if (!bLast)
		{
			AddH(Cells, VRule(B, Line()), FMargin(0.f), VAlign_Fill);
		}
		if (OutLabel)
		{
			*OutLabel = LabelText;
		}
		if (OutBlock)
		{
			*OutBlock = Cell;
		}
	};
	AddCell(LOCTEXT("Stored", "보관 중"), StoredText, &StoredLabel, true, false, &StoredBlock);
	AddCell(LOCTEXT("CheckedOut", "팀 반출"), CheckedOutText, nullptr, false, false, nullptr);
	AddCell(LOCTEXT("Held", "내 소지"), HeldText, nullptr, false, false, nullptr);
	AddCell(LOCTEXT("Owned", "팀 보유"), OwnedText, nullptr, false, true, nullptr);
	AddV(QuantityBlock, Cells);
	AddV(QuantityBlock, Rule(B, Line()));
	AddV(Copy, QuantityBlock, FMargin(0.f, Css(18.f, 9.f), 0.f, Css(14.f, 8.f)));

	// #transferControls(탐사만) — .quantities margin-bottom 14 바로 아래
	TransferBlock = BuildTransfer(B);
	AddV(Copy, TransferBlock);

	// .action-note: 11px/1.5, min-height 33
	// 압축: 10px/1.4, min-height 28, margin-top 6
	const float NotePx = Css(11.f, 10.f);
	ActionNoteText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, NotePx), Muted()), NotePx * Css(1.5f, 1.4f));
	ActionNoteText->SetAutoWrapText(true);
	USizeBox* NoteBox = B.Size();
	NoteBox->SetMinDesiredHeight(Css(33.f, 28.f));
	NoteBox->AddChild(ActionNoteText);
	// 로비에선 위 .quantities margin-bottom 14와 .action-note margin-top 10이 겹쳐(마진 상쇄) 14만 남는다 — 탐사는 버튼 뒤 10
	NoteSlot = AddV(Copy, NoteBox);
	AddV(Detail, Copy, FMargin(0.f, Css(22.f, 10.f), 0.f, 0.f));
	return Detail;
}

UWidget* USupplyLockerPage::BuildManifest(const FBuilder& B)
{
	UVerticalBox* Manifest = B.VBox();

	// .manifest-head: flex end, border-bottom 1px, padding-bottom 12
	UHorizontalBox* Head = B.HBox();
	UVerticalBox* TitleStack = B.VBox();
	CategoryTitleText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 11.f, .12f), Lime()));
	AddV(TitleStack, CategoryTitleText, FMargin(0.f, 0.f, 0.f, 6.f));
	const float OnBoardPx = Css(65.f, 45.f);
	AddV(TitleStack, B.LineBox(B.Text(LOCTEXT("OnBoard", "ON BOARD."), Font(EFace::Display, OnBoardPx, -.055f, true), Paper()), OnBoardPx, ERef::ArialBlack));
	AddH(Head, TitleStack, FMargin(0.f), VAlign_Bottom, 1.f);
	TotalText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Muted()), 19.2f);
	TotalText->SetJustification(ETextJustify::Right);
	AddH(Head, TotalText, FMargin(16.f, 0.f, 0.f, 0.f), VAlign_Bottom);
	AddV(Manifest, Head, FMargin(0.f, 2.f, 0.f, Css(12.f, 10.f)));
	AddV(Manifest, Rule(B, Line()));

	UHorizontalBox* ColumnHead = B.HBox();
	AddH(ColumnHead, B.LineBox(B.Text(LOCTEXT("HeadItems", "보유 아이템"), Font(EFace::Regular, 10.f, .08f), Muted())), FMargin(0.f), VAlign_Top, 1.f);
	AddH(ColumnHead, B.LineBox(B.Text(LOCTEXT("HeadCounts", "보관 / 팀 보유"), Font(EFace::Regular, 10.f, .08f), Muted())), FMargin(0.f), VAlign_Top);
	AddV(Manifest, ColumnHead, FMargin(30.f, Css(15.f, 11.f), 17.f, 9.f));

	// .rows: gap 8, max-height 44vh, padding 6 10 12 17
	RowsFrame = B.Size();
	RowsFrame->SetMaxDesiredHeight(GetViewSize().Y * Css(.44f, .41f));
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
		FSlateBrush Clear;
		Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
		Clear.ImageSize = FVector2D::ZeroVector;
		BarStyle.SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDraggedThumbImage(Thumb);
		BarStyle.SetVerticalBackgroundImage(Clear).SetVerticalTopSlotImage(Clear).SetVerticalBottomSlotImage(Clear);
		BarStyle.SetThickness(4.f);
		RowsScroll->SetWidgetBarStyle(BarStyle);
		// SScrollBox 기본 위/아래 가장자리 그림자(스크롤 가능할 때 그라데이션 띠) — CSS overflow엔 없다
		FScrollBoxStyle BoxStyle = RowsScroll->GetWidgetStyle();
		BoxStyle.SetTopShadowBrush(Clear).SetBottomShadowBrush(Clear).SetLeftShadowBrush(Clear).SetRightShadowBrush(Clear);
		RowsScroll->SetWidgetStyle(BoxStyle);
	}
	UBorder* RowsPad = B.Box(FMargin(17.f, 6.f, 10.f, 12.f));
	RowsList = B.VBox();
	RowsPad->SetContent(RowsList);
	RowsScroll->AddChild(RowsPad);
	RowsFrame->AddChild(RowsScroll);
	AddV(Manifest, RowsFrame);

	// .empty: padding 44 15, border-bottom
	UVerticalBox* Empty = B.VBox();
	EmptyTitle = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 23.f), Paper()));
	EmptyBody = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 15.f), Muted()), 15.f * 1.7f);
	AddV(Empty, EmptyTitle, FMargin(0.f, 0.f, 0.f, 9.f));
	AddV(Empty, EmptyBody);
	UVerticalBox* EmptyStack = B.VBox();
	UBorder* EmptyPad = B.Box(FMargin(15.f, 44.f));
	EmptyPad->SetContent(Empty);
	AddV(EmptyStack, EmptyPad);
	AddV(EmptyStack, Rule(B, Line()));
	EmptyBox = EmptyStack;
	EmptyBox->SetVisibility(ESlateVisibility::Collapsed);
	AddV(Manifest, EmptyStack);

	// .manifest-foot: border-top, padding 15 10, 11px
	AddV(Manifest, Rule(B, Line()));
	UHorizontalBox* Foot = B.HBox();
	PositionText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 11.f), Muted()));
	AddH(Foot, PositionText, FMargin(0.f), VAlign_Top, 1.f);
	UHorizontalBox* Legend = B.HBox();
	AddH(Legend, B.LineBox(B.Text(LOCTEXT("LegendSelected", "색면 = 선택 "), Font(EFace::Regular, 11.f), Muted())));
	AddH(Legend, B.LineBox(B.Text(LOCTEXT("LegendFocus", "▶ = 현재 조작"), Font(EFace::Bold, 11.f), Lime())));
	AddH(Foot, Legend, FMargin(20.f, 0.f, 0.f, 0.f), VAlign_Top);
	AddV(Manifest, Foot, FMargin(10.f, Css(15.f, 11.f)));
	return Manifest;
}

UWidget* USupplyLockerPage::BuildRow(const FBuilder& B)
{
	const int32 RowIndex = Rows.Num();
	FRowWidgets& Row = Rows.AddDefaulted_GetRef();

	// .item-row: min-height 88(≥1500), grid 30px/1fr/auto gap 12, padding 12 17 12 10, border-bottom 1px #56647a
	// 압축: min-height 65, padding-block 9, 이름 18, 종류 mt 4
	USizeBox* Root = B.Size();
	Root->SetMinDesiredHeight(Css(88.f, 65.f));
	Row.Root = Root;
	UOverlay* Card = B.Overlay();
	Root->AddChild(Card);

	// :before inset 0 5px, skewX(-7deg)
	UBorder* Face = B.Fill(FLinearColor::Transparent);
	Face->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Face->SetRenderShear(FVector2D(-7.f, 0.f));
	Row.Face = Face;
	AddOverlay(Card, Face, HAlign_Fill, VAlign_Fill, FMargin(5.f, 0.f));
	UWidget* Ring = B.Ring(Lime(), 3.f);
	Ring->SetRenderTransformPivot(FVector2D(.5f, .5f));
	Ring->SetRenderShear(FVector2D(-7.f, 0.f));
	Ring->SetVisibility(ESlateVisibility::Hidden);
	Row.Ring = Ring;
	AddOverlay(Card, Ring, HAlign_Fill, VAlign_Fill, FMargin(5.f, 0.f));

	UHorizontalBox* Content = B.HBox();
	USizeBox* IndexCell = B.Size(30.f);
	Row.Index = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Black, 18.f, 0.f, true), Paper()), -1.f, ERef::Segoe);
	Row.Index->SetRenderOpacity(.7f);
	SetContent(IndexCell, Row.Index, HAlign_Left, VAlign_Center);
	AddH(Content, IndexCell, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center);

	UVerticalBox* Words = B.VBox();
	const float RowNamePx = Css(23.f, 18.f);
	Row.Name = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, RowNamePx, -.04f), Paper()), RowNamePx * 1.25f);
	Row.Kind = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 10.f), Paper()), 12.5f);
	Row.Kind->SetRenderOpacity(.76f);
	AddV(Words, Row.Name);
	AddV(Words, Row.Kind, FMargin(0.f, Css(6.f, 4.f), 0.f, 0.f));
	AddH(Content, Words, FMargin(0.f, 0.f, 12.f, 0.f), VAlign_Center, 1.f);

	UVerticalBox* Counts = B.VBox();
	Row.Quantity = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Black, 26.f), Paper()), -1.f, ERef::Segoe);
	Row.State = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 10.f), Paper()));
	AddV(Counts, Row.Quantity, FMargin(0.f), HAlign_Right);
	AddV(Counts, Row.State, FMargin(0.f, 3.f, 0.f, 0.f), HAlign_Right);
	AddH(Content, Counts, FMargin(0.f), VAlign_Center);

	AddOverlay(Card, Content, HAlign_Fill, VAlign_Center, FMargin(10.f, Css(12.f, 9.f), 17.f, Css(13.f, 10.f)));
	AddOverlay(Card, Rule(B, Hex(0x56647a)), HAlign_Fill, VAlign_Bottom);

	// :focus:after ▶ 22px, left -15, top 24
	UTextBlock* CursorMark = B.LineBox(B.Text(FText::FromString(TEXT("▶")), Font(EFace::Regular, 22.f), Lime()));
	CursorMark->SetVisibility(ESlateVisibility::Hidden);
	Row.Cursor = CursorMark;
	UBorder* CursorHolder = B.Box();
	CursorHolder->SetContent(CursorMark);
	AddOverlay(Card, CursorHolder, HAlign_Left, VAlign_Top, FMargin(-15.f, 24.f, 0.f, 0.f));

	AddHitTarget(Root, [this, RowIndex]()
	{
		if (Rows.IsValidIndex(RowIndex) && Rows[RowIndex].ProductId != NAME_None)
		{
			// 로비 행 클릭은 선택만, 탐사는 상세 조작으로
			Select(Rows[RowIndex].ProductId);
			SetArea(bExpedition ? EArea::Detail : EArea::List);
		}
	});
	return Root;
}

UWidget* USupplyLockerPage::BuildTransfer(const FBuilder& B)
{
	UVerticalBox* Block = B.VBox();

	// .action-head: 03 / 장비 조작 ↔ 1개씩 (10px 800 .1em, 상세 포커스면 라임), margin-bottom 8
	UHorizontalBox* Head = B.HBox();
	UTextBlock* ZoneLeft = B.LineBox(B.Text(LOCTEXT("ZoneLeft", "03 / 장비 조작"), Font(EFace::Bold, 10.f, .1f), Muted()));
	UTextBlock* ZoneRight = B.LineBox(B.Text(LOCTEXT("ZoneRight", "1개씩"), Font(EFace::Bold, 10.f, .1f), Muted()));
	ZoneTags = { ZoneLeft, ZoneRight };
	AddH(Head, ZoneLeft, FMargin(0.f), VAlign_Center, 1.f);
	AddH(Head, ZoneRight, FMargin(0.f), VAlign_Center);
	AddV(Block, Head, FMargin(0.f, 0.f, 0.f, Css(8.f, 6.f)));

	// .actions: gap 8, .action padding 13 14, min-height 48, 14px 900
	UHorizontalBox* Actions = B.HBox();
	auto MakeAction = [&](FActionWidgets& Button, const FText& Label, const TCHAR* Arrow, ESharedLockerAction InAction, bool bFirst)
	{
		// 압축: min-height 40, padding 9 12, 13px
		USizeBox* Size = B.Size();
		Size->SetMinDesiredHeight(Css(48.f, 40.f));
		UOverlay* Card = B.Overlay();
		Size->AddChild(Card);
		UBorder* Face = B.Fill(Paper(), FMargin(Css(14.f, 12.f), Css(13.f, 9.f)));
		UHorizontalBox* Row = B.HBox();
		const float ActionPx = Css(14.f, 13.f);
		UTextBlock* LabelText = B.LineBox(B.Text(Label, Font(EFace::Bold, ActionPx), Ink()));
		UTextBlock* ArrowText = B.LineBox(B.Text(FText::FromString(Arrow), Font(EFace::Bold, ActionPx), Ink()));
		AddH(Row, LabelText, FMargin(0.f), VAlign_Center, 1.f);
		AddH(Row, ArrowText, FMargin(8.f, 0.f, 0.f, 0.f), VAlign_Center);
		Face->SetContent(Row);
		AddFill(Card, Face);
		UWidget* Edge = B.Ring(Paper(), 1.f);
		AddFill(Card, Edge);
		UWidget* Ring = B.Ring(Lime(), 3.f);
		AddOverlay(Card, Ring, HAlign_Fill, VAlign_Fill, FMargin(-7.f));
		Ring->SetVisibility(ESlateVisibility::Hidden);
		// .action:focus:before ▶ left -17 top 14
		UTextBlock* Mark = B.LineBox(B.Text(FText::FromString(TEXT("▶")), Font(EFace::Regular, 14.f), Lime()));
		Mark->SetVisibility(ESlateVisibility::Hidden);
		UBorder* MarkHolder = B.Box();
		MarkHolder->SetContent(Mark);
		AddOverlay(Card, MarkHolder, HAlign_Left, VAlign_Top, FMargin(-17.f, 14.f, 0.f, 0.f));
		Button.Root = Size;
		Button.Face = Face;
		Button.Edge = Edge;
		Button.FocusRing = Ring;
		Button.Cursor = Mark;
		Button.Texts = { LabelText, ArrowText };
		AddH(Actions, Size, FMargin(bFirst ? 0.f : 8.f, 0.f, 0.f, 0.f), VAlign_Fill, 1.f);
		AddHitTarget(Size, [this, InAction]()
		{
			if (bExpedition)
			{
				SetAction(InAction);
				SetArea(EArea::Detail);
				OpenConfirm(InAction);
			}
		});
	};
	MakeAction(WithdrawButton, LOCTEXT("Withdraw", "꺼내기"), TEXT("↙"), ESharedLockerAction::Withdraw, true);
	MakeAction(DepositButton, LOCTEXT("Deposit", "넣기"), TEXT("↗"), ESharedLockerAction::Deposit, false);
	AddV(Block, Actions);
	return Block;
}

UWidget* USupplyLockerPage::BuildConfirm(const FBuilder& B)
{
	// .layer #030817d9 + .dialog: 540, 종이색, border 4 ink, box-shadow 12px 12px 0 blue, padding 28
	UOverlay* Layer = B.Overlay();
	AddFill(Layer, B.Fill(Hex(0x030817, 0xd9 / 255.f)));
	USizeBox* Width = B.Size(540.f);
	AddOverlay(Layer, Width, HAlign_Center, VAlign_Center);
	UOverlay* Card = B.Overlay();
	Card->SetRenderTransformPivot(FVector2D(.5f, .5f));
	ConfirmDialog = Card;
	Width->AddChild(Card);
	UBorder* Shadow = B.Fill(Blue());
	Shadow->SetRenderTranslation(FVector2D(12.f, 12.f));
	AddFill(Card, Shadow);
	UBorder* Frame = B.Fill(Ink(), FMargin(4.f));
	AddFill(Card, Frame);
	UBorder* Sheet = B.Fill(Paper(), FMargin(28.f));
	Frame->SetContent(Sheet);
	UVerticalBox* Column = B.VBox();
	Sheet->SetContent(Column);

	// .dialog p 규칙이 .micro를 14px/1.7로 덮는다(시안 그대로)
	AddV(Column, B.LineBox(B.Text(LOCTEXT("ConfirmMicro", "CREW EQUIPMENT / TRANSFER"), Font(EFace::Bold, 14.f, .18f), Hex(0x475165)), 23.8f, ERef::Segoe));
	ConfirmTitleText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Bold, 40.f, -.05f), Ink()), 48.f);
	AddV(Column, ConfirmTitleText, FMargin(0.f, 11.f));
	ConfirmDescriptionText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 14.f), Ink()), 23.8f);
	AddV(Column, ConfirmDescriptionText);

	// .dialog-count: margin 21 0, padding 12 0, border-block 2px ink, 12px / strong 30px
	UVerticalBox* Count = B.VBox();
	AddV(Count, Rule(B, Ink(), 2.f));
	UHorizontalBox* CountRow = B.HBox();
	ConfirmItemText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Ink()));
	AddH(CountRow, ConfirmItemText, FMargin(0.f), VAlign_Center, 1.f);
	AddH(CountRow, B.LineBox(B.Text(FText::FromString(TEXT("× 1")), Font(EFace::Bold, 30.f), Ink())), FMargin(0.f), VAlign_Center);
	AddV(Count, CountRow, FMargin(0.f, 12.f));
	AddV(Count, Rule(B, Ink(), 2.f));
	AddV(Column, Count, FMargin(0.f, 21.f));

	ConfirmReasonText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Hex(0x6e3019)), 20.4f);
	ConfirmReasonText->SetAutoWrapText(true);
	USizeBox* ReasonBox = B.Size();
	ReasonBox->SetMinDesiredHeight(20.f);
	ReasonBox->AddChild(ConfirmReasonText);
	AddV(Column, ReasonBox);

	// .dialog-actions: gap 10, mt 20 — 취소 / 요청 확인(ink)
	UHorizontalBox* Actions = B.HBox();
	auto MakeButton = [&](FDialogButton& Button, const FText& Label, bool bCommit, int32 Index)
	{
		USizeBox* Size = B.Size(-1.f, 52.f);
		UOverlay* ButtonCard = B.Overlay();
		Size->AddChild(ButtonCard);
		Button.Face = B.Fill(bCommit ? Ink() : FLinearColor::Transparent);
		AddFill(ButtonCard, Button.Face);
		Button.Edge = B.Ring(Ink(), 2.f);
		AddFill(ButtonCard, Button.Edge);
		Button.Text = B.LineBox(B.Text(Label, Font(EFace::Bold, 15.f), bCommit ? Paper() : Ink()));
		AddOverlay(ButtonCard, Button.Text, HAlign_Center, VAlign_Center);
		UWidget* Ring = B.Ring(Blue(), 3.f);
		AddOverlay(ButtonCard, Ring, HAlign_Fill, VAlign_Fill, FMargin(-6.f));
		Ring->SetVisibility(ESlateVisibility::Hidden);
		Button.FocusRing = Ring;
		Button.Root = Size;
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
		}, LockerConfirmLayerIndex);
	};
	MakeButton(CancelButton, LOCTEXT("Cancel", "취소"), false, 0);
	MakeButton(CommitButton, LOCTEXT("Commit", "요청 확인"), true, 1);
	AddV(Column, Actions, FMargin(0.f, 20.f, 0.f, 0.f));
	return Layer;
}

UWidget* USupplyLockerPage::BuildFooter(const FBuilder& B)
{
	// .status-band(min-height 49, border-top, padding-top 13) + gap 25 + .footer(26, 시험 버튼 높이 유지)
	UVerticalBox* Stack = B.VBox();
	AddV(Stack, Rule(B, Line()));
	// 압축: status-band min-height 34(padding-top 8), gap 12, .footer 11px
	USizeBox* BandSize = B.Size();
	BandSize->SetMinDesiredHeight(Css(36.f, 26.f));
	UHorizontalBox* Band = B.HBox();
	BandSize->AddChild(Band);
	USizeBox* Signal = B.Size(8.f, 8.f);
	Signal->AddChild(B.Fill(Lime()));
	AddH(Band, Signal, FMargin(0.f, 0.f, 14.f, 0.f), VAlign_Center);
	StatusText = B.LineBox(B.Text(FText::GetEmpty(), Font(EFace::Regular, 14.f), Hex(0xccd6e9)), 22.4f);
	AddH(Band, StatusText, FMargin(0.f), VAlign_Center, 1.f);
	AddV(Stack, BandSize, FMargin(0.f, Css(13.f, 8.f), 0.f, 0.f));

	USizeBox* FootSize = B.Size(-1.f, 26.f);
	HelpBox = B.HBox();
	FootSize->AddChild(HelpBox);
	FooterRow = FootSize;
	AddV(Stack, FootSize, FMargin(0.f, Css(25.f, 12.f), 0.f, 0.f));
	return Stack;
}

// ---------------------------------------------------------------------------------------------------------------------
// 데이터

TArray<FSharedLockerViewEntry> USupplyLockerPage::GetOwnedEntries() const
{
	TArray<FSharedLockerViewEntry> Result;
	for (const FSharedLockerViewEntry& Entry : Entries)
	{
		if (Entry.OwnedQuantity > 0)
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

bool USupplyLockerPage::IsCategoryMatch(int32 InCategory, const FSharedLockerViewEntry& Entry) const
{
	return InCategory == 0
		|| (InCategory == 1 && Entry.Lifetime == EItemShopItemLifetime::Permanent)
		|| (InCategory == 2 && Entry.Lifetime == EItemShopItemLifetime::Consumable);
}

TArray<FSharedLockerViewEntry> USupplyLockerPage::GetVisibleEntries() const
{
	TArray<FSharedLockerViewEntry> Result;
	for (const FSharedLockerViewEntry& Entry : GetOwnedEntries())
	{
		if (IsCategoryMatch(Category, Entry))
		{
			Result.Add(Entry);
		}
	}
	return Result;
}

const FSharedLockerViewEntry* USupplyLockerPage::FindEntry(FName ProductId) const
{
	return Entries.FindByPredicate([ProductId](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == ProductId && Entry.OwnedQuantity > 0; });
}

const FItemShopProduct* USupplyLockerPage::FindProduct(FName ProductId) const
{
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	const UItemShopCatalogDataAsset* Catalog = Backend ? Backend->GetShopCatalog() : nullptr;
	return Catalog ? Catalog->FindProduct(ProductId) : nullptr;
}

FText USupplyLockerPage::GetEntryName(const FSharedLockerViewEntry& Entry) const
{
	if (const FItemShopProduct* Product = FindProduct(Entry.ProductId))
	{
		if (!Product->ShopDisplayName.IsEmpty())
		{
			return Product->ShopDisplayName;
		}
		if (Product->ItemData && !Product->ItemData->DisplayName.IsEmpty())
		{
			return Product->ItemData->DisplayName;
		}
	}
	return FText::FromName(Entry.ProductId);
}

// ---------------------------------------------------------------------------------------------------------------------
// 수명·구독

void USupplyLockerPage::OnPageShown(bool bBack, FName FocusHint)
{
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	bExpedition = Backend && Backend->GetSupplyEntry() == ESupplyEntry::ExpeditionLocker;
	ViewContextText->SetText(bExpedition ? LOCTEXT("ContextExpedition", "탐사 · 아이템 박스") : LOCTEXT("ContextLobby", "로비 · 조회 전용"));

	TransferBlock->SetVisibility(bExpedition ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	NoteSlot->SetPadding(FMargin(0.f, bExpedition ? Css(10.f, 6.f) : 0.f, 0.f, 0.f));
	Action = ESharedLockerAction::Withdraw;
	bConfirmOpen = false;
	HitLayer = 0;
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);

	BindSources();
	Entries = BoundGameState.IsValid() ? BoundGameState->GetSharedLockerItems() : TArray<FSharedLockerViewEntry>();

	// 시안 openLocker: 전체 분류, 선택 없음 → 데이터가 오면 첫 행·목록 포커스
	CategoryMemory.Reset();
	Category = 0;
	SelectedId = NAME_None;
	LastStored = INDEX_NONE;
	SetCategory(0, false);
	Area = SelectedId != NAME_None ? EArea::List : EArea::Category;

	if (!BoundGameState.IsValid())
	{
		SetStatus(LOCTEXT("StatusLoading", "공유 상태를 불러오는 중입니다."), false);
	}
	else if (IsPending())
	{
		SetStatus(LOCTEXT("StatusPendingOpen", "이전 요청의 결과를 기다리는 중입니다."), false);
	}
	else
	{
		SetStatus(LOCTEXT("StatusReady", "팀의 장비가 여기에 있습니다."), false);
	}
	RefreshAll(false);
}

void USupplyLockerPage::OnPageHidden()
{
	UnbindSources();
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		for (const FName& Track : { LockerListTrack, LockerDetailTrack, LockerArtTrack, LockerCursorTrack, LockerStoredTrack, LockerConfirmTrack })
		{
			Animator->Finish(Track);
		}
	}
}

void USupplyLockerPage::NativeDestruct()
{
	UnbindSources();
	Super::NativeDestruct();
}

void USupplyLockerPage::BindSources()
{
	UnbindSources();
	if (AGoHomeGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AGoHomeGameState>() : nullptr)
	{
		BoundGameState = GameState;
		GameState->OnSharedLockerChanged.AddUniqueDynamic(this, &USupplyLockerPage::HandleLockerChanged);
	}
	// 내 소지(반납 가능) 수량은 내 폰 핫바가 바뀔 때 다시 센다
	if (APawn* OwnerPawn = GetOwningPlayerPawn())
	{
		if (UInventoryComponent* Inventory = OwnerPawn->FindComponentByClass<UInventoryComponent>())
		{
			BoundInventory = Inventory;
			Inventory->OnInventoryChanged.AddUniqueDynamic(this, &USupplyLockerPage::HandleInventoryChanged);
		}
	}
	USupplyScreenWidget* Screen = GetHost();
	if (ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr)
	{
		LockerResultHandle = Backend->OnSupplyLockerResult().AddUObject(this, &USupplyLockerPage::HandleLockerResult);
	}
}

void USupplyLockerPage::UnbindSources()
{
	if (AGoHomeGameState* GameState = BoundGameState.Get())
	{
		GameState->OnSharedLockerChanged.RemoveDynamic(this, &USupplyLockerPage::HandleLockerChanged);
	}
	BoundGameState.Reset();
	if (UInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnInventoryChanged.RemoveDynamic(this, &USupplyLockerPage::HandleInventoryChanged);
	}
	BoundInventory.Reset();
	USupplyScreenWidget* Screen = GetHost();
	if (ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr)
	{
		Backend->OnSupplyLockerResult().Remove(LockerResultHandle);
	}
	LockerResultHandle.Reset();
}

void USupplyLockerPage::HandleLockerChanged()
{
	Reconcile();
}

void USupplyLockerPage::HandleInventoryChanged()
{
	RefreshDetail(false);
	RefreshFocus();
}

void USupplyLockerPage::HandleLockerResult(const FSharedLockerResult& Result)
{
	// 결과 문구는 수량 스냅샷과 별개 이벤트 — 포커스는 빼앗지 않는다
	if (Result.Result == EItemShopResult::Success)
	{
		SetStatus(Result.Action == ESharedLockerAction::Withdraw ? LOCTEXT("Withdrawn", "아이템 1개를 꺼냈습니다.") : LOCTEXT("Deposited", "아이템 1개를 넣었습니다."), false);
	}
	else
	{
		SetStatus(Result.Message.IsEmpty() ? LOCTEXT("RequestFailed", "요청을 처리하지 못했습니다. 잠시 후 다시 요청해 주세요.") : Result.Message, true);
	}
	RefreshDetail(false);
	RefreshConfirm();
	RefreshFocus();
}

// ---------------------------------------------------------------------------------------------------------------------
// 탐사 조작

int32 USupplyLockerPage::GetHeld(FName ProductId) const
{
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	return Backend ? Backend->GetHeldLockerQuantity(ProductId) : 0;
}

bool USupplyLockerPage::IsPending() const
{
	const USupplyScreenWidget* Screen = GetHost();
	const ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	return Backend && Backend->IsLockerRequestPending();
}

bool USupplyLockerPage::CanTransfer(ESharedLockerAction InAction, FName ProductId) const
{
	const FSharedLockerViewEntry* Entry = FindEntry(ProductId);
	if (!bExpedition || !Entry || !BoundGameState.IsValid() || IsPending())
	{
		return false;
	}
	return InAction == ESharedLockerAction::Withdraw ? Entry->StoredQuantity > 0 : GetHeld(ProductId) > 0;
}

FText USupplyLockerPage::GetTransferReason(ESharedLockerAction InAction, FName ProductId) const
{
	const FSharedLockerViewEntry* Entry = FindEntry(ProductId);
	if (!BoundGameState.IsValid())
	{
		return LOCTEXT("ReasonLoading", "공유 상태를 확인 중입니다.");
	}
	if (IsPending())
	{
		return LOCTEXT("ReasonPending", "요청 결과를 기다리는 중입니다.");
	}
	if (!Entry)
	{
		return LOCTEXT("ReasonNone", "선택한 아이템이 없습니다.");
	}
	if (InAction == ESharedLockerAction::Withdraw && Entry->StoredQuantity == 0)
	{
		return LOCTEXT("ReasonEmpty", "모두 꺼낸 상태입니다. 보관 수량이 없습니다.");
	}
	if (InAction == ESharedLockerAction::Deposit && GetHeld(ProductId) <= 0)
	{
		return LOCTEXT("ReasonNotHeld", "내가 보관함에서 꺼내 든 같은 아이템이 없습니다. 탐사 중 획득한 아이템의 보관은 별도 규칙입니다.");
	}
	return InAction == ESharedLockerAction::Deposit
		? LOCTEXT("ReasonDeposit", "내가 보관함에서 꺼내 든 같은 아이템 1개를 반납합니다.")
		: LOCTEXT("ReasonWithdraw", "아이템 1개를 빈 핫바 슬롯으로 꺼냅니다.");
}

void USupplyLockerPage::SetAction(ESharedLockerAction InAction)
{
	Action = InAction;
	RefreshDetail(false);
	RefreshFocus();
}

void USupplyLockerPage::OpenConfirm(ESharedLockerAction InAction)
{
	Action = InAction;
	if (!CanTransfer(InAction, SelectedId))
	{
		// 비활성 동작도 포커스는 받아 사유를 읽게 한다 — 요청만 막는다
		SetStatus(GetTransferReason(InAction, SelectedId), true);
		RefreshDetail(false);
		RefreshFocus();
		return;
	}
	bConfirmOpen = true;
	ConfirmAction = InAction;
	ConfirmId = SelectedId;
	ConfirmFocus = 0;
	HitLayer = LockerConfirmLayerIndex;
	ConfirmLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshConfirm();
	RefreshFocus();

	// .dialog.enter confirm-in: Y 12→0, Angle -2→0, opacity .4→1, 160ms snap
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		TWeakObjectPtr<UWidget> Target = ConfirmDialog.Get();
		Animator->Play(LockerConfirmTrack, 0.f, .16f, [Target](float T)
		{
			if (UWidget* Widget = Target.Get())
			{
				const float E = FSupplyEase::Snap().Eval(T);
				FWidgetTransform Transform;
				Transform.Translation = FVector2D(0.f, 12.f * (1.f - E));
				Transform.Angle = -2.f * (1.f - E);
				Widget->SetRenderTransform(Transform);
				Widget->SetRenderOpacity(FMath::Lerp(.4f, 1.f, E));
			}
		});
	}
}

void USupplyLockerPage::CancelConfirm()
{
	if (FSupplyAnimator* Animator = GetAnimator())
	{
		Animator->Finish(LockerConfirmTrack);
	}
	bConfirmOpen = false;
	HitLayer = 0;
	ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	SetArea(EArea::Detail);
}

void USupplyLockerPage::CommitConfirm()
{
	if (!bConfirmOpen || !CanTransfer(ConfirmAction, ConfirmId))
	{
		SetStatus(LOCTEXT("Changed", "상태가 변경되어 요청할 수 없습니다."), true);
		RefreshConfirm();
		return;
	}
	const ESharedLockerAction RequestAction = ConfirmAction;
	const FName RequestId = ConfirmId;
	CancelConfirm();

	// 리슨 호스트는 결과가 요청 호출 안에서 돌아오므로 "처리 중" 문구를 먼저 쓴다
	SetStatus(LOCTEXT("Processing", "요청을 처리 중입니다. 목록 탐색은 계속할 수 있습니다."), false);
	SetArea(EArea::List);
	USupplyScreenWidget* Screen = GetHost();
	ISupplyScreenBackend* Backend = Screen ? Screen->GetBackend() : nullptr;
	if (!Backend || !Backend->RequestLockerTransfer(RequestAction, RequestId))
	{
		SetStatus(LOCTEXT("ReasonPending", "요청 결과를 기다리는 중입니다."), true);
	}
	RefreshDetail(false);
	RefreshFocus();
}

void USupplyLockerPage::RefreshTransfer()
{
	const bool bReady = FindEntry(SelectedId) != nullptr;
	auto Paint = [this, bReady](FActionWidgets& Button, ESharedLockerAction InAction, bool bPrimary)
	{
		const bool bEnabled = CanTransfer(InAction, SelectedId);
		FLinearColor Back = bPrimary ? Paper() : Deep();
		FLinearColor TextColor = bPrimary ? Ink() : Paper();
		bool bEdge = !bPrimary;
		FLinearColor EdgeColor = Paper();
		if (!bEnabled)
		{
			// [aria-disabled]: #1a293d / #91a0b7, 1px #42536d
			Back = Hex(0x1a293d);
			TextColor = Hex(0x91a0b7);
			bEdge = true;
			EdgeColor = Hex(0x42536d);
		}
		Button.Face->SetBrushColor(Back);
		for (UTextBlock* Text : Button.Texts)
		{
			Text->SetColorAndOpacity(FSlateColor(TextColor));
		}
		Button.Edge->SetVisibility(bEdge ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		SetRingColor(Button.Edge, EdgeColor);
	};
	Paint(WithdrawButton, ESharedLockerAction::Withdraw, true);
	Paint(DepositButton, ESharedLockerAction::Deposit, false);
}

void USupplyLockerPage::RefreshConfirm()
{
	if (!bConfirmOpen)
	{
		return;
	}
	const bool bWithdraw = ConfirmAction == ESharedLockerAction::Withdraw;
	const FSharedLockerViewEntry* Entry = FindEntry(ConfirmId);
	ConfirmTitleText->SetText(bWithdraw ? LOCTEXT("ConfirmWithdraw", "장비를 꺼낼까요?") : LOCTEXT("ConfirmDeposit", "장비를 넣을까요?"));
	ConfirmDescriptionText->SetText(bWithdraw ? LOCTEXT("ConfirmWithdrawDesc", "공유 보관함 → 내 핫바") : LOCTEXT("ConfirmDepositDesc", "내 핫바 → 공유 보관함"));
	ConfirmItemText->SetText(Entry ? GetEntryName(*Entry) : LOCTEXT("Gone", "사라진 아이템"));
	ConfirmReasonText->SetText(GetTransferReason(ConfirmAction, ConfirmId));

	// 요청 확인 비활성: #c5c7bd / #535951 (dashed는 실선 대체)
	const bool bEnabled = CanTransfer(ConfirmAction, ConfirmId);
	CommitButton.Face->SetBrushColor(bEnabled ? Ink() : Hex(0xc5c7bd));
	CommitButton.Text->SetColorAndOpacity(FSlateColor(bEnabled ? Paper() : Hex(0x535951)));
	SetRingColor(CommitButton.Edge, bEnabled ? Ink() : Hex(0x535951));
}

// ---------------------------------------------------------------------------------------------------------------------
// 상태 전이

void USupplyLockerPage::SetCategory(int32 InCategory, bool bAnimate)
{
	const bool bChanged = Category != InCategory;
	Category = FMath::Clamp(InCategory, 0, 2);
	const TArray<FSharedLockerViewEntry> Visible = GetVisibleEntries();
	const FName* Remembered = CategoryMemory.Find(Category);
	const FSharedLockerViewEntry* Kept = Remembered ? Visible.FindByPredicate([Remembered](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == *Remembered; }) : nullptr;
	const FName Previous = SelectedId;
	SelectedId = Kept ? Kept->ProductId : (Visible.Num() > 0 ? Visible[0].ProductId : NAME_None);

	RefreshAll(bChanged && Previous != SelectedId);
	if (RowsScroll)
	{
		RowsScroll->SetScrollOffset(0.f);
	}

	FSupplyAnimator* Animator = GetAnimator();
	if (bAnimate && bChanged && Animator && RowsFrame)
	{
		// .rows.changing: Y 7→0, opacity .6→1, 140ms snap
		TWeakObjectPtr<UWidget> Target = RowsFrame.Get();
		Animator->Play(LockerListTrack, 0.f, .14f, [Target](float T)
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

void USupplyLockerPage::Select(FName ProductId)
{
	const bool bChanged = SelectedId != ProductId;
	SelectedId = ProductId;
	CategoryMemory.Add(Category, ProductId);
	RefreshAll(bChanged);

	for (const FRowWidgets& Row : Rows)
	{
		if (Row.ProductId == ProductId && RowsScroll)
		{
			RowsScroll->ScrollWidgetIntoView(Row.Root, false, EDescendantScrollDestination::IntoView);
		}
	}
}

void USupplyLockerPage::SetArea(EArea InArea)
{
	if (InArea == EArea::Detail && !bExpedition)
	{
		InArea = EArea::List;	// 로비 상세는 포커스 대상이 아니다
	}
	if ((InArea == EArea::List || InArea == EArea::Detail) && !FindEntry(SelectedId))
	{
		InArea = EArea::Category;
	}
	Area = InArea;
	RefreshFocus();

	if (Area == EArea::List)
	{
		// ▶ X -7→0, 100ms
		for (const FRowWidgets& Row : Rows)
		{
			if (Row.ProductId == SelectedId)
			{
				if (FSupplyAnimator* Animator = GetAnimator())
				{
					TWeakObjectPtr<UWidget> CursorMark = Row.Cursor.Get();
					Animator->Play(LockerCursorTrack, 0.f, .1f, [CursorMark](float T)
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
}

void USupplyLockerPage::Reconcile()
{
	// 시안 reconcile(): 같은 ProductId·영역 유지, 사라지면 옛 인덱스의 다음 행(없으면 마지막), 비면 분류
	const TArray<FSharedLockerViewEntry> OldVisible = GetVisibleEntries();
	const FName OldId = SelectedId;
	const int32 OldIndex = FMath::Max(0, OldVisible.IndexOfByPredicate([OldId](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == OldId; }));
	const FSharedLockerViewEntry* OldEntry = FindEntry(OldId);
	const int32 OldStored = OldEntry ? OldEntry->StoredQuantity : INDEX_NONE;

	Entries = BoundGameState.IsValid() ? BoundGameState->GetSharedLockerItems() : TArray<FSharedLockerViewEntry>();
	const TArray<FSharedLockerViewEntry> Visible = GetVisibleEntries();
	if (Visible.ContainsByPredicate([OldId](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == OldId; }))
	{
		SelectedId = OldId;
	}
	else
	{
		SelectedId = Visible.Num() > 0 ? Visible[FMath::Min(OldIndex, Visible.Num() - 1)].ProductId : NAME_None;
	}
	const bool bRemoved = OldId != NAME_None && OldId != SelectedId;

	// 확인 중 대상이 사라지면 확인을 닫는다(다른 항목으로 요청을 옮기지 않음). 수량만 바뀌면 문구·허용 여부만 갱신.
	if (bConfirmOpen && !FindEntry(ConfirmId))
	{
		bConfirmOpen = false;
		HitLayer = 0;
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	}

	RefreshAll(bRemoved);
	RefreshConfirm();

	const FSharedLockerViewEntry* NewEntry = FindEntry(SelectedId);
	if (!bRemoved && NewEntry && OldStored != INDEX_NONE && NewEntry->StoredQuantity != OldStored)
	{
		// #storedBlock.changed: 라임 바탕/짙은 숫자 → 기본, 400ms ease-out
		if (FSupplyAnimator* Animator = GetAnimator())
		{
			TWeakObjectPtr<USupplyLockerPage> WeakThis = this;
			Animator->Play(LockerStoredTrack, 0.f, .4f, [WeakThis](float T)
			{
				USupplyLockerPage* Page = WeakThis.Get();
				if (!Page)
				{
					return;
				}
				const float E = FSupplyEase::EaseOut().Eval(T);
				FLinearColor Back = Lime();
				Back.A = 1.f - E;
				Page->StoredBlock->SetBrushColor(Back);
				const FLinearColor ValueColor = Page->bExpedition ? Lime() : Paper();
				Page->StoredText->SetColorAndOpacity(FSlateColor(FMath::Lerp(Ink(), ValueColor, E)));
				Page->StoredLabel->SetColorAndOpacity(FSlateColor(FMath::Lerp(Ink(), Muted(), E)));
			});
		}
	}

	if (bRemoved)
	{
		SetStatus(SelectedId != NAME_None
			? LOCTEXT("RemovedNext", "선택한 항목이 목록에서 사라졌습니다. 가까운 항목으로 이동했습니다.")
			: LOCTEXT("RemovedCategory", "선택한 항목이 목록에서 사라졌습니다. 분류로 돌아갑니다."), false);
		SetArea(SelectedId != NAME_None ? EArea::List : EArea::Category);
	}
	else if (!bConfirmOpen)
	{
		SetArea(Area);
	}
	else
	{
		RefreshFocus();
	}
}

void USupplyLockerPage::ReturnFromLocker()
{
	USupplyScreenWidget* Screen = GetHost();
	if (!Screen)
	{
		return;
	}
	// 로비 조회는 메인 분기의 팀 보관함으로, 탐사는 화면 종료
	if (bExpedition)
	{
		Screen->RequestClose();
	}
	else
	{
		Screen->NavigateTo(ESupplyPage::Hub, true, USupplyHubPage::FocusLocker);
	}
}

void USupplyLockerPage::SetStatus(const FText& Message, bool bError)
{
	StatusMessage = Message;
	bStatusError = bError;
	StatusText->SetText(Message);
	StatusText->SetColorAndOpacity(FSlateColor(bError ? Alert() : Hex(0xccd6e9)));
}

// ---------------------------------------------------------------------------------------------------------------------
// 입력

void USupplyLockerPage::HandleInput(ESupplyInput Input)
{
	if (bConfirmOpen)
	{
		// 확인: 좌우·Tab = 취소/요청 확인, Enter = 현재 버튼(초기 취소)
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
		// 선택 분류 → 선택 행 → (탐사) 꺼내기 → 넣기 → 나가기
		struct FStop
		{
			EArea Area;
			ESharedLockerAction Action;
		};
		TArray<FStop> Stops = { { EArea::Category, Action } };
		if (FindEntry(SelectedId))
		{
			Stops.Add({ EArea::List, Action });
			if (bExpedition)
			{
				Stops.Add({ EArea::Detail, ESharedLockerAction::Withdraw });
				Stops.Add({ EArea::Detail, ESharedLockerAction::Deposit });
			}
		}
		Stops.Add({ EArea::Utility, Action });
		int32 Current = Stops.IndexOfByPredicate([this](const FStop& Stop) { return Stop.Area == Area && (Area != EArea::Detail || Stop.Action == Action); });
		Current = FMath::Max(0, Current);
		const FStop& Next = Stops[(Current + (Input == ESupplyInput::TabNext ? 1 : -1) + Stops.Num()) % Stops.Num()];
		Action = Next.Action;
		SetArea(Next.Area);
		RefreshDetail(false);
		return;
	}

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
			if (FindEntry(SelectedId))
			{
				SetArea(EArea::List);
			}
			else
			{
				SetStatus(BoundGameState.IsValid() ? LOCTEXT("EmptyCategoryStatus", "이 분류에는 보유 아이템이 없습니다.") : LOCTEXT("StatusLoadingShort", "공유 상태를 확인 중입니다."), false);
			}
		}
		break;
	case EArea::List:
		if (Input == ESupplyInput::Up || Input == ESupplyInput::Down)
		{
			const TArray<FSharedLockerViewEntry> Visible = GetVisibleEntries();
			const int32 Index = Visible.IndexOfByPredicate([this](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == SelectedId; });
			const int32 Next = FMath::Clamp(Index + (Input == ESupplyInput::Down ? 1 : -1), 0, Visible.Num() - 1);
			if (Visible.IsValidIndex(Next))
			{
				Select(Visible[Next].ProductId);
				SetArea(EArea::List);
			}
		}
		else if (bExpedition && (Input == ESupplyInput::Left || Input == ESupplyInput::Accept))
		{
			// 탐사: 왼쪽 / Enter = 상세 조작. 로비는 Enter를 소비만 한다(조회 전용).
			SetArea(EArea::Detail);
		}
		break;
	case EArea::Detail:
		if (Input == ESupplyInput::Left || Input == ESupplyInput::Right)
		{
			SetAction(Action == ESharedLockerAction::Withdraw ? ESharedLockerAction::Deposit : ESharedLockerAction::Withdraw);
		}
		else if (Input == ESupplyInput::Up)
		{
			SetArea(EArea::List);
		}
		else if (Input == ESupplyInput::Accept)
		{
			OpenConfirm(Action);
		}
		break;
	case EArea::Utility:
		if (Input == ESupplyInput::Accept)
		{
			ReturnFromLocker();
		}
		break;
	}
}

void USupplyLockerPage::HandleBack()
{
	// 탐사: 확인 → 상세 → 목록 → 분류 → 닫힘 / 로비: 목록 → 분류 → 메인 분기
	if (bConfirmOpen)
	{
		CancelConfirm();
		return;
	}
	switch (Area)
	{
	case EArea::Detail:
		SetArea(EArea::List);
		break;
	case EArea::List:
		SetArea(EArea::Category);
		break;
	case EArea::Utility:
		SetArea(FindEntry(SelectedId) ? EArea::List : EArea::Category);
		break;
	default:
		ReturnFromLocker();
		break;
	}
}

void USupplyLockerPage::OnViewSizeChanged(const FVector2D& ViewSize)
{
	const float FrameWidth = FMath::Min(ViewSize.X, 1920.f);
	if (UOverlaySlot* FrameSlot = PageFrame ? Cast<UOverlaySlot>(PageFrame->Slot) : nullptr)
	{
		FrameSlot->SetPadding(FMargin(FMath::Max(0.f, (ViewSize.X - 1920.f) * .5f), 0.f));
	}
	if (UCanvasPanelSlot* BandSlot = BackBand ? Cast<UCanvasPanelSlot>(BackBand->Slot) : nullptr)
	{
		// width 110vw, height 36vh, top 24%, left -4%
		BandSlot->SetPosition(FVector2D(-.04f * FrameWidth, .24f * ViewSize.Y));
		BandSlot->SetSize(FVector2D(1.1f * ViewSize.X, .36f * ViewSize.Y));
	}
	if (ArtBoard)
	{
		ArtBoard->SetHeightOverride(Css(FMath::Clamp(ViewSize.Y * .27f, 170.f, 310.f), 100.f));
	}
	if (RowsFrame)
	{
		RowsFrame->SetMaxDesiredHeight(ViewSize.Y * Css(.44f, .41f));
	}
	if (IsCompact())
	{
		// padding 20px 3vw — 폭이 바뀌면 다시
		PagePadding = FMargin(ViewSize.X * .03f, 20.f);
		if (UOverlaySlot* ColumnSlot = PageColumn ? Cast<UOverlaySlot>(PageColumn->Slot) : nullptr)
		{
			ColumnSlot->SetPadding(PagePadding);
		}
	}
}

void USupplyLockerPage::OnPageRebuilt()
{
	// OnPageShown의 진입 초기화(선택·분류 리셋)는 하지 않고, 들고 있던 상태로 새 트리를 다시 그린다
	ViewContextText->SetText(bExpedition ? LOCTEXT("ContextExpedition", "탐사 · 아이템 박스") : LOCTEXT("ContextLobby", "로비 · 조회 전용"));
	TransferBlock->SetVisibility(bExpedition ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	NoteSlot->SetPadding(FMargin(0.f, bExpedition ? Css(10.f, 6.f) : 0.f, 0.f, 0.f));
	SetStatus(StatusMessage, bStatusError);
	RefreshAll(false);
	if (bConfirmOpen)
	{
		HitLayer = LockerConfirmLayerIndex;
		ConfirmLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
		RefreshConfirm();
	}
}

void USupplyLockerPage::PlayOpeningLift()
{
	// 탐사 직접 진입: 상단 200 ease-out +170, 분류 180 +210, 상세·목록 210 +240, 하단 180 +280
	PlayLift(Masthead, TEXT("LockerLift.Head"), 170.f, 200.f, false);
	PlayLift(CategoryBar, TEXT("LockerLift.Categories"), 210.f, 180.f, false);
	PlayLift(DetailColumn, TEXT("LockerLift.Detail"), 240.f, 210.f, true);
	PlayLift(ManifestColumn, TEXT("LockerLift.Manifest"), 240.f, 210.f, true);
	PlayLift(FooterRow, TEXT("LockerLift.Foot"), 280.f, 180.f, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// 그리기

void USupplyLockerPage::RefreshAll(bool bDetailChanged)
{
	RefreshCategories();
	RefreshRows();
	RefreshDetail(bDetailChanged);
	RefreshFocus();
}

void USupplyLockerPage::RefreshCategories()
{
	const bool bReady = BoundGameState.IsValid();
	const TArray<FSharedLockerViewEntry> Owned = GetOwnedEntries();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FCategoryWidgets& Widgets = Categories[Index];
		const bool bPressed = Index == Category;
		Widgets.Face->SetBrushColor(bPressed ? Paper() : FLinearColor::Transparent);
		// skewX(-8deg), 글자는 skewX(8deg)로 바로 세운다
		Widgets.Card->SetRenderShear(FVector2D(bPressed ? -8.f : 0.f, 0.f));
		Widgets.Content->SetRenderShear(FVector2D(bPressed ? 8.f : 0.f, 0.f));
		for (UTextBlock* Text : Widgets.Texts)
		{
			Text->SetColorAndOpacity(FSlateColor(bPressed ? Ink() : Muted()));
		}
		int32 Count = 0;
		for (const FSharedLockerViewEntry& Entry : Owned)
		{
			Count += IsCategoryMatch(Index, Entry) ? 1 : 0;
		}
		Widgets.Count->SetText(bReady ? FText::AsNumber(Count) : Dash());
	}

	static const TCHAR* Titles[3] = { TEXT("01 / ALL EQUIPMENT"), TEXT("02 / PERMANENT"), TEXT("03 / CONSUMABLE") };
	CategoryTitleText->SetText(FText::FromString(Titles[Category]));
	TotalText->SetText(bReady ? FText::Format(LOCTEXT("Total", "{0} 품목 · 팀 공유"), FText::AsNumber(Owned.Num())) : LOCTEXT("TotalLoading", "공유 상태\n확인 중"));
}

void USupplyLockerPage::RefreshRows()
{
	const TArray<FSharedLockerViewEntry> Visible = GetVisibleEntries();
	if (WidgetTree)
	{
		const FBuilder B(WidgetTree);
		while (Rows.Num() < Visible.Num())
		{
			AddV(RowsList, BuildRow(B), FMargin(0.f, Rows.Num() == 0 ? 0.f : Css(8.f, 6.f), 0.f, 0.f));
		}
	}

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		FRowWidgets& Row = Rows[Index];
		if (!Visible.IsValidIndex(Index))
		{
			Row.ProductId = NAME_None;
			Row.Root->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		const FSharedLockerViewEntry& Entry = Visible[Index];
		Row.ProductId = Entry.ProductId;
		Row.Root->SetVisibility(ESlateVisibility::HitTestInvisible);

		const bool bSelected = Entry.ProductId == SelectedId;
		const FLinearColor TextColor = bSelected ? Ink() : Paper();
		Row.Face->SetBrushColor(bSelected ? Paper() : FLinearColor::Transparent);
		Row.Index->SetText(TwoDigits(Index + 1));
		Row.Name->SetText(GetEntryName(Entry));
		Row.Kind->SetText(Entry.Lifetime == EItemShopItemLifetime::Permanent ? LOCTEXT("KindPermanent", "장비") : LOCTEXT("KindConsumable", "소모품"));
		Row.Quantity->SetText(FText::Format(LOCTEXT("RowCounts", "{0} / {1}"), FText::AsNumber(Entry.StoredQuantity), FText::AsNumber(Entry.OwnedQuantity)));
		Row.State->SetText(Entry.StoredQuantity == 0 ? LOCTEXT("AllOut", "모두 꺼냄") : LOCTEXT("InStore", "보관 중"));
		for (UTextBlock* Text : { Row.Index.Get(), Row.Name.Get(), Row.Kind.Get(), Row.Quantity.Get(), Row.State.Get() })
		{
			Text->SetColorAndOpacity(FSlateColor(TextColor));
		}
	}

	const bool bEmpty = Visible.Num() == 0;
	RowsFrame->SetVisibility(bEmpty ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	EmptyBox->SetVisibility(bEmpty ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const bool bReady = BoundGameState.IsValid();
	EmptyTitle->SetText(bReady ? LOCTEXT("EmptyTitle", "이 분류에는 보유 아이템이 없습니다") : LOCTEXT("EmptyLoading", "보관함을 확인합니다"));
	EmptyBody->SetText(bReady ? LOCTEXT("EmptyBody", "분류를 바꾸거나 보관함을 닫을 수 있습니다.") : LOCTEXT("EmptyLoadingBody", "공유 장비 상태를 불러오는 중입니다."));

	const int32 Position = Visible.IndexOfByPredicate([this](const FSharedLockerViewEntry& Entry) { return Entry.ProductId == SelectedId; });
	PositionText->SetText(Position != INDEX_NONE
		? FText::Format(LOCTEXT("Position", "{0} / {1}"), TwoDigits(Position + 1), TwoDigits(Visible.Num()))
		: FText::FromString(TEXT("— / —")));
}

void USupplyLockerPage::RefreshDetail(bool bChanged)
{
	const FSharedLockerViewEntry* Entry = FindEntry(SelectedId);
	const FItemShopProduct* Product = Entry ? FindProduct(Entry->ProductId) : nullptr;
	const bool bReady = BoundGameState.IsValid();

	if (Entry)
	{
		KindText->SetText(Entry->Lifetime == EItemShopItemLifetime::Permanent ? LOCTEXT("KindLongPermanent", "장비 / PERMANENT") : LOCTEXT("KindLongConsumable", "소모품 / CONSUMABLE"));
		NameText->SetText(GetEntryName(*Entry));
		DescriptionText->SetText(Product && !Product->ShopDescription.IsEmpty() ? Product->ShopDescription : LOCTEXT("NoDescription", "상품 설명이 등록되지 않았습니다. 식별 번호로 표시합니다."));
		Art->SetArt(Product ? Product->ShopArt : EItemShopArt::Unknown);
		StoredText->SetText(FText::AsNumber(Entry->StoredQuantity));
		CheckedOutText->SetText(FText::AsNumber(Entry->OwnedQuantity - Entry->StoredQuantity));
		OwnedText->SetText(FText::AsNumber(Entry->OwnedQuantity));
		const TArray<FSharedLockerViewEntry> Visible = GetVisibleEntries();
		const int32 Position = Visible.IndexOfByPredicate([this](const FSharedLockerViewEntry& It) { return It.ProductId == SelectedId; });
		ArtIndexText->SetText(TwoDigits(Position + 1));
	}
	else
	{
		KindText->SetText(LOCTEXT("KindShared", "SHARED EQUIPMENT"));
		NameText->SetText(bReady ? LOCTEXT("NoItem", "보유 아이템 없음") : LOCTEXT("Waiting", "수신 대기"));
		DescriptionText->SetText(bReady ? LOCTEXT("OtherCategory", "다른 분류에서 팀 보유 아이템을 확인할 수 있습니다.") : LOCTEXT("Loading", "공유 상태를 불러오는 중입니다."));
		Art->SetArt(EItemShopArt::Unknown);
		StoredText->SetText(Dash());
		CheckedOutText->SetText(Dash());
		OwnedText->SetText(Dash());
		ArtIndexText->SetText(FText::FromString(TEXT("00")));
	}
	// 내 소지 = 내 핫바에 있는, 보관함에서 꺼낸 같은 상품(넣기 가능 수)
	HeldText->SetText(Entry ? FText::AsNumber(GetHeld(Entry->ProductId)) : Dash());

	// 로비 조회 전용은 첫 칸도 기본 글자색(시안 .readonly)
	StoredText->SetColorAndOpacity(FSlateColor(bExpedition ? Lime() : Paper()));
	StoredBlock->SetBrushColor(FLinearColor::Transparent);
	StoredLabel->SetColorAndOpacity(FSlateColor(Muted()));

	ActionNoteText->SetText(bExpedition ? GetTransferReason(Action, SelectedId) : LOCTEXT("ReadOnlyNote", "조회 전용 · 장비는 탐사 맵의 아이템 박스에서 꺼낼 수 있습니다."));
	if (bExpedition)
	{
		RefreshTransfer();
	}

	if (bChanged)
	{
		PlayDetailChange();
	}
}

void USupplyLockerPage::PlayDetailChange()
{
	FSupplyAnimator* Animator = GetAnimator();
	if (!Animator)
	{
		return;
	}
	// .detail.changing: 글 X -8→0 / opacity .65→1 130ms, 도해 Y 14→0 / opacity .35→1 160ms
	TWeakObjectPtr<UWidget> Copy = DetailCopy.Get();
	Animator->Play(LockerDetailTrack, 0.f, .13f, [Copy](float T)
	{
		if (UWidget* Widget = Copy.Get())
		{
			const float E = FSupplyEase::Snap().Eval(T);
			Widget->SetRenderOpacity(FMath::Lerp(.65f, 1.f, E));
			Widget->SetRenderTranslation(FVector2D(-8.f * (1.f - E), 0.f));
		}
	});
	TWeakObjectPtr<UWidget> ArtMotion = Art ? Art->GetMotionTarget() : nullptr;
	Animator->Play(LockerArtTrack, 0.f, .16f, [ArtMotion](float T)
	{
		if (UWidget* Widget = ArtMotion.Get())
		{
			const float E = FSupplyEase::Snap().Eval(T);
			Widget->SetRenderOpacity(FMath::Lerp(.35f, 1.f, E));
			Widget->SetRenderTranslation(FVector2D(0.f, 14.f * (1.f - E)));
		}
	});
}

void USupplyLockerPage::RefreshFocus()
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		ShowIf(Categories[Index].Ring, Area == EArea::Category && Index == Category);
	}
	ShowIf(CategoryCaret, Area == EArea::Category);
	for (FRowWidgets& Row : Rows)
	{
		const bool bFocused = Area == EArea::List && Row.ProductId != NAME_None && Row.ProductId == SelectedId;
		ShowIf(Row.Ring, bFocused);
		ShowIf(Row.Cursor, bFocused);
	}
	ShowIf(ExitRing, !bConfirmOpen && Area == EArea::Utility);

	// 탐사 상세: 선택 동작에 ▶ + 라임 외곽선, 상세에 포커스가 있으면 구역 표식 라임
	const bool bDetail = !bConfirmOpen && Area == EArea::Detail;
	ShowIf(WithdrawButton.FocusRing, bDetail && Action == ESharedLockerAction::Withdraw);
	ShowIf(WithdrawButton.Cursor, bDetail && Action == ESharedLockerAction::Withdraw);
	ShowIf(DepositButton.FocusRing, bDetail && Action == ESharedLockerAction::Deposit);
	ShowIf(DepositButton.Cursor, bDetail && Action == ESharedLockerAction::Deposit);
	for (UTextBlock* Tag : ZoneTags)
	{
		Tag->SetColorAndOpacity(FSlateColor(bDetail || bConfirmOpen ? Lime() : Muted()));
	}
	ShowIf(CancelButton.FocusRing, bConfirmOpen && ConfirmFocus == 0);
	ShowIf(CommitButton.FocusRing, bConfirmOpen && ConfirmFocus == 1);

	// .help: kbd 칩 + 설명(gap 20)
	TArray<TPair<FText, FText>> Hints;
	switch (Area)
	{
	case EArea::Category:
		Hints = {
			{ FText::FromString(TEXT("← →")), LOCTEXT("HintCategory", "분류") },
			{ FText::FromString(TEXT("↓ / Enter")), LOCTEXT("HintToList", "목록") },
			{ FText::FromString(TEXT("Esc")), bExpedition ? LOCTEXT("HintClose", "닫기") : LOCTEXT("HintHub", "메인 분기") } };
		break;
	case EArea::List:
		Hints = { { FText::FromString(TEXT("↑ ↓")), LOCTEXT("HintSelect", "선택") } };
		if (bExpedition)
		{
			Hints.Add({ FText::FromString(TEXT("← / Enter")), LOCTEXT("HintDetail", "상세·조작") });
		}
		Hints.Add({ FText::FromString(TEXT("Esc")), LOCTEXT("HintToCategory", "분류") });
		break;
	case EArea::Detail:
		Hints = {
			{ FText::FromString(TEXT("← →")), LOCTEXT("HintActions", "꺼내기 / 넣기") },
			{ FText::FromString(TEXT("Enter")), LOCTEXT("HintConfirm", "선택 동작 확인") },
			{ FText::FromString(TEXT("Esc")), LOCTEXT("HintToList", "목록") } };
		break;
	default:
		Hints = {
			{ FText::FromString(TEXT("Enter")), LOCTEXT("HintRun", "실행") },
			{ FText::FromString(TEXT("Esc")), LOCTEXT("HintPrev", "이전 단계") } };
		break;
	}

	if (HelpBox && WidgetTree)
	{
		HelpBox->ClearChildren();
		const FBuilder B(WidgetTree);
		for (int32 Index = 0; Index < Hints.Num(); ++Index)
		{
			// kbd: 800 11px, border 1px #7b8aa0, box-shadow 0 2px 0 #48556a, padding 2 5, margin-right 6
			UOverlay* Chip = B.Overlay();
			UBorder* ChipShadow = B.Fill(Hex(0x48556a));
			ChipShadow->SetRenderTranslation(FVector2D(0.f, 2.f));
			AddFill(Chip, ChipShadow);
			AddFill(Chip, B.Fill(Ink()));
			AddFill(Chip, B.Ring(Hex(0x7b8aa0), 1.f));
			AddOverlay(Chip, B.LineBox(B.Text(Hints[Index].Key, Font(EFace::ExtraBold, 11.f), Paper()), -1.f, ERef::Segoe), HAlign_Fill, VAlign_Fill, FMargin(6.f, 3.f));
			AddH(HelpBox, Chip, FMargin(Index == 0 ? 0.f : 20.f, 0.f, 6.f, 0.f), VAlign_Center);
			AddH(HelpBox, B.LineBox(B.Text(Hints[Index].Value, Font(EFace::Regular, Css(12.f, 11.f)), Muted())), FMargin(0.f), VAlign_Center);
		}
	}
}

#undef LOCTEXT_NAMESPACE
