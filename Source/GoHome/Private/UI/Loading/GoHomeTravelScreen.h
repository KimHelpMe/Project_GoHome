#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "UI/Loading/TravelPresentation.h"

class UTexture2D;

/** HTML Canvas의 도형·텍스트·좌표/모션을 그대로 평가하는 C++ 화면. */
class SGoHomeTravelScreen final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGoHomeTravelScreen) {}
		SLATE_ARGUMENT(TSharedPtr<FTravelPresentation>, Presentation)
		SLATE_ARGUMENT(UTexture2D*, WhiteTexture)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
	virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

protected:
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1600, 900); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32,
		const FWidgetStyle&, bool) const override;

private:
	TSharedPtr<FTravelPresentation> Presentation;
	FSlateBrush WhiteBrush;
	FSlateFontInfo TitleDownFont;
	FSlateFontInfo TitleUpFont;
	FSlateFontInfo StatusFont;
};
