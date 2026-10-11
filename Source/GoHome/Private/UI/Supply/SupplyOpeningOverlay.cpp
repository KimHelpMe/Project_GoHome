#include "UI/Supply/SupplyOpeningOverlay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyUIStyle.h"

#define LOCTEXT_NAMESPACE "SupplyOpening"

using namespace SupplyUI;

namespace
{
	const FName CoralTrack(TEXT("Opening.Coral"));
	const FName CardTrack(TEXT("Opening.Card"));
	const FName FadeTrack(TEXT("Opening.Fade"));

	// 화면 대비 판 크기(CSS inset 음수 %)
	constexpr float CoralWidthScale = 1.28f;	// inset -26% -14%
	constexpr float CardWidthScale = 1.32f;		// inset -24% -16%

	UCanvasPanelSlot* StretchInCanvas(UCanvasPanel* Canvas, UWidget* Child, float MinX, float MinY, float MaxX, float MaxY)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
		Slot->SetAnchors(FAnchors(MinX, MinY, MaxX, MaxY));
		Slot->SetOffsets(FMargin(0.f));
		return Slot;
	}

	FSlateBrush CircleBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.OutlineSettings.Width = 0.f;
		return Brush;
	}
}

void USupplyOpeningOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const FBuilder B(WidgetTree);
	UCanvasPanel* Root = B.Canvas();
	Root->SetClipping(EWidgetClipping::ClipToBounds);
	WidgetTree->RootWidget = Root;

	// :before — 코발트 선행 면, -10°
	UBorder* CoralPlate = B.Fill(Blue());
	CoralPlate->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	StretchInCanvas(Root, CoralPlate, -0.14f, -0.26f, 1.14f, 1.26f);
	Coral = CoralPlate;

	// .sophia-intro-card — 아이보리 카드, 8px 코발트 테두리, padding 8%(=화면 폭), -9°
	UBorder* CardFrame = B.Fill(Blue(), FMargin(8.f));
	UBorder* CardFace = B.Fill(Ivory(), FMargin(153.6f));
	CardFrame->SetContent(CardFace);
	CardFrame->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	StretchInCanvas(Root, CardFrame, -0.16f, -0.24f, 1.16f, 1.24f);
	Card = CardFrame;

	UHorizontalBox* Content = B.HBox();
	SetContent(CardFace, Content, HAlign_Center, VAlign_Center);

	// 3×3 도트(58px, 간격 28), 가운데 라임, -7°
	UUniformGridPanel* Dots = WidgetTree->ConstructWidget<UUniformGridPanel>();
	Dots->SetSlotPadding(FMargin(14.f));
	Dots->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	Dots->SetRenderTransformAngle(-7.f);
	for (int32 Index = 0; Index < 9; ++Index)
	{
		USizeBox* DotSize = B.Size(58.f, 58.f);
		UBorder* Dot = B.Box();
		Dot->SetBrush(CircleBrush(Index == 4 ? Lime() : Blue()));
		DotSize->AddChild(Dot);
		UUniformGridSlot* DotSlot = Dots->AddChildToUniformGrid(DotSize, Index / 3, Index % 3);
		DotSlot->SetHorizontalAlignment(HAlign_Center);
		DotSlot->SetVerticalAlignment(VAlign_Center);
	}
	// 바깥 여백 14를 상쇄해 CSS gap과 같은 외곽을 만든다
	AddH(Content, Dots, FMargin(-14.f, -14.f, 72.f - 14.f, -14.f), VAlign_Center);

	UVerticalBox* Stamp = B.VBox();
	AddV(Stamp, B.LineBox(B.Text(LOCTEXT("Channel", "GOHOME · SUPPLY CHANNEL"), Font(EFace::Black, 16.f, .22f), Blue()), -1.f, ERef::Segoe));
	AddV(Stamp, B.LineBox(B.Text(LOCTEXT("Title", "잠수정 보급"), Font(EFace::Bold, 80.f, -.06f), Ink()), 78.4f, ERef::Segoe), FMargin(0.f, 8.f, 0.f, 0.f));
	AddV(Stamp, B.Size(64.f, 8.f), FMargin(0.f, 8.f, 0.f, 0.f), HAlign_Left);
	Cast<USizeBox>(Stamp->GetChildAt(2))->AddChild(B.Fill(Blue()));
	AddH(Content, Stamp, FMargin(0.f), VAlign_Center);

	SetVisibility(ESlateVisibility::Collapsed);
}

FVector2D USupplyOpeningOverlay::GetViewSize() const
{
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	return Size.X > 1.f ? Size : FVector2D(1920.f, 1080.f);
}

void USupplyOpeningOverlay::ApplyCoral(float T)
{
	static const FSupplyKey Keys[] = { { 0.f, -1.12f }, { .38f, -0.04f }, { 1.f, 1.12f } };
	const float X = SampleSupplyKeys(Keys, T, FSupplyEase::Page());
	FWidgetTransform Transform;
	Transform.Angle = -10.f;
	Transform.Translation = FVector2D(X * GetViewSize().X * CoralWidthScale, 0.f);
	Coral->SetRenderTransform(Transform);
}

void USupplyOpeningOverlay::ApplyCard(float T)
{
	static const FSupplyKey Keys[] = { { 0.f, -1.18f }, { .35f, -0.02f }, { .52f, 0.f }, { 1.f, 1.18f } };
	const float X = SampleSupplyKeys(Keys, T, FSupplyEase::Page());
	FWidgetTransform Transform;
	Transform.Angle = -9.f;
	Transform.Translation = FVector2D(X * GetViewSize().X * CardWidthScale, 0.f);
	Card->SetRenderTransform(Transform);
}

void USupplyOpeningOverlay::Play(FSupplyAnimator& Animator)
{
	Stop(Animator);
	if (Animator.bReducedMotion)
	{
		return;
	}

	bPlaying = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetRenderOpacity(1.f);

	Animator.Play(CoralTrack, 0.f, .66f, [this](float T) { ApplyCoral(T); });
	Animator.Play(CardTrack, .07f, .65f, [this](float T) { ApplyCard(T); });
	// 전체: 85%까지 불투명 유지 후 linear로 0
	Animator.Play(FadeTrack, 0.f, .76f, [this](float T)
	{
		SetRenderOpacity(T <= .85f ? 1.f : 1.f - (T - .85f) / .15f);
	}, [this]()
	{
		bPlaying = false;
		SetVisibility(ESlateVisibility::Collapsed);
	});
}

void USupplyOpeningOverlay::Stop(FSupplyAnimator& Animator)
{
	Animator.Cancel(CoralTrack);
	Animator.Cancel(CardTrack);
	Animator.Cancel(FadeTrack);
	bPlaying = false;
	SetVisibility(ESlateVisibility::Collapsed);
}

#undef LOCTEXT_NAMESPACE
