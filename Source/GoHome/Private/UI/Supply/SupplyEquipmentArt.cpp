#include "UI/Supply/SupplyEquipmentArt.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "UI/Supply/SupplyUIStyle.h"

using namespace SupplyUI;

namespace
{
	constexpr float ArtWidth = 190.f;
	constexpr float ArtHeight = 165.f;
	constexpr float Round = -1.f;	// border-radius 50%

	// drop-shadow(10px 12px 0 #07164455)를 코발트 바탕 위 결과색으로 고정(겹치는 조각에서 알파가 두 번 쌓이지 않게)
	FLinearColor ShadowColor() { return MixSRGB(0x071644, 0x55 / 255.f, 0x2747d8); }

	// 채움만 쓰는 둥근 사각(외곽선 OutlineSettings는 부모 투명도를 안 따라 페이드에서 남는다 — 테두리는 두 겹 채움으로)
	FSlateBrush PieceBrush(const FLinearColor& Fill, float Radius)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.Width = 0.f;
		if (Radius < 0.f)
		{
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		}
		else
		{
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		}
		return Brush;
	}
}

void USupplyEquipmentArt::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	const FBuilder B(WidgetTree);

	// 바깥(연출 이동) > 회전·배율 > 190×165 캔버스
	USizeBox* Motion = B.Size(ArtWidth, ArtHeight);
	MotionRoot = Motion;
	WidgetTree->RootWidget = Motion;

	USizeBox* Rotation = B.Size(ArtWidth, ArtHeight);
	Rotation->SetRenderTransformPivot(FVector2D(.5f, .5f));
	RotationRoot = Rotation;
	Motion->AddChild(Rotation);

	Canvas = B.Canvas();
	Rotation->AddChild(Canvas);

	for (FPieceWidgets& Piece : Pieces)
	{
		Piece.Shadow = B.Box();
		AddCanvas(Canvas, Piece.Shadow, FVector2D::ZeroVector, FVector2D(1.f, 1.f));
	}

	// '?'(unknown) 그림자는 조각 그림자와 같은 층
	GlyphShadow = B.Text(FText::GetEmpty(), Font(EFace::Black, 120.f), ShadowColor());
	UCanvasPanelSlot* GlyphShadowSlot = Canvas->AddChildToCanvas(GlyphShadow);
	GlyphShadowSlot->SetAutoSize(true);

	for (FPieceWidgets& Piece : Pieces)
	{
		Piece.Face = B.Box();
		Piece.Inner = B.Box();
		SetContent(Piece.Face, Piece.Inner);
		AddCanvas(Canvas, Piece.Face, FVector2D::ZeroVector, FVector2D(1.f, 1.f));
	}

	Glyph = B.Text(FText::GetEmpty(), Font(EFace::Black, 120.f), Paper());
	UCanvasPanelSlot* GlyphSlot = Canvas->AddChildToCanvas(Glyph);
	GlyphSlot->SetAutoSize(true);

	GlyphSub = B.Text(FText::FromString(TEXT("2")), Font(EFace::Black, 17.f), Ink());
	UCanvasPanelSlot* SubSlot = Canvas->AddChildToCanvas(GlyphSub);
	SubSlot->SetAutoSize(true);
	SubSlot->SetPosition(FVector2D(96.f, 82.f));
	Typeset(GlyphSub, ERef::Segoe);

	SetVisibility(ESlateVisibility::HitTestInvisible);
	SetArt(EItemShopArt::Hammer);
}

