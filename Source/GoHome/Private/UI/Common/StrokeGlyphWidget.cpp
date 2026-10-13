#include "UI/Common/StrokeGlyphWidget.h"

#include "Widgets/SLeafWidget.h"

class SStrokeGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStrokeGlyph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}

	void SetData(const FVector2D& InViewBox, const TArray<TArray<FVector2D>>& InPaths, float InStrokeWidth, const FLinearColor& InColor)
	{
		ViewBox = InViewBox;
		Paths = InPaths;
		StrokeWidth = InStrokeWidth;
		Color = InColor;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return ViewBox;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (ViewBox.X <= 0.f || ViewBox.Y <= 0.f)
		{
			return LayerId;
		}
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const float Scale = FMath::Min(Size.X / ViewBox.X, Size.Y / ViewBox.Y);
		const FVector2f Offset = (Size - FVector2f(ViewBox) * Scale) * .5f;
		const FLinearColor Tint = Color * InWidgetStyle.GetColorAndOpacityTint();

		for (const TArray<FVector2D>& Path : Paths)
		{
			if (Path.Num() < 2)
			{
				continue;
			}
			TArray<FVector2f> Points;
			Points.Reserve(Path.Num());
			for (const FVector2D& Point : Path)
			{
				Points.Add(Offset + FVector2f(Point) * Scale);
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points,
				ESlateDrawEffect::None, Tint, true, StrokeWidth * Scale);
		}
		return LayerId;
	}

private:
	FVector2D ViewBox = FVector2D(24.f, 16.f);
	TArray<TArray<FVector2D>> Paths;
	float StrokeWidth = 1.f;
	FLinearColor Color = FLinearColor::White;
};

void UStrokeGlyphWidget::SetGlyph(const FVector2D& InViewBox, const TArray<TArray<FVector2D>>& InPaths, float InStrokeWidth)
{
	ViewBox = InViewBox;
	Paths = InPaths;
	StrokeWidth = InStrokeWidth;
	SynchronizeProperties();
}

void UStrokeGlyphWidget::SetStrokeColor(const FLinearColor& InColor)
{
	StrokeColor = InColor;
	SynchronizeProperties();
}

void UStrokeGlyphWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyGlyph.IsValid())
	{
		MyGlyph->SetData(ViewBox, Paths, StrokeWidth, StrokeColor);
	}
}

void UStrokeGlyphWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyGlyph.Reset();
}

TSharedRef<SWidget> UStrokeGlyphWidget::RebuildWidget()
{
	MyGlyph = SNew(SStrokeGlyph);
	MyGlyph->SetData(ViewBox, Paths, StrokeWidth, StrokeColor);
	return MyGlyph.ToSharedRef();
}
