#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Shop/ItemShopTypes.h"
#include "SupplyEquipmentArt.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;

/**
 * 시안 .equipment 평면 도해(190×165 상자, 사각·원·2D 회전 조각 4개 + drop-shadow). 상품의 EItemShopArt로 모양을 고른다.
 * 크기 배율은 상점 .75, 보관함 1(시안 scale). 실제 장비 모델 이미지가 아니라 표시용 그림이다.
 */
UCLASS()
class GOHOME_API USupplyEquipmentArt : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetArt(EItemShopArt InArt);
	void SetArtScale(float InScale);

	// 상세 변경 연출 대상(Y·투명도) — 고정 각도는 안쪽 상자가 갖는다.
	UWidget* GetMotionTarget() const { return MotionRoot; }

protected:
	virtual void NativeOnInitialized() override;

private:
	struct FPieceWidgets
	{
		TObjectPtr<UBorder> Shadow = nullptr;
		TObjectPtr<UBorder> Face = nullptr;		// 테두리 색 바깥 면
		TObjectPtr<UBorder> Inner = nullptr;	// 테두리 두께만큼 안쪽 채움
	};

	void ApplyPiece(int32 Index, float Left, float Top, float Width, float Height, const FLinearColor& Fill, float BorderWidth, const FLinearColor& BorderColor, float Radius, float AngleDeg, const FVector2D& Pivot, bool bVisible);

	UPROPERTY(Transient)
	TObjectPtr<UWidget> MotionRoot;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> RotationRoot;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Glyph;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GlyphShadow;

	// O₂의 아래첨자 — 대체 글꼴에 U+2082가 없어 작은 "2"로 따로 그린다
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> GlyphSub;

	FPieceWidgets Pieces[4];
	EItemShopArt Art = EItemShopArt::Unknown;
	float ArtScale = 1.f;
};