void USupplyEquipmentArt::ApplyPiece(int32 Index, float Left, float Top, float Width, float Height, const FLinearColor& Fill, float BorderWidth, const FLinearColor& BorderColor, float Radius, float AngleDeg, const FVector2D& Pivot, bool bVisible)
{
	for (int32 Layer = 0; Layer < 2; ++Layer)
	{
		UBorder* Widget = Layer == 0 ? Pieces[Index].Shadow : Pieces[Index].Face;
		if (!Widget)
		{
			continue;
		}
		const bool bShadow = Layer == 0;
		Widget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bShadow)
		{
			Widget->SetBrush(PieceBrush(ShadowColor(), Radius));
		}
		else
		{
			const bool bBordered = BorderWidth > 0.f;
			Widget->SetBrush(PieceBrush(bBordered ? BorderColor : Fill, Radius));
			Widget->SetPadding(FMargin(bBordered ? BorderWidth : 0.f));
			UBorder* Inner = Pieces[Index].Inner;
			Inner->SetVisibility(bBordered ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			Inner->SetBrush(PieceBrush(Fill, Radius < 0.f ? Radius : FMath::Max(0.f, Radius - BorderWidth)));
		}
		if (UCanvasPanelSlot* PieceSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
		{
			PieceSlot->SetPosition(FVector2D(Left + (bShadow ? 10.f : 0.f), Top + (bShadow ? 12.f : 0.f)));
			PieceSlot->SetSize(FVector2D(Width, Height));
		}
		Widget->SetRenderTransformPivot(Pivot);
		Widget->SetRenderTransformAngle(AngleDeg);
	}
}

void USupplyEquipmentArt::SetArtScale(float InScale)
{
	ArtScale = InScale;
	SetArt(Art);
}

void USupplyEquipmentArt::SetArt(EItemShopArt InArt)
{
	Art = InArt;
	if (!Canvas)
	{
		return;
	}

	const FLinearColor P = Paper();
	const FLinearColor I = Ink();
	const FLinearColor L = Lime();
	const FLinearColor Clear = FLinearColor::Transparent;
	const FVector2D Center(.5f, .5f);

	float Angle = -22.f;
	FText GlyphText = FText::GetEmpty();
	FVector2D GlyphPos = FVector2D::ZeroVector;
	FSlateFontInfo GlyphFont = Font(EFace::Black, 120.f);

	switch (Art)
	{
	case EItemShopArt::Hammer:
		ApplyPiece(0, 30, 13, 124, 54, P, 3, I, 0, 0, Center, true);
		ApplyPiece(1, 79, 40, 29, 116, P, 3, I, 0, 0, Center, true);
		ApplyPiece(2, 72, 146, 43, 13, L, 3, I, 0, 0, Center, true);
		ApplyPiece(3, 25, 14, 16, 51, L, 3, I, 0, 0, Center, true);
		break;
	case EItemShopArt::Sonar:
		Angle = 10.f;
		ApplyPiece(0, 37, 18, 116, 116, I, 8, P, Round, 0, Center, true);
		ApplyPiece(1, 62, 43, 66, 66, Blue(), 2, L, Round, 0, Center, true);
		ApplyPiece(2, 92, 76, 50, 6, L, 0, Clear, 0, -30, FVector2D(0.f, .5f), true);
		ApplyPiece(3, 76, 132, 36, 20, P, 3, I, 0, 0, Center, true);
		break;
	case EItemShopArt::Capsule:
	case EItemShopArt::Oxygen:
	{
		Angle = 24.f;
		const bool bOxygen = Art == EItemShopArt::Oxygen;
		ApplyPiece(0, 60, 9, 73, 143, P, 3, I, 30, 0, Center, true);
		ApplyPiece(1, 61, 67, 71, 44, bOxygen ? Hex(0xa4f5e7) : L, 3, I, 0, 0, Center, true);
		ApplyPiece(2, 82, 86, 29, 4, I, 0, Clear, 0, 0, Center, !bOxygen);
		ApplyPiece(3, 94, 74, 4, 29, I, 0, Clear, 0, 0, Center, !bOxygen);
		if (bOxygen)
		{
			GlyphText = FText::FromString(TEXT("O"));
			GlyphPos = FVector2D(74.f, 68.f);
			GlyphFont = Font(EFace::Black, 29.f);
		}
		break;
	}
	case EItemShopArt::Remote:
		ApplyPiece(0, 54, 28, 85, 120, P, 3, I, 0, 0, Center, true);
		ApplyPiece(1, 84, 0, 22, 33, L, 3, I, 0, 0, Center, true);
		ApplyPiece(2, 72, 43, 49, 22, I, 3, I, 0, 0, Center, true);
		ApplyPiece(3, 83, 93, 27, 27, L, 3, I, Round, 0, Center, true);
		break;
	case EItemShopArt::Harpoon:
		Angle = -12.f;
		ApplyPiece(0, 23, 45, 133, 28, P, 3, I, 0, 0, Center, true);
		ApplyPiece(1, 55, 63, 26, 58, P, 3, I, 0, 14, Center, true);
		ApplyPiece(2, 131, 56, 64, 7, L, 3, I, 0, 0, Center, true);
		ApplyPiece(3, 174, 49, 21, 21, L, 3, I, 0, 45, Center, true);
		break;
	case EItemShopArt::Lamp:
		Angle = 10.f;
		ApplyPiece(0, 50, 33, 88, 100, L, 3, I, 25, 0, Center, true);
		ApplyPiece(1, 70, 12, 48, 32, I, 3, P, 0, 0, Center, true);
		ApplyPiece(2, 60, 120, 68, 16, P, 3, I, 0, 0, Center, true);
		ApplyPiece(3, 91, 52, 8, 60, P, 0, Clear, 0, 0, Center, true);
		break;
	default:
		for (int32 Index = 0; Index < 4; ++Index)
		{
			ApplyPiece(Index, 0, 0, 1, 1, Clear, 0, Clear, 0, 0, Center, false);
		}
		GlyphText = FText::FromString(TEXT("?"));
		GlyphPos = FVector2D(62.f, 0.f);
		break;
	}

	const bool bUnknown = Art == EItemShopArt::Unknown;
	for (UTextBlock* Text : { Glyph.Get(), GlyphShadow.Get() })
	{
		const bool bShadow = Text == GlyphShadow;
		Text->SetText(GlyphText);
		Text->SetFont(GlyphFont);
		Text->SetColorAndOpacity(FSlateColor(bShadow ? ShadowColor() : (bUnknown ? Paper() : Ink())));
		Text->SetVisibility(GlyphText.IsEmpty() || (bShadow && !bUnknown) ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (UCanvasPanelSlot* TextSlot = Cast<UCanvasPanelSlot>(Text->Slot))
		{
			TextSlot->SetPosition(GlyphPos + (bShadow ? FVector2D(10.f, 12.f) : FVector2D::ZeroVector));
		}
		Typeset(Text, ERef::Segoe);
	}

	GlyphSub->SetVisibility(Art == EItemShopArt::Oxygen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	FWidgetTransform Transform;
	Transform.Angle = Angle;
	Transform.Scale = FVector2D(ArtScale, ArtScale);
	RotationRoot->SetRenderTransform(Transform);
}
