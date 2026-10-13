#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/SpectateBackend.h"
#include "UI/Supply/SupplyMotion.h"
#include "UObject/WeakInterfacePtr.h"
#include "SpectateScreenWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture2D;
class UWidget;

/**
 * 사망 → 관전 화면(Reference Pack: Docs/Dev/UI/spectate, A안 DIVER FEED). HUD 레이어 — 입력은 받지 않는다.
 * ISpectateBackend 스냅샷(FSpectateView)만 매 틱 읽고, 단계 진입·대상 전환(SwitchSerial)을 보고 motion.md 연출을 재생한다.
 * 위젯 트리·노이즈/비네트/스캔라인 텍스처까지 C++이 만든다(보급·목적지 화면과 같은 방식 — WBP 없음).
 * 생성·파괴는 USpectateComponent.
 */
UCLASS()
class GOHOME_API USpectateScreenWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();
	void BuildTextures();
	UWidget* BuildFrame();
	UWidget* BuildDeathCard();
	UWidget* BuildConnect();
	UWidget* BuildToast();
	UWidget* BuildPlate();
	UWidget* BuildNoSurvivor();
	UWidget* Circle(float Size, const FLinearColor& Color) const;
	void RebuildPips(const FSpectateView& View);

	void EnterPhase(ESpectatePhase Phase, ESpectatePhase Previous, const FSpectateView& View);
	void PlayCut(int32 Direction);
	void ShowCause(FName Cause, bool bAnimate);
	void RenderPlate(const FSpectateView& View);
	void ApplyLayers(float DeltaTime);

	static FText CauseText(FName Cause);

	TWeakInterfacePtr<ISpectateBackend> Backend;
	FSupplyAnimator Animator;

	// 레이어 값 — 애니메이터가 쓰고 ApplyLayers가 위젯에 반영한다
	float VignetteAlpha = 0.f;
	float VignetteScale = 1.f;
	float NoiseAlpha = 0.f;
	float BlackAlpha = 0.f;
	float FlashAlpha = 0.f;
	float CardAlpha = 0.f;
	float ConnectAlpha = 0.f;
	float FeedAlpha = 0.f;		// 프레임·LIVE·스캔라인·플레이트
	float ToastAlpha = 0.f;
	float ToastShift = 0.f;
	float NoSurvivorAlpha = 0.f;
	float NameAlpha = 1.f;
	float NameShift = 0.f;
	FVector2D TitleJitter = FVector2D::ZeroVector;

	float Clock = 0.f;			// 깜빡임(LIVE 점·연결 점)·노이즈 프레임 교체용
	float NoiseFrameTime = 0.f;
	int32 NoiseFrame = 0;

	ESpectatePhase LastPhase = ESpectatePhase::Inactive;
	int32 LastSwitchSerial = 0;
	FName ShownCause = NAME_None;
	FString PipSignature;

	// --- 트리 ---
	UPROPERTY(Transient) TObjectPtr<UImage> Scanlines;
	UPROPERTY(Transient) TObjectPtr<UImage> Vignette;
	UPROPERTY(Transient) TObjectPtr<UBorder> Black;
	UPROPERTY(Transient) TObjectPtr<UImage> Noise;
	UPROPERTY(Transient) TObjectPtr<UBorder> Flash;

	UPROPERTY(Transient) TObjectPtr<UWidget> Frame;
	UPROPERTY(Transient) TObjectPtr<UWidget> LiveDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CamText;

	UPROPERTY(Transient) TObjectPtr<UWidget> DeathCard;
	UPROPERTY(Transient) TObjectPtr<UWidget> CardTitle;
	UPROPERTY(Transient) TObjectPtr<UWidget> CauseRow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CauseValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MetaText;

	UPROPERTY(Transient) TObjectPtr<UWidget> Connect;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConnectText;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> ConnectDots;

	UPROPERTY(Transient) TObjectPtr<UWidget> Toast;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToastText;

	UPROPERTY(Transient) TObjectPtr<UWidget> Plate;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AliveText;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Pips;
	UPROPERTY(Transient) TObjectPtr<UWidget> PrevArrow;
	UPROPERTY(Transient) TObjectPtr<UWidget> NextArrow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LastText;
	UPROPERTY(Transient) TObjectPtr<USizeBox> HPFill;
	UPROPERTY(Transient) TObjectPtr<UBorder> HPFillFace;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HPValue;
	UPROPERTY(Transient) TObjectPtr<USizeBox> O2Fill;
	UPROPERTY(Transient) TObjectPtr<UBorder> O2FillFace;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> O2Value;

	UPROPERTY(Transient) TObjectPtr<UWidget> NoSurvivor;

	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> NoiseTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> VignetteTexture;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> ScanlineTexture;
};
