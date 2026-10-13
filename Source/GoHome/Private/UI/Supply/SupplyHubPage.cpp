#include "UI/Supply/SupplyHubPage.h"

#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/Supply/SupplyScreenWidget.h"

#define LOCTEXT_NAMESPACE "SupplyHub"

using namespace SupplyUI;

const FName USupplyHubPage::FocusShop(TEXT("Shop"));
const FName USupplyHubPage::FocusLocker(TEXT("Locker"));

UWidget* USupplyHubPage::BuildPage(const FBuilder& B)
{
	// .entry-screen: padding clamp(24px,5vw,80px) = 80, gap 30, ink 바탕 + 코발트 사선 띠(left -15% top 30% 130%×32%, -12°)
	PagePadding = FMargin(80.f);

	UOverlay* Root = B.Overlay();
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	AddFill(Root, B.Fill(Ink()));
	AddFill(Root, MakeBand(B, Blue(), -0.15f, 0.30f, 1.15f, 0.62f, -12.f));

	UVerticalBox* Column = B.VBox();
	AddOverlay(Root, Column, HAlign_Fill, VAlign_Fill, PagePadding);

	// --- masthead: kicker/wordmark ↔ 모드 표식
	UHorizontalBox* Head = B.HBox();
	Masthead = Head;
	{
		UVerticalBox* Brand = B.VBox();
		UTextBlock* Kicker = B.Text(LOCTEXT("Kicker", "LOBBY / SUPPLY ACCESS"), Font(EFace::Bold, 11.f, .18f), Lime());
		AddV(Brand, B.LineBox(Kicker, 16.5f, ERef::Segoe), FMargin(0.f, 0.f, 0.f, 7.f));
		UTextBlock* Wordmark = B.Text(LOCTEXT("Wordmark", "GOHOME"), Font(EFace::Display, 48.f, -.08f, true), Paper());
		AddV(Brand, B.LineBox(Wordmark, 45.6f, ERef::ArialBlack));
		AddH(Head, Brand, FMargin(0.f), VAlign_Top);

		AddH(Head, B.Spacer(24.f, 0.f), FMargin(0.f), VAlign_Top, 1.f);

		UBorder* Tag = B.Fill(Line(), FMargin(1.f));
		UBorder* TagInner = B.Fill(Ink(), FMargin(9.f, 6.f));
		TagInner->SetContent(B.LineBox(B.Text(LOCTEXT("ModeTag", "로비 · 보급 오브젝트"), Font(EFace::Regular, 11.f, .07f), Muted()), -1.f));
		Tag->SetContent(TagInner);
		AddH(Head, Tag, FMargin(0.f), VAlign_Top);
	}
	AddV(Column, Head);

	// --- body: 1.1fr / 8vw / 1fr, 세로 가운데
	UHorizontalBox* Body = B.HBox();
	{
		UVerticalBox* Left = B.VBox();
		TitleBlock = Left;
		UTextBlock* Team = B.Text(LOCTEXT("Team", "TEAM"), Font(EFace::Display, 150.f, -.09f, true), Paper());
		UTextBlock* Ready = B.Text(LOCTEXT("Ready", "READY."), Font(EFace::Display, 150.f, -.09f, true), Lime());
		AddV(Left, B.LineBox(Team, 126.f, ERef::ArialBlack));
		AddV(Left, B.LineBox(Ready, 126.f, ERef::ArialBlack));

		UTextBlock* Copy = B.Text(LOCTEXT("Copy", "다음 잠수를 준비합니다.\n필요한 보급을 찾고, 팀의 장비를 확인하세요."), Font(EFace::Regular, 14.f), Paper());
		CopyBlock = B.LineBox(Copy, 14.f * 1.7f);
		AddV(Left, Copy, FMargin(0.f, 28.f, 0.f, 0.f));
		AddH(Body, Left, FMargin(0.f), VAlign_Center, 1.1f);

		ColumnGap = Cast<USpacer>(B.Spacer(153.6f, 0.f));
		AddH(Body, ColumnGap);

		UVerticalBox* Right = B.VBox();
		AddV(Right, BuildDestination(B, 0, LOCTEXT("N1", "01"), LOCTEXT("Shop", "보급 상점"), LOCTEXT("ShopCaption", "팀 장비 구매 · 보관함으로 보급")));
		AddV(Right, BuildDestination(B, 1, LOCTEXT("N2", "02"), LOCTEXT("Locker", "팀 보관함"), LOCTEXT("LockerCaption", "팀 보유 장비 조회")), FMargin(0.f, 18.f, 0.f, 0.f));
		AddH(Body, Right, FMargin(0.f), VAlign_Center, 1.f);
	}
	AddV(Column, Body, FMargin(0.f, 30.f), HAlign_Fill, true);

	// --- footer
	// 시안 하단 줄 높이 26(오른쪽 시험 도구 버튼 높이) — 버튼은 옮기지 않지만 배치는 유지한다
	USizeBox* Foot = B.Size(-1.f, 26.f);
	Footer = Foot;
	UHorizontalBox* Hints = B.HBox();
	FillKeyHints(B, Hints, {
		{ { TEXT("WS"), TEXT("↑↓") }, LOCTEXT("HintSelect", "선택") },
		{ { TEXT("Space"), TEXT("Enter") }, LOCTEXT("HintEnter", "진입") },
		{ { TEXT("Esc") }, LOCTEXT("HintClose", "닫기") } }, 12.f);
	SetContent(Foot, Hints, HAlign_Left, VAlign_Top);
	AddV(Column, Foot);

	SetFocused(0);
	return Root;
}

