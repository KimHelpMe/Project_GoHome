#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SupplyOpeningOverlay.generated.h"

class FSupplyAnimator;

/**
 * 오브젝트에서 보급 화면을 열 때 1회 재생하는 Sophia 오프닝(코발트 선행 면 → 아이보리 카드·3×3 도트·잠수정 보급 표식).
 * 장식 전용 — 입력을 막지 않고(HitTestInvisible) 끝나도 포커스를 건드리지 않는다. 출처 수치는 motion.md "사용자 지정 Sophia 오프닝".
 */
UCLASS()
class GOHOME_API USupplyOpeningOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	void Play(FSupplyAnimator& Animator);
	void Stop(FSupplyAnimator& Animator);
	bool IsPlaying() const { return bPlaying; }

protected:
	virtual void NativeOnInitialized() override;

private:
	void ApplyCoral(float T);
	void ApplyCard(float T);
	FVector2D GetViewSize() const;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Coral;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Card;

	bool bPlaying = false;
};
