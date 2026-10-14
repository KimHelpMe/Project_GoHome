#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/ExpeditionEntryTypes.h"
#include "ExpeditionOpeningWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UImage;
class UTextBlock;
class UTexture2D;
class UWidget;

/**
 * 탐사 진입 오프닝 INTO THE DEEP(시안 Docs/Dev/UI/expedition-opening). 화면 크기 전체를 덮는 Sequence 화면.
 * 생성·제거는 UExpeditionEntryComponent, 이 위젯은 IExpeditionEntryBackend 스냅샷만 본다. 스킵 없음(시안의 스킵 버튼·빈 데이터 버튼 제외 —
 * 사용자 결정 2026-10-14: 모든 클라가 같은 서버 시각으로 끝까지 본다).
 * 타임라인 시각 = 서버 시각 − OpeningStartServerTime — 모든 클라가 같은 프레임을 보고, 서버는 OpeningDuration 뒤 탐사를 연다.
 * 위젯 트리·모션은 C++(보급·목적지 화면과 같은 이유 — 시안 수치·cubic-bezier를 그대로). 시안의 정적 맵 사진·상단 표기·조준점은
 * 실제 게임 화면의 대용이라 그리지 않는다(사용자 결정 2026-10-14) — 브리핑 오른쪽은 실제 월드, 끝의 도착 안내만 그린 뒤 퇴장.
 */
UCLASS()
class GOHOME_API UExpeditionOpeningWidget : public UUserWidget
{
	GENERATED_BODY()

public:
#if !UE_BUILD_SHIPPING
	// 시안 비교용: 백엔드 없이 지정 데이터로 그린다. TimeMs < 0 이면 0부터 재생, 아니면 그 시각에 정지.
	// Mode: 0 = 정상, 1 = 데이터 대기, 2 = 데이터 없음
	void SetDebugPreview(const FExpeditionEntryState& State, int32 Mode, float TimeMs);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	enum class EMode : uint8 { Timeline, Loading, Empty };

	void BuildTree();
	void BuildCutin();
	void BuildBrief();
	void BuildMasthead();
	void BuildTitle();
	void BuildMission();
	void BuildStamp();
	void BuildArrival();
	void BuildFallback();

	void ApplyLayout(const FVector2D& ViewSize);
	void ApplyData(const FExpeditionEntryState& State);
	void LayoutTitle();
	void RebuildHalftone(const FVector2D& Size);
	void ApplyFrame(float TimeMs);
	void ApplyMode(EMode Mode);

	// --- 상태 ---
	FVector2D View = FVector2D::ZeroVector;
	uint32 DataHash = 0;
	EMode CurrentMode = EMode::Timeline;
	bool bModeApplied = false;
	bool bFinishRequested = false;
	int32 TitleLines = 1;
	float TitlePx = 210.f;

#if !UE_BUILD_SHIPPING
	bool bDebug = false;
	int32 DebugMode = 0;
	float DebugTimeMs = 0.f;
	bool bDebugPlaying = false;
	FExpeditionEntryState DebugState;
#endif

	// --- 위젯 ---
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Stage;

	// 도착 안내(.arrival)
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Arrival;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ArrivalName;

	// 브리핑(.brief)
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Brief;
	UPROPERTY(Transient) TObjectPtr<UImage> BlueField;
	UPROPERTY(Transient) TObjectPtr<UImage> PaperEdge;
	UPROPERTY(Transient) TObjectPtr<UImage> Halftone;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> HalftoneTexture;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OutlineWord;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Masthead;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Record;
	UPROPERTY(Transient) TObjectPtr<UWidget> VerticalLabel;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Index;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IndexNumber;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Title;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UWidget> NameArrow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CaptionText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Mission;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> QuotaText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeUnitText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Footer;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Stamp;

	// 컷인(.cutin)
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Cutin;
	UPROPERTY(Transient) TObjectPtr<UImage> CutinPaper;
	UPROPERTY(Transient) TObjectPtr<UImage> CutinBlue;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> CutinType;

	// 대기·빈 데이터(.fallback)
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Fallback;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallbackKicker;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallbackTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallbackText;
};
