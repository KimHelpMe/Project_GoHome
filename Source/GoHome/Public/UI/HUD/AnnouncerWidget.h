#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/AnnouncerTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/Supply/SupplyMotion.h"
#include "AnnouncerWidget.generated.h"

class IAnnouncerBackend;
class UCanvasPanel;
class UCanvasPanelSlot;
class USizeBox;
class UStrokeGlyphWidget;
class UTextBlock;
class UTexture2D;
class UWidget;

/**
 * 아나운서 컷인 밑판(패싯 패널·하단 신호선·BUI 탭·초상 계기창 링·초상). 텍스트 아래에 깔려야 해서 별도 자식으로 둔다
 * (UUserWidget::NativePaint 는 자식 위에 그려진다). CSS 그라데이션·마스크·clip-path 를 정점 색으로 그대로 옮긴다.
 */
UCLASS()
class GOHOME_API UAnnouncerPlate : public UUserWidget
{
	GENERATED_BODY()

public:
	// 카드(660×184) 좌표
	float PanelTop = 21.f;
	float PanelHeight = 142.f;
	float Glass = .5f;
	FLinearColor Signal = FLinearColor::White;
	TWeakObjectPtr<UWidget> Tab;
	FSlateBrush PortraitBrush;
	bool bHasPortrait = false;
	FVector2D PortraitPos = FVector2D::ZeroVector;		// 초상 계기창(184 정사각) 기준
	FVector2D PortraitSize = FVector2D::ZeroVector;

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	// 정점 메시용 흰 텍스처. FCoreStyle WhiteBrush 는 색 브러시라 렌더 리소스 핸들이 없다
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WhiteTexture;

	FSlateBrush WhiteBrush;
	mutable FSlateResourceHandle WhiteHandle;
};

/**
 * 아나운서(부이) 통신 컷인. 확정 시안(Docs/Dev/UI/ingame-hud/vitals-announcer prototype.html + study.css .announcer)을 C++ 트리로 옮겼다(WBP 없음).
 * 우측 상단 right 24 / top 48, 660×184. 왼쪽 패싯 패널(BUI 탭·통신 코드·상황 제목·대사), 오른쪽 원형 계기창의 부이 초상.
 * 표시 데이터·큐는 IAnnouncerBackend(UAnnouncerComponent)가, 등장·퇴장 모션(180ms ease, Y -8→0)은 이 위젯이 소유한다. 입력은 받지 않는다.
 * 생성은 AGoHomePlayerController::RefreshProgressHUD(AnnouncerClass, 탐사 월드만). 관전 중에도 유지(팀 시간 안내).
 */
UCLASS()
class GOHOME_API UAnnouncerWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 투명 배경 부이 초상(Calm). 없으면 계기창 안 단순 얼굴 기호로 대체하고 대사는 그대로 띄운다
	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	TSoftObjectPtr<UTexture2D> Portrait = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/Announcer/T_Announcer_BUI.T_Announcer_BUI")));

	// 대사판 배경 농도(시안 --glass, 기본 .50 · 조정 범위 .20~.80). 글자·초상 투명도와 무관
	UPROPERTY(EditDefaultsOnly, Category = "Announcer", meta = (ClampMin = "0.2", ClampMax = "0.8"))
	float BackgroundOpacity = .5f;

private:
	void BuildTree();
	void BindBackend();
	void HandleDisplayChanged();
	void ApplyContent(const FAnnouncerDisplay& Display);
	void ApplyMotion(float Eased);

	IAnnouncerBackend* Backend = nullptr;
	TWeakObjectPtr<UObject> BackendObject;
	uint32 ShownSerial = 0;
	bool bTargetVisible = false;
	float Progress = 0.f;	// 0 숨김 ~ 1 표시(선형 시간), 모션은 ease 를 거친다
	FSupplyEase Ease;

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Card;
	UPROPERTY(Transient) TObjectPtr<UAnnouncerPlate> Plate;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> CopySlot;
	UPROPERTY(Transient) TObjectPtr<UWidget> CopyBox;
	UPROPERTY(Transient) TObjectPtr<UWidget> TabBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CodeText;
	UPROPERTY(Transient) TObjectPtr<UStrokeGlyphWidget> CodeIcon;
	UPROPERTY(Transient) TObjectPtr<UWidget> CodeOxygen;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> OxygenTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Headline;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LineText;
	UPROPERTY(Transient) TObjectPtr<UWidget> LabelBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
	UPROPERTY(Transient) TObjectPtr<UWidget> Fallback;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallbackText;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> PortraitTexture;
};
