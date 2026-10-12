#pragma once

#include "CoreMinimal.h"
#include "UI/Supply/SupplyPageBase.h"
#include "SupplyHubPage.generated.h"

class UBorder;
class UTextBlock;

/**
 * 로비 보급 오브젝트의 메인 분기(TEAM READY). 목적지 두 개: 보급 상점 / 조회 전용 팀 보관함.
 * 포커스가 곧 선택이다(aria-pressed가 focus를 따라감) — 선택 표찰은 종이색 면 + 라임 받침 + -2° 회전 + 라임 외곽선.
 */
UCLASS()
class GOHOME_API USupplyHubPage : public USupplyPageBase
{
	GENERATED_BODY()

public:
	static const FName FocusShop;
	static const FName FocusLocker;

	virtual void OnPageShown(bool bBack, FName FocusHint) override;
	virtual void HandleInput(ESupplyInput Input) override;
	virtual void HandleBack() override;
	virtual void PlayOpeningLift() override;
	virtual void OnViewSizeChanged(const FVector2D& ViewSize) override;

protected:
	virtual UWidget* BuildPage(const SupplyUI::FBuilder& B) override;

private:
	struct FDestination
	{
		TObjectPtr<UWidget> Lift = nullptr;		// 오프닝 등장(이동)
		TObjectPtr<UWidget> Card = nullptr;		// 선택 회전
		TObjectPtr<UWidget> Shadow = nullptr;
		TObjectPtr<UWidget> Outline = nullptr;
		TObjectPtr<UBorder> Face = nullptr;
		TArray<TObjectPtr<UTextBlock>> Texts;
	};

	UWidget* BuildDestination(const SupplyUI::FBuilder& B, int32 Index, const FText& Number, const FText& Title, const FText& Caption);
	void SetFocused(int32 Index);
	void Activate(int32 Index);

	FDestination Destinations[2];
	int32 FocusedIndex = 0;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> TitleBlock;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> CopyBlock;

	// .hub-body 열 간격 8vw
	UPROPERTY(Transient)
	TObjectPtr<class USpacer> ColumnGap;
};
