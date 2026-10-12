#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "HairlineWidget.generated.h"

class SHairline;

/**
 * 항상 정확히 1 물리 픽셀 두께로 그려지는 구분선.
 * UImage 1px 선은 DPI 배율이 1 미만이면(PIE 창 0.92 등) 물리 두께가 1 미만이 되어,
 * 두 픽셀 중심 사이에 끼는 줄은 래스터화 0픽셀로 사라진다. 이 위젯은 그리는 순간의 배율로
 * 두께를 역보정하고 선을 물리 픽셀 경계에 스냅한다. 레이아웃은 기존 1px 이미지와 같게 로컬 1단위를 차지한다.
 */
UCLASS()
class GOHOME_API UHairlineWidget : public UWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Hairline")
	void SetLineColor(const FLinearColor& InColor);

	UFUNCTION(BlueprintCallable, Category = "Hairline")
	void SetOrientation(EOrientation InOrientation);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// 선 색(리니어). 반투명 선은 알파로 — 위젯 ColorAndOpacity 틴트와 곱해진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hairline")
	FLinearColor LineColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hairline")
	TEnumAsByte<EOrientation> Orientation = Orient_Horizontal;

private:
	TSharedPtr<SHairline> MyHairline;
};
