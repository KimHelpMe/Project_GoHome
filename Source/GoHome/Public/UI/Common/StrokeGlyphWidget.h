#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "StrokeGlyphWidget.generated.h"

class SStrokeGlyph;

/**
 * SVG viewBox 좌표의 선(polyline) 몇 개로 그리는 작은 아이콘. 시안의 인라인 SVG 키캡·대체 글꼴에 없는 기호(↧ 등)를
 * 글리프 대신 그대로 옮길 때 쓴다. viewBox는 위젯 크기에 맞춰 비율 유지·가운데 정렬(preserveAspectRatio meet).
 */
UCLASS()
class GOHOME_API UStrokeGlyphWidget : public UWidget
{
	GENERATED_BODY()

public:
	// Paths: viewBox 좌표의 꼭짓점 목록들. StrokeWidth도 viewBox 단위(SVG stroke-width).
	void SetGlyph(const FVector2D& InViewBox, const TArray<TArray<FVector2D>>& InPaths, float InStrokeWidth);

	UFUNCTION(BlueprintCallable, Category = "StrokeGlyph")
	void SetStrokeColor(const FLinearColor& InColor);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StrokeGlyph")
	FLinearColor StrokeColor = FLinearColor::White;

private:
	FVector2D ViewBox = FVector2D(24.f, 16.f);
	TArray<TArray<FVector2D>> Paths;
	float StrokeWidth = 1.f;

	TSharedPtr<SStrokeGlyph> MyGlyph;
};
