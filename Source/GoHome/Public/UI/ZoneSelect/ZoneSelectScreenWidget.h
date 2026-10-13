#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UObject/WeakInterfacePtr.h"
#include "Core/ZoneSelectBackend.h"
#include "UI/Supply/SupplyMotion.h"
#include "UI/Supply/SupplyUIStyle.h"
#include "ZoneSelectScreenWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UExpeditionZoneDataAsset;
class UHorizontalBox;
class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UOverlay;
class USizeBox;
class UTextBlock;
class UTexture2D;

// CSS transition 한 값: 목표가 바뀌면 지금 값에서 새 목표로 다시 보간한다.
struct FZoneSelectTween
{
	float From = 0.f;
	float To = 0.f;
	float Value = 0.f;
	float Elapsed = 0.f;
	float Duration = 0.f;
	FSupplyEase Ease;

	void Snap(float InValue);
	void Set(float Target, float DurationSeconds, const FSupplyEase& InEase);
	void Tick(float DeltaSeconds);
};

// 카드 한 장의 위젯과 표시 상태(미리보기·포커스·지정은 화면이 따로 들고 여기엔 그린 결과만)
USTRUCT()
struct FZoneSelectCardView
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<USizeBox> Slot;		// 폭(flex-grow 보간 결과)
	UPROPERTY(Transient) TObjectPtr<UWidget> Body;		// 진입 투명도·이동 대상(button)
	UPROPERTY(Transient) TObjectPtr<UWidget> Edge;		// 1px 테두리(미리보기 = 종이색)
	UPROPERTY(Transient) TObjectPtr<UImage> Photo;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PhotoMaterial;
	UPROPERTY(Transient) TObjectPtr<UWidget> Fallback;
	UPROPERTY(Transient) TObjectPtr<UWidget> ChosenMark;
	UPROPERTY(Transient) TObjectPtr<UWidget> PreviewTag;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
	UPROPERTY(Transient) TObjectPtr<UWidget> Caption;
	UPROPERTY(Transient) TObjectPtr<UWidget> ViewLabel;
	UPROPERTY(Transient) TObjectPtr<UWidget> TeamTag;
	UPROPERTY(Transient) TObjectPtr<UWidget> Arrow;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Texture;

	FName ZoneId;
	FVector2D Focus = FVector2D(.54f, .5f);
	float SelectedBrightness = 1.45f;
	float SelectedSaturation = .58f;
};

/**
 * 탐사 목적지 Modal(시안 Docs/Dev/UI/zone-select/candidate-dive-atlas-pressure-type). 생성·입력 모드·제거와 서버 요청은
 * 오너 PlayerController(IZoneSelectBackend). 로컬 미리보기·조작 포커스·서버 팀 목적지를 별개 상태로 들고 셋을 다르게 그린다.
 * 위젯 트리·모션은 C++이 만든다(보급 화면과 같은 이유 — 시안 수치·cubic-bezier를 그대로). 사진 보정만 UI 머티리얼.
 * Slate 키보드 포커스는 항상 이 위젯이 쥐고, 조작 포커스(Tab 순서: 닫기 → 카드들 → 지정)는 논리 상태로 직접 그린다.
 */
UCLASS()
class GOHOME_API UZoneSelectScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
#if !UE_BUILD_SHIPPING
	// 시안 비교용: 주요 상자·텍스트의 레이아웃 위치(자기 렌더 이동 제외, 화면 논리 px)를 로그로
	void DumpLayout() const;

	// 활성 프레임 비교용: 진입 연출을 그 시각(ms)에 멈춘다(음수 = 다시 재생)
	void DebugFreezeEntrance(float Milliseconds);
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	UPROPERTY(EditAnywhere, Category = "ZoneSelect")
	TSoftObjectPtr<UMaterialInterface> PhotoMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ZoneSelect/M_ZoneSelectPhoto.M_ZoneSelectPhoto")));

