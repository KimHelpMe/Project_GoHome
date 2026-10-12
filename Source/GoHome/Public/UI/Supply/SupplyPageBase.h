#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Supply/SupplyUIStyle.h"
#include "SupplyPageBase.generated.h"

class FSupplyAnimator;
class USupplyScreenWidget;
class UOverlay;

// 보급 화면 입력을 키보드·게임패드 공통 의미로 정규화한 것. 실제 키 매핑은 USupplyScreenWidget.
enum class ESupplyInput : uint8
{
	Up,
	Down,
	Left,
	Right,
	Accept,	// Enter / A
	TabNext,
	TabPrev
};

/**
 * 보급 화면의 한 페이지(분기·상점·보관함). 위젯 트리를 C++로 직접 만들고, 포커스·선택은 논리 상태로 들고 직접 그린다
 * (Slate 포커스는 호스트가 항상 쥐고 있다 — 선택 면과 조작 커서를 분리해 그리는 시안 규칙 때문).
 * 마우스는 호스트가 받아 HandleClick으로 넘기고, 페이지는 등록한 히트 대상의 레이아웃 영역으로 판정한다.
 */
UCLASS(Abstract)
class GOHOME_API USupplyPageBase : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitPage(USupplyScreenWidget* InHost);

	// 페이지가 보이기 시작할 때(진입 방향, 복귀 목적지 힌트)
	virtual void OnPageShown(bool bBack, FName FocusHint) {}
	virtual void OnPageHidden() {}

	virtual void HandleInput(ESupplyInput Input) {}

	// Esc / B — 한 단계 뒤로. 최상위면 페이지가 호스트에 이동/닫기를 요청한다.
	virtual void HandleBack() {}

	bool HandleClick(const FVector2D& ScreenPosition);

	// 내부 이동 사선 스윕이 지나가는 띠(머리 아래 ~ 바닥 위). 화면 위/아래 기준 거리(px).
	void GetWipeBand(float& OutTop, float& OutBottom) const;

	// 뷰포트 논리 크기가 바뀔 때(최초 포함). CSS vw 단위 간격을 여기서 갱신한다.
	virtual void OnViewSizeChanged(const FVector2D& ViewSize) {}

	// Sophia 오프닝 동안 판 뒤에서 순차 정착하는 등장(페이지별 지연표 — motion.md)
	virtual void PlayOpeningLift() {}

	// 페이지 전체(진입/퇴장 이동·투명도 대상)
	UWidget* GetSurface() const { return Surface; }

protected:
	virtual void NativeOnInitialized() override;

	// 위젯 트리를 만들고 루트를 돌려준다. Masthead/Footer/PagePadding을 채워야 스윕 띠가 맞는다.
	virtual UWidget* BuildPage(const SupplyUI::FBuilder& B) { return nullptr; }

	// Layer: 지금 입력을 받는 층(HitLayer)과 같은 대상만 클릭된다 — 확인 패널이 뜨면 층을 올려 배경 클릭을 막는다.
	void AddHitTarget(UWidget* Target, TFunction<void()> OnClick, int32 Layer = 0);
	FSupplyAnimator* GetAnimator() const;
	USupplyScreenWidget* GetHost() const { return Host.Get(); }

	// 시안 sophiaLift: opacity 0→1, Y 9→0
	void PlayLift(UWidget* Target, FName Id, float DelayMs, float DurationMs, bool bPageEase);

	// 사선 배경 면(뷰포트 비율 앵커, 중심 회전)
	UWidget* MakeBand(const SupplyUI::FBuilder& B, const FLinearColor& Color, float AnchorMinX, float AnchorMinY, float AnchorMaxX, float AnchorMaxY, float AngleDeg) const;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Surface;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Masthead;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> Footer;

	FMargin PagePadding;

	int32 HitLayer = 0;

private:
	struct FHitTarget
	{
		TWeakObjectPtr<UWidget> Widget;
		TFunction<void()> OnClick;
		int32 Layer = 0;
	};

	TArray<FHitTarget> HitTargets;
	TWeakObjectPtr<USupplyScreenWidget> Host;
};
