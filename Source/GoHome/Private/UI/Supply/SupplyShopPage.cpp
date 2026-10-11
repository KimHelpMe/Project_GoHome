#include "UI/Supply/SupplyShopPage.h"

#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/Supply/SupplyHubPage.h"
#include "UI/Supply/SupplyScreenWidget.h"

#define LOCTEXT_NAMESPACE "SupplyShop"

using namespace SupplyUI;

UWidget* USupplyShopPage::BuildPage(const FBuilder& B)
{
	// .supply: padding clamp(20px,3vw,54px) = 54, gap 20, 남청 사선 면 inset 25% -10% 20% / -8°
	PagePadding = FMargin(54.f);

	UOverlay* Root = B.Overlay();
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	AddFill(Root, B.Fill(Ink()));
	AddFill(Root, MakeBand(B, Navy(), -0.10f, 0.25f, 1.10f, 0.80f, -8.f));

	UVerticalBox* Column = B.VBox();
	AddOverlay(Root, Column, HAlign_Fill, VAlign_Fill, PagePadding);

	Masthead = BuildMasthead(B);
	AddV(Column, Masthead);

	// 본문(카탈로그/상세)은 supply-ui-2
	AddV(Column, B.Spacer(0.f, 0.f), FMargin(0.f, 20.f), HAlign_Fill, true);

	Footer = BuildFooter(B);
	AddV(Column, Footer);
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
		UVerticalBox* Wallet = B.VBox();
		AddV(Wallet, B.Text(LOCTEXT("FundsLabel", "팀 공동 자금"), Font(EFace::Regular, 11.f), Lime()), FMargin(16.f, 5.f, 16.f, 0.f), HAlign_Right);
		FundsText = B.Text(FText::FromString(TEXT("—")), Font(EFace::Black, 32.f, 0.f, true), Paper());
		AddV(Wallet, B.LineBox(FundsText, 38.4f, ERef::Segoe), FMargin(16.f, 0.f, 16.f, 5.f), HAlign_Right);
		AddV(Wallet, B.Fill(Lime(), FMargin(0.f, 1.5f)));
		AddH(Side, Wallet, FMargin(0.f, 0.f, 24.f, 0.f), VAlign_Center);

		UVerticalBox* Exit = B.VBox();
		UHorizontalBox* ExitRow = B.HBox();
		AddH(ExitRow, B.Text(LOCTEXT("Exit", "메인 분기"), Font(EFace::Regular, 13.f), Paper()), FMargin(0.f), VAlign_Center);
		AddH(ExitRow, B.Text(FText::FromString(TEXT("↗")), Font(EFace::Bold, 22.f), Paper()), FMargin(10.f, 0.f, 0.f, 0.f), VAlign_Center);
		AddV(Exit, ExitRow, FMargin(12.f, 10.f, 0.f, 10.f));
		AddV(Exit, B.Fill(Paper(), FMargin(0.f, 0.5f)));
		AddH(Side, Exit, FMargin(0.f), VAlign_Center);
		AddHitTarget(Exit, [this]() { ReturnToHub(); });
	}
	AddH(Head, Side, FMargin(0.f), VAlign_Top);
	return Head;
}

UWidget* USupplyShopPage::BuildFooter(const FBuilder& B)
{
	UVerticalBox* Foot = B.VBox();
	AddV(Foot, B.Fill(Line(), FMargin(0.f, 0.5f)));
	UVerticalBox* Texts = B.VBox();
	StatusText = B.Text(FText::GetEmpty(), Font(EFace::Regular, 12.f), Paper());
	AddV(Texts, B.LineBox(StatusText, 18.f));
	HintText = B.Text(LOCTEXT("HintCategory", "← → 분류　↓ / Enter 목록　Esc 메인 분기"), Font(EFace::Regular, 11.f), Muted());
	AddV(Texts, HintText, FMargin(0.f, 8.f, 0.f, 0.f));
	AddV(Foot, Texts, FMargin(0.f, 12.f, 0.f, 0.f));
	return Foot;
}

void USupplyShopPage::OnPageShown(bool bBack, FName FocusHint)
{
}

void USupplyShopPage::HandleInput(ESupplyInput Input)
{
}

void USupplyShopPage::HandleBack()
{
	ReturnToHub();
}

void USupplyShopPage::ReturnToHub()
{
	if (USupplyScreenWidget* Screen = GetHost())
	{
		Screen->NavigateTo(ESupplyPage::Hub, true, USupplyHubPage::FocusShop);
	}
}

#undef LOCTEXT_NAMESPACE
