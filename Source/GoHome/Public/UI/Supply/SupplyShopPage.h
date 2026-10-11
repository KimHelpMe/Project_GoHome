#pragma once

#include "CoreMinimal.h"
#include "UI/Supply/SupplyPageBase.h"
#include "SupplyShopPage.generated.h"

class UTextBlock;
class UVerticalBox;

/**
 * 보급 상점(NEXT DIVE.). 왼쪽 카탈로그 / 오른쪽 상세+밝은 주문서, 실행 직전에만 구매 전표.
 * 구매는 서버 요청만 하고 자금·보유 수량은 GameState 복제로만 갱신한다.
 */
UCLASS()
class GOHOME_API USupplyShopPage : public USupplyPageBase
{
	GENERATED_BODY()

public:
	virtual void OnPageShown(bool bBack, FName FocusHint) override;
	virtual void HandleInput(ESupplyInput Input) override;
	virtual void HandleBack() override;

protected:
	virtual UWidget* BuildPage(const SupplyUI::FBuilder& B) override;

private:
	UWidget* BuildMasthead(const SupplyUI::FBuilder& B);
	UWidget* BuildFooter(const SupplyUI::FBuilder& B);
	void ReturnToHub();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FundsText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;
};
