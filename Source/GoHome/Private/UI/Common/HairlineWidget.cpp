#include "UI/Common/HairlineWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "GoHomeHairline"

class SHairline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHairline)
		: _Color(FLinearColor::White)
		, _Orientation(Orient_Horizontal)
	{}
		SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_ARGUMENT(EOrientation, Orientation)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Color = InArgs._Color;
		Orientation = InArgs._Orientation;
	}

	void SetColor(const FLinearColor& InColor)
	{
		Color = InColor;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	void SetOrientation(EOrientation InOrientation)
	{
		Orientation = InOrientation;
		Invalidate(EInvalidateWidgetReason::Layout);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(1.0, 1.0);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const bool bHorizontal = Orientation == Orient_Horizontal;
		const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();

		// 선 두께 방향으로 로컬 1단위가 몇 물리 픽셀인지. 두께는 그걸 정수로 반올림(최소 1) — 4K(배율 2)에선 2픽셀로 스펙 유지
		const float PixelsPerUnit = Transform.TransformVector(bHorizontal ? FVector2f(0.f, 1.f) : FVector2f(1.f, 0.f)).Size();
		if (PixelsPerUnit <= KINDA_SMALL_NUMBER)
		{
			return LayerId;
		}
		const float Thickness = FMath::Max(1.f, FMath::RoundToFloat(PixelsPerUnit)) / PixelsPerUnit;

		// 선 시작 모서리를 물리 픽셀 경계로 스냅 — 반 픽셀 걸침으로 흐려지거나 사라지지 않게
		const FVector2f AbsOrigin = Transform.TransformPoint(FVector2f::ZeroVector);
		const float AbsEdge = bHorizontal ? AbsOrigin.Y : AbsOrigin.X;
		const float LocalOffset = (FMath::RoundToFloat(AbsEdge) - AbsEdge) / PixelsPerUnit;

		const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
		const FVector2f LineSize = bHorizontal ? FVector2f(LocalSize.X, Thickness) : FVector2f(Thickness, LocalSize.Y);
		const FVector2f LineOffset = bHorizontal ? FVector2f(0.f, LocalOffset) : FVector2f(LocalOffset, 0.f);

		const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(LineSize, FSlateLayoutTransform(LineOffset)),
			FCoreStyle::Get().GetBrush("WhiteBrush"),
			DrawEffects,
			Color * InWidgetStyle.GetColorAndOpacityTint());

		return LayerId;
	}

private:
	FLinearColor Color;
	EOrientation Orientation = Orient_Horizontal;
};

void UHairlineWidget::SetLineColor(const FLinearColor& InColor)
{
	LineColor = InColor;
	if (MyHairline.IsValid())
	{
		MyHairline->SetColor(LineColor);
	}
}

void UHairlineWidget::SetOrientation(EOrientation InOrientation)
{
	Orientation = InOrientation;
	if (MyHairline.IsValid())
	{
		MyHairline->SetOrientation(Orientation);
	}
}

void UHairlineWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MyHairline.IsValid())
	{
		MyHairline->SetColor(LineColor);
		MyHairline->SetOrientation(Orientation);
	}
}

void UHairlineWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyHairline.Reset();
}

TSharedRef<SWidget> UHairlineWidget::RebuildWidget()
{
	MyHairline = SNew(SHairline)
		.Color(LineColor)
		.Orientation(Orientation);
	return MyHairline.ToSharedRef();
}

#if WITH_EDITOR
const FText UHairlineWidget::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "GoHome");
}
#endif

#undef LOCTEXT_NAMESPACE
