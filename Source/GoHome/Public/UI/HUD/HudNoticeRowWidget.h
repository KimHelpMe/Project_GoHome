#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HudNoticeTypes.h"
#include "HudNoticeRowWidget.generated.h"

class UBorder;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;

/**
 * 우측 알림 피드 한 줄 — B안(몬헌식 그라디언트 띠): 오른쪽이 진하고 왼쪽으로 사라지는 배경 띠 + 하단 액센트 라인,
 * 원형 아이콘(액센트 링) + 우하단 배지, 제목 / 액센트색 부제. Danger·Good 은 제목 뒤에 액센트 띠를 깐다.
 * 그라디언트·원형 브러시와 등장/유지/퇴장/접힘/갱신 펄스 모션은 전부 여기(텍스처는 런타임 생성 — 에셋 없음).
 * 레이아웃(BindWidget 배치·폰트)은 BP(WBP_HudNoticeRow). Reference Pack: Docs/Dev/UI/ingame-hud 알림 피드.
 */
UCLASS(Abstract)
class GOHOME_API UHudNoticeRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(const FHudNotice& InNotice);

	// 같은 Key 재알림: 내용 교체 + 펄스 + 유지 타이머 리셋
	void Refresh(const FHudNotice& InNotice);

	// 유지 시간과 무관하게 퇴장 시작(넘침이면 bFast)
	void BeginRemove(bool bFast);

	const FName& GetKey() const { return Notice.Key; }
	bool IsRemoving() const { return Phase == EPhase::Exit || Phase == EPhase::Collapse || Phase == EPhase::Finished; }
	bool IsFinished() const { return Phase == EPhase::Finished; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 루트: 퇴장 후 높이를 0 으로 접는다(HeightOverride)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> CollapseBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BackgroundImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BottomLine;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> BadgeImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> BadgeText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> TitleBand;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SubtitleText;

	// ── 색(Pack 토큰) ──
	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor InfoColor = FLinearColor::FromSRGBColor(FColor(0x19, 0xE3, 0xEE));

	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor GoodColor = FLinearColor::FromSRGBColor(FColor(0x5E, 0xF0, 0xB0));

	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor WarningColor = FLinearColor::FromSRGBColor(FColor(0xFF, 0xB5, 0x47));

	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor DangerColor = FLinearColor::FromSRGBColor(FColor(0xFF, 0x4D, 0x4D));

	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor InkColor = FLinearColor::FromSRGBColor(FColor(0xEE, 0xF6, 0xF6));

	// 제목 띠 위 글자색(Danger·Good)
	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor BandInkColor = FLinearColor::FromSRGBColor(FColor(0x07, 0x12, 0x14));

	// 배경 띠 색 — 알파는 오른쪽 끝 기준(.82), 55% 지점 .6, 왼쪽 끝 0
	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor BackgroundColor = FLinearColor(0.0009f, 0.0030f, 0.0037f, 0.82f); // sRGB #030A0C

	// 아이콘 텍스처가 없을 때 원 채움색
	UPROPERTY(EditAnywhere, Category = "Notice Row|Color")
	FLinearColor IconFallbackColor = FLinearColor::FromSRGBColor(FColor(0x10, 0x28, 0x2A));

	// ── 크기 ──
	UPROPERTY(EditAnywhere, Category = "Notice Row|Style")
	float IconSize = 32.f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Style")
	float IconRingWidth = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Style")
	float BadgeSize = 16.f;

	// 제목 띠 왼쪽 페이드 폭(px)
	UPROPERTY(EditAnywhere, Category = "Notice Row|Style")
	float TitleBandFadeWidth = 10.f;

	// ── 모션(motion.md 알림 피드) ──
	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float EnterSeconds = 0.22f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion")
	float EnterOffsetX = 24.f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float HoldSeconds = 4.f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float ExitSeconds = 0.3f;

	// 5번째 줄이 밀어낼 때
	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float FastExitSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion")
	float ExitOffsetX = 16.f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float CollapseSeconds = 0.18f;

	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "0"))
	float PulseSeconds = 0.3f;

	// 펄스 시작 밝기 배율(→ 1)
	UPROPERTY(EditAnywhere, Category = "Notice Row|Motion", meta = (ClampMin = "1"))
	float PulseBrightness = 1.8f;

private:
	enum class EPhase : uint8
	{
		Enter,
		Hold,
		Exit,
		Collapse,
		Finished,
	};

	void ApplyContent();
	void ApplyMotion();
	FLinearColor AccentColor() const;
	void EnsureTextures();

	FHudNotice Notice;

	EPhase Phase = EPhase::Enter;
	float PhaseElapsed = 0.f;
	float ExitDuration = 0.3f;
	float PulseElapsed = -1.f;
	float CollapseFromHeight = 0.f;

	// 런타임 생성 알파 램프: 배경 띠(3단 램프), 하단 라인(선형), 제목 띠(왼쪽 짧은 페이드 + 불투명)
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BackgroundRamp;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LineRamp;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BandRamp;
};
