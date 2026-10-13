#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "VitalsWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UEquipmentUpgradeDataAsset;
class UHealthComponent;
class UOxygenComponent;
class UTextBlock;
class UTexture2D;

/**
 * 바이탈 계기 도형(외장·눈금·HP 모듈·심장·파형 / 바늘·축). 텍스트와 겹치는 순서를 시안 SVG/HTML 그대로 지키려고
 * 뒤판(Back: 눈금 숫자 아래)과 앞판(Front: 눈금 숫자 위, 산소 수치 아래) 두 장으로 나눠 NativePaint 정점 메시로 그린다.
 * 좌표는 계기 원점(시안 .instrument 좌상단) 기준 CSS px.
 */
UCLASS()
class GOHOME_API UVitalsPlate : public UUserWidget
{
	GENERATED_BODY()

public:
	bool bFront = false;

	FLinearColor Grade = FLinearColor::White;		// 강화 등급색(외장 테두리)
	FLinearColor Oxygen = FLinearColor::White;		// 산소 단계색(바늘)
	FLinearColor Health = FLinearColor::White;		// HP 단계색(심장·파형)
	FLinearColor Status = FLinearColor::White;		// 상태 문구 점
	float OxygenRatio = 1.f;
	float HeartScale = 1.f;
	float SweepStart = 0.f;							// 파형 스윕 시작(pathLength 0..100)

	// 등급색이 바뀌면 정적 메시(외장)를 다시 만든다
	void MarkStaticDirty() { bStaticDirty = true; }

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void BuildStatic() const;

	// 정점 메시용 흰 텍스처(런타임 생성). 커스텀 정점은 렌더 리소스 핸들이 있는 브러시가 필요하다
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WhiteTexture;

	FSlateBrush WhiteBrush;
	mutable FSlateResourceHandle WhiteHandle;

	mutable bool bStaticDirty = true;
	mutable TArray<FVector2f> StaticPositions;
	mutable TArray<FColor> StaticColors;
	mutable TArray<uint32> StaticIndices;
};

/**
 * 좌하단 바이탈 계기(확정 시안 Docs/Dev/UI/ingame-hud/vitals-announcer B안). 트리까지 C++(WBP 없음).
 * 산소 바늘 계기 + 큰 강화 레벨을 한 외장에, 디지털 HP(심장·숫자·파형)를 바로 아래 별도 모듈에 둔다. left 24 / bottom 24, 폭 264.
 * 산소는 현재/최대 비율(25% 이하 주의, 10% 이하 위험 — 바늘·잔량 숫자·상태 문구), 강화 등급색은 경고와 독립(외장 테두리·O₂·LV.).
 * 소유 폰의 UOxygenComponent·UHealthComponent 에 스스로 바인딩하고 폰이 바뀌면 다시 바인딩, 폰이 없으면 접힌다.
 * 사망(복제 IsDead)하면 250ms 에 걸쳐 사라지고 다음 폰까지 0 으로 남는다 — USpectateComponent::HiddenHUDClasses 는 사망 프레임에 즉시 접어
 * 페이드가 안 보이므로 목록에 넣지 않는다. 생성은 AGoHomePlayerController::RefreshProgressHUD(VitalsClass, 탐사 월드만).
 */
UCLASS()
class GOHOME_API UVitalsWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 레벨별 누적 보너스로 현재 강화 등급을 역산한다(산소 강화 레벨은 복제되지 않고 MaxOxygenBonus 만 복제된다)
	UPROPERTY(EditDefaultsOnly, Category = "Vitals")
	TSoftObjectPtr<UEquipmentUpgradeDataAsset> UpgradeData = TSoftObjectPtr<UEquipmentUpgradeDataAsset>(FSoftObjectPath(TEXT("/Game/GoHome/Developers/LSA/Blueprint/DA_OxygenUpgrade.DA_OxygenUpgrade")));

	// 강화 등급색(Lv.1, Lv.2 …). 모자라면 마지막 색 반복
	UPROPERTY(EditDefaultsOnly, Category = "Vitals")
	TArray<FLinearColor> GradeColors;

	UPROPERTY(EditDefaultsOnly, Category = "Vitals", meta = (ClampMin = "0", ClampMax = "1"))
	float OxygenWarningRatio = .25f;

	UPROPERTY(EditDefaultsOnly, Category = "Vitals", meta = (ClampMin = "0", ClampMax = "1"))
	float OxygenCriticalRatio = .10f;

	UPROPERTY(EditDefaultsOnly, Category = "Vitals", meta = (ClampMin = "0", ClampMax = "1"))
	float HealthLowRatio = .25f;

private:
	void BuildTree();
	void BindToPawn(APawn* Pawn);
	void Unbind();
	void Refresh();
	int32 ComputeUpgradeLevel(float Bonus) const;

	UFUNCTION()
	void HandleOxygenChanged(float CurrentOxygen, float MaxOxygen);

	UFUNCTION()
	void HandleHPChanged(float CurrentHP, float MaxHP);

	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<UOxygenComponent> BoundOxygen;
	TWeakObjectPtr<UHealthComponent> BoundHealth;

	int32 ShownLevel = 0;
	int32 ShownLevelDigits = 0;
	int32 ShownOxygenDigits = 0;
	float DeathFade = 1.f;		// 1 표시 ~ 0 사라짐
	float Clock = 0.f;			// 심장 박동·파형 흐름 시간

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Instrument;
	UPROPERTY(Transient) TObjectPtr<UVitalsPlate> Back;
	UPROPERTY(Transient) TObjectPtr<UVitalsPlate> Front;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GradeTexts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LevelText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> LevelSlot;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> LevelLabelSlot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OxygenText;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> OxygenSlot;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> PercentSlot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OxygenStateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HealthMaxText;
};