UWidget* USupplyHubPage::BuildDestination(const FBuilder& B, int32 Index, const FText& Number, const FText& Title, const FText& Caption)
{
	FDestination& Dest = Destinations[Index];

	// 바깥: 오프닝 등장 이동 / 안쪽 Overlay: 선택 회전(받침·외곽선 포함)
	UBorder* Lift = B.Box();
	Dest.Lift = Lift;

	UOverlay* Card = B.Overlay();
	Card->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Dest.Card = Card;
	Lift->SetContent(Card);

	// box-shadow -10px 8px 0 lime
	UBorder* Shadow = B.Fill(Lime());
	Shadow->SetRenderTranslation(FVector2D(-10.f, 8.f));
	Dest.Shadow = Shadow;
	AddFill(Card, Shadow);

	// .destination: padding 25 22, border-bottom 2px paper
	UBorder* Face = B.Fill(Paper());
	Dest.Face = Face;
	UVerticalBox* FaceColumn = B.VBox();
	Face->SetContent(FaceColumn);
	AddFill(Card, Face);

	UHorizontalBox* Row = B.HBox();
	AddV(FaceColumn, Row, FMargin(22.f, 25.f));
	AddV(FaceColumn, B.Fill(Paper(), FMargin(0.f, 1.f)));

	UTextBlock* NumberText = B.Text(Number, Font(EFace::Black, 24.f, 0.f, true), Paper());
	USizeBox* NumberCell = B.Size(42.f);
	SetContent(NumberCell, B.LineBox(NumberText, -1.f, ERef::Segoe), HAlign_Left, VAlign_Center);
	AddH(Row, NumberCell, FMargin(0.f, 0.f, 14.f, 0.f), VAlign_Center);

	UVerticalBox* Words = B.VBox();
	UTextBlock* TitleText = B.Text(Title, Font(EFace::Bold, 36.f, -.05f), Paper());
	UTextBlock* CaptionText = B.Text(Caption, Font(EFace::Regular, 11.f), Paper());
	AddV(Words, B.LineBox(TitleText));
	AddV(Words, B.LineBox(CaptionText), FMargin(0.f, 8.f, 0.f, 0.f));
	AddH(Row, Words, FMargin(0.f, 0.f, 14.f, 0.f), VAlign_Center, 1.f);

	UTextBlock* Arrow = B.Text(FText::FromString(TEXT("↗")), Font(EFace::Regular, 15.f), Paper());
	USizeBox* ArrowCell = B.Size(25.f);
	SetContent(ArrowCell, B.LineBox(Arrow), HAlign_Left, VAlign_Center);
	AddH(Row, ArrowCell, FMargin(0.f), VAlign_Center);

	Dest.Texts = { NumberText, TitleText, CaptionText, Arrow };

	// button:focus outline 3px lime, offset 4px
	UWidget* Outline = B.Ring(Lime(), 3.f);
	Dest.Outline = Outline;
	AddOverlay(Card, Outline, HAlign_Fill, VAlign_Fill, FMargin(-7.f));

	AddHitTarget(Lift, [this, Index]() { Activate(Index); });
	return Lift;
}

