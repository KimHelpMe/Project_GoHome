#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Supply/SupplyMotion.h"
#include "ItemInspectWidget.generated.h"

class AItemActorBase;
class UCanvasPanel;
class UCanvasPanelSlot;
class UHorizontalBox;
class UImage;
class UInteractionComponent;
class UTextBlock;
class UTexture2D;
class UWidget;

/**
 * 유물 조준 정보(Reference Pack: Docs/Dev/UI/item-inspect, A안 분산 콜아웃). HUD 레이어 — 입력은 받지 않는다.
 * 소유 폰 UInteractionComponent::OnInteractableTargetChanged 로 바닥 아이템 조준을 받아, 대상 주변에 패널 5개(이름·가치·무게·등급·정보)를
 * 리더 라인으로 잇는다. 위치는 매 프레임 대상 메시 바운드를 투영해 다시 계산한다(배치 규칙 = README 슬롯 표).
 * 패널은 C++ 트리, 브래킷·연결 틱·리더 라인·레티클·외곽선은 NativePaint 가 그린다(WBP 없음).
 * 생성은 AGoHomePlayerController::RefreshProgressHUD(ItemInspectClass). 관전 중엔 USpectateComponent::HiddenHUDClasses 가 숨긴다 —
 * 그래서 최상위 가시성은 건드리지 않고 내부 루트만 토글한다(관전 복원 값이 꼬이지 않게).
 */
UCLASS()
class GOHOME_API UItemInspectWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// 흰 글리프(알파) 텍스처. 위젯이 아이콘 색·등급 색으로 틴트한다. 없으면 빈 아이콘 박스.
	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	TSoftObjectPtr<UTexture2D> IconName = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ItemInspect/T_Inspect_Name.T_Inspect_Name")));

	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	TSoftObjectPtr<UTexture2D> IconValue = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ItemInspect/T_Inspect_Value.T_Inspect_Value")));

	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	TSoftObjectPtr<UTexture2D> IconWeight = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ItemInspect/T_Inspect_Weight.T_Inspect_Weight")));

	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	TSoftObjectPtr<UTexture2D> IconGrade = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ItemInspect/T_Inspect_Grade.T_Inspect_Grade")));

	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	TSoftObjectPtr<UTexture2D> IconInfo = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GoHome/UI/ItemInspect/T_Inspect_Info.T_Inspect_Info")));

	// 트레이스(0.1s 간격)가 한 번 빗나간 것으로 퇴장하지 않게 하는 유예
	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	float ReleaseGraceSeconds = 0.15f;

	// 레퍼런스 구 반지름(1920 논리 px) — 대상 화면 크기가 이 값일 때 슬롯 오프셋이 레퍼런스 실측 그대로(k=1)
	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	float ReferenceRadius = 189.f;

	// 아주 작은/가까운 아이템에서 패널이 레티클 위로 몰리거나 화면 밖으로 퍼지지 않게 묶는 거리배율 범위
	UPROPERTY(EditDefaultsOnly, Category = "Item Inspect")
	FVector2D ScaleRange = FVector2D(0.45f, 1.6f);

private:
	enum class EPanel : uint8 { Name, Value, Weight, Grade, Info, Count };

	enum class EPhase : uint8
	{
		Hidden,
		Showing,	// Enter·Shown(등장 연출은 애니메이터가 끝까지 재생)
		Leaving,	// 100ms 퇴장
		Switching	// 80ms 퇴장 → NextItem 으로 Enter
	};

	struct FPanel
	{
		TObjectPtr<UWidget> Root;
		TObjectPtr<UCanvasPanelSlot> Slot;
		TObjectPtr<UImage> Icon;
		bool bVisible = true;

		// 매 틱 레이아웃 결과(위젯 로컬 좌표, 등장 이동 포함)
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		bool bLeft = false;
		float ExitRatio = .5f;			// 선이 나가는 높이(패널 위에서 비율)
		TArray<FVector2D> Line;			// 패널 쪽 → 수평 run → 대상 위 점
		float IconSize = 72.f;

		// 모션 값
		float Alpha = 0.f;
		float Shift = 0.f;				// 0..1, 1 = 바깥쪽 8px
		float LineProgress = 0.f;
	};

	void BuildTree();
	UWidget* BuildPanel(EPanel Kind, UWidget* Content, float IconSize);
	UTextBlock* MakeLabel(const FText& Text) const;
	UTextBlock* MakeValue(const FText& Text) const;
	UWidget* MakeChip(const FText& Text, const FLinearColor& Color) const;
	UTexture2D* LoadIcon(const TSoftObjectPtr<UTexture2D>& Icon) const;

	void BindPawn(APawn* Pawn);
	UFUNCTION()
	void HandleTargetChanged(AActor* NewTarget);
	UFUNCTION()
	void HandleInteractRequested(AActor* Target);

	AItemActorBase* ResolveCandidate() const;
	bool IsStillValid(const AItemActorBase* Item) const;

	void Enter(AItemActorBase* Item);
	void Leave(float Seconds, EPhase NextPhase);
	void ApplyItem(const AItemActorBase* Item);
	void RefreshDynamic(const AItemActorBase* Item);

	// 대상 투영 → 패널 배치·선 계산. 대상이 화면 뒤면 false
	bool LayoutPanels(const FVector2D& ViewSize);

	float MotionScale() const;

	// --- 상태 ---
	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<UInteractionComponent> BoundInteraction;
	TWeakObjectPtr<AActor> AimedActor;
	// E로 줍기를 요청한 대상 — 서버 확보(bIsBeingClaimed 복제)가 오기 전까지 다시 띄우지 않는다. 조준이 벗어나면 풀린다
	TWeakObjectPtr<AActor> PickupRequestedActor;

	TWeakObjectPtr<AItemActorBase> DisplayItem;
	TWeakObjectPtr<AItemActorBase> NextItem;
	EPhase Phase = EPhase::Hidden;
	float GraceLeft = -1.f;
	int32 ShownBreakCount = -1;

	FSupplyAnimator Animator;
	float RootAlpha = 0.f;
	float ReticleAlpha = 0.f;
	float ReticleScale = 1.f;

	bool bOnScreen = false;
	FVector2D Anchor = FVector2D::ZeroVector;
	float Scale = 1.f;

	FPanel Panels[(int32)EPanel::Count];

	// --- 트리 ---
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValueText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ValueWas;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeightText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GradeText;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Chips;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescText;

	// FPanel 은 UPROPERTY 가 아니라서 GC 가 위젯을 놓치지 않게 따로 잡는다
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelRoots;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> LoadedIcons;
};
