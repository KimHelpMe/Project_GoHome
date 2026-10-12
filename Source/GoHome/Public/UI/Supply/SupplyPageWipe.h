#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SupplyPageWipe.generated.h"

class FSupplyAnimator;
class UCanvasPanel;
class UCanvasPanelSlot;

/**
 * 내부 페이지 이동의 사선 스윕(Sophia .wipe 리듬). 머리 아래~바닥 위 띠 안에서 코발트 판 → 46ms 뒤 아이보리 판이
 * 같은 방향으로 지나가고, 그 사이에 이전 페이지 잔상이 180ms에 빠진다. 총 406ms 뒤 정리.
 * 잔상은 이전 페이지 위젯을 그대로 옮겨 담아(입력 제외) 그린다 — 끝나면 OnGhostReleased로 돌려준다.
 */
UCLASS()
class GOHOME_API USupplyPageWipe : public UUserWidget
{
	GENERATED_BODY()

public:
	DECLARE_DELEGATE_OneParam(FOnGhostReleased, UWidget* /*Ghost*/);

	void Play(FSupplyAnimator& Animator, float BandTop, float BandBottom, bool bBack, UWidget* Ghost, FOnGhostReleased InOnGhostReleased);

	// 남은 장식을 즉시 끝낸다(다음 입력 시). 잔상도 돌려준다.
	void Stop(FSupplyAnimator& Animator);

	bool IsPlaying() const { return bPlaying; }

protected:
	virtual void NativeOnInitialized() override;

private:
	void ApplyPlate(UWidget* Plate, float WidthScale, float T) const;
	void ReleaseGhost();
	FVector2D GetViewSize() const;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Band;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanelSlot> BandSlot;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Stage;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> LeadPlate;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> FollowPlate;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Ghost;

	FOnGhostReleased OnGhostReleased;
	float BandHeight = 0.f;
	float Direction = 1.f;
	bool bPlaying = false;
};