void USupplyHubPage::SetFocused(int32 Index)
{
	FocusedIndex = FMath::Clamp(Index, 0, 1);
	for (int32 I = 0; I < 2; ++I)
	{
		FDestination& Dest = Destinations[I];
		const bool bPressed = I == FocusedIndex;
		if (Dest.Card)
		{
			Dest.Card->SetRenderTransformAngle(bPressed ? -2.f : 0.f);
		}
		if (Dest.Shadow)
		{
			Dest.Shadow->SetVisibility(bPressed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
		if (Dest.Outline)
		{
			Dest.Outline->SetVisibility(bPressed ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
		if (Dest.Face)
		{
			Dest.Face->SetBrushColor(bPressed ? Paper() : FLinearColor::Transparent);
		}
		for (UTextBlock* Text : Dest.Texts)
		{
			Text->SetColorAndOpacity(FSlateColor(bPressed ? Ink() : Paper()));
		}
	}
}

void USupplyHubPage::Activate(int32 Index)
{
	SetFocused(Index);
	if (USupplyScreenWidget* Screen = GetHost())
	{
		Screen->NavigateTo(Index == 0 ? ESupplyPage::Shop : ESupplyPage::Locker, false);
	}
}

void USupplyHubPage::OnPageShown(bool bBack, FName FocusHint)
{
	if (FocusHint == FocusLocker)
	{
		SetFocused(1);
	}
	else if (FocusHint == FocusShop || !bBack)
	{
		SetFocused(0);
	}
}

void USupplyHubPage::HandleInput(ESupplyInput Input)
{
	switch (Input)
	{
	case ESupplyInput::Up:
	case ESupplyInput::Down:
	case ESupplyInput::TabNext:
	case ESupplyInput::TabPrev:
		// 두 목적지 사이 순환(시안 cycle)
		SetFocused(1 - FocusedIndex);
		break;
	case ESupplyInput::Accept:
		Activate(FocusedIndex);
		break;
	default:
		break;
	}
}

void USupplyHubPage::HandleBack()
{
	if (USupplyScreenWidget* Screen = GetHost())
	{
		Screen->RequestClose();
	}
}

void USupplyHubPage::OnViewSizeChanged(const FVector2D& ViewSize)
{
	if (ColumnGap)
	{
		ColumnGap->SetSize(FVector2D(ViewSize.X * .08f, 0.f));
	}
}

void USupplyHubPage::PlayOpeningLift()
{
	// motion.md 오프닝: 상단 200 ease-out +170, 제목·설명 260 +250, 목적지 220 +340/+405, 하단 180 ease-out +280
	PlayLift(Masthead, TEXT("HubLift.Head"), 170.f, 200.f, false);
	PlayLift(TitleBlock, TEXT("HubLift.Title"), 250.f, 260.f, true);
	PlayLift(Destinations[0].Lift, TEXT("HubLift.Shop"), 340.f, 220.f, true);
	PlayLift(Destinations[1].Lift, TEXT("HubLift.Locker"), 405.f, 220.f, true);
	PlayLift(Footer, TEXT("HubLift.Foot"), 280.f, 180.f, false);
}

#undef LOCTEXT_NAMESPACE