private:
	// 조작 포커스 대상. 카드는 Card + 인덱스.
	enum class EFocus : uint8 { Close, Card, Apply };

	void BuildTree();
	UWidget* BuildHeader(const SupplyUI::FBuilder& B);
	UWidget* BuildHeading(const SupplyUI::FBuilder& B);
	UWidget* BuildGallery(const SupplyUI::FBuilder& B);
	UWidget* BuildRuler(const SupplyUI::FBuilder& B);
	UWidget* BuildBriefing(const SupplyUI::FBuilder& B);
	UWidget* BuildBottom(const SupplyUI::FBuilder& B);
	UWidget* BuildOpening(const SupplyUI::FBuilder& B);
	void BuildCards();
	FZoneSelectCardView BuildCard(const SupplyUI::FBuilder& B, const UExpeditionZoneDataAsset* Zone, int32 Index);

	// 화면 크기에 따른 CSS 수치(패딩 clamp·갤러리 높이·눈금·배경)
	void ApplyViewSize(const FVector2D& ViewSize);
	void UpdateCardLayout();
	void UpdatePhoto(int32 Index);
	void RebuildRulerTicks(float Width);
	void RebuildBackdrop(const FVector2D& AppSize);

	// 상태 → 표시
	void RenderState();
	void RenderFocus();
	void ApplyCardTargets(bool bInstant);

	// 동작(시안 selectMap / confirmDestination / announce / closeScreen)
	void SelectMap(int32 Index);
	void ConfirmDestination();
	void Announce(const FText& Message);
	void Close();
	void MoveFocus(bool bForward);
	void Activate();
	void HandleTeamDestinationChanged(FName ZoneId, EZoneDestinationChange Change);

	void PlayEntrance();
	void CancelEntrance();
	void ApplyEntranceFrame(float TimeSeconds);

	int32 FindZone(FName ZoneId) const;
	int32 HitCard(const FVector2D& ScreenPosition) const;
	bool HitWidget(const UWidget* Widget, const FVector2D& ScreenPosition) const;
	FText ZoneName(int32 Index) const;

	// --- 위젯 ---
	UPROPERTY(Transient) TObjectPtr<UImage> Backdrop;			// .app:before 방사 그라데이션(앱 상자)
	UPROPERTY(Transient) TObjectPtr<UTexture2D> BackdropTexture;
	UPROPERTY(Transient) TObjectPtr<USizeBox> AppBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> AppPadding;
	UPROPERTY(Transient) TObjectPtr<UWidget> App;				// .app 진입·퇴장 대상
	UPROPERTY(Transient) TObjectPtr<UWidget> Header;
	UPROPERTY(Transient) TObjectPtr<UWidget> Heading;
	UPROPERTY(Transient) TObjectPtr<UWidget> CloseButton;
	UPROPERTY(Transient) TObjectPtr<UWidget> CloseFocus;
	UPROPERTY(Transient) TObjectPtr<USizeBox> GallerySize;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Gallery;
	UPROPERTY(Transient) TObjectPtr<UWidget> ShutterLeft;
	UPROPERTY(Transient) TObjectPtr<UWidget> ShutterRight;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RulerTicks;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RulerName;
	UPROPERTY(Transient) TObjectPtr<UWidget> Briefing;
	UPROPERTY(Transient) TObjectPtr<UWidget> DetailName;
	UPROPERTY(Transient) TObjectPtr<UWidget> DetailQuota;
	UPROPERTY(Transient) TObjectPtr<UWidget> DetailTime;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ZoneNameText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> QuotaText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimeUnitText;
	UPROPERTY(Transient) TObjectPtr<UWidget> ApplyButton;
	UPROPERTY(Transient) TObjectPtr<UBorder> ApplyFace;
	UPROPERTY(Transient) TObjectPtr<UWidget> ApplyEdge;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ApplyLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ApplyIcon;
	UPROPERTY(Transient) TObjectPtr<UWidget> ApplyFocus;
	UPROPERTY(Transient) TObjectPtr<UWidget> Bottom;
	UPROPERTY(Transient) TObjectPtr<UBorder> SharedDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SharedName;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<UWidget> StatusWrap;		// 결과 문구 등장(텍스트 RenderTranslation은 Typeset 몫)
	UPROPERTY(Transient) TObjectPtr<UWidget> Opening;
	UPROPERTY(Transient) TObjectPtr<UWidget> WingUpper;
	UPROPERTY(Transient) TObjectPtr<UWidget> WingLower;
	UPROPERTY(Transient) TObjectPtr<UWidget> RouteType;

	UPROPERTY(Transient) TArray<FZoneSelectCardView> Cards;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> LoadedPhotoMaterial;

	TArray<TWeakObjectPtr<const UExpeditionZoneDataAsset>> Zones;

	// --- 상태 ---
	TWeakInterfacePtr<IZoneSelectBackend> Backend;
	FDelegateHandle DestinationHandle;
	int32 Selected = 0;			// 로컬 미리보기
	int32 Committed = INDEX_NONE;	// 서버 팀 목적지(복제 결과)
	int32 Hovered = INDEX_NONE;
	bool bApplyHovered = false;
	EFocus FocusKind = EFocus::Card;
	int32 FocusCard = 0;
	bool bFocusVisible = true;	// :focus-visible — 키보드로 옮긴 포커스만 링을 그린다
	bool bClosing = false;

	// CSS transition(카드별: flex-grow·밝기·채도·태그·설명·지정 표시·화살표) + 버튼 면
	TArray<FZoneSelectTween> Grow, Bright, Sat, TagOpacity, CaptionOpacity, Mark, ArrowShift;
	FZoneSelectTween ApplyHover;

	// 사진 이동 래퍼(.photo-motion) 임시 트랙 값
	TArray<float> PhotoShift;
	TArray<float> PhotoFade;

	FSupplyAnimator Animator;
	bool bEntrancePlaying = false;

	FVector2D LastViewSize = FVector2D::ZeroVector;
	float GalleryWidth = 0.f;
	float GalleryHeight = 0.f;
	float LastRulerWidth = -1.f;
};
