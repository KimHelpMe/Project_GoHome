//THE

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ItemDataAsset.generated.h"

class UStaticMesh;
class USoundBase;
class USoundAttenuation;

/** 아이템 종별 무게/가치/파손·소음 플래그. AItemActorBase가 참조해 종을 구분한다. */
UCLASS(BlueprintType)
class GOHOME_API UItemDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	float Weight = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	float Value = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UStaticMesh> Mesh;

	// 파손 단계별 교체용 머터리얼 (인덱스 0 = BreackCount 1, 1=BreakCount 2).
// 비워두면 그 단계는 교체 안되고 스텐실 틴트만 그대로 유지됨. 안전함.)
	UPROPERTY(EditAnywhere, Category = "Item")
	TArray<TObjectPtr<UMaterialInterface>> BreakStageMaterials;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FVector Scale = FVector::OneVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	bool bCanBreak = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	bool bMakesNoise = false;

	// 파손형 전용(bCanBreak일 때만 의미 있음).
	// 01문서 4절 수치가 기본값 - 감산형: 파손마다 -30%p, 40%에서 하한.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "낙하/충돌 속도가 이 값(cm/s) 이상이면 파손 판정"))
	float BreakVelocityThreshold = 1000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "파손 1회당 잔존가치 비율에서 차감되는 값 (0.3 = -30%p, 감산형)", 
			ClampMin = "0.0", ClampMax = "1.0"))
	float BreakValuePenaltyPercent = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "최대 파손 횟수",
			ClampMin = "0"))
	int32 MaxBreakCount = 2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "잔존가치 하한 비율 (0.4 = 40%)",
			ClampMin = "0.0", ClampMax = "1.0"))
	float MinValuePercent = 0.4f;

	// 파손형 아이템 사운드 전용.

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "파손될 때마다 재생할 사운드(마지막 파손 포함). 비워두면 소리 없음"))
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "파손 사운드에 적용할 Sound Attenuation 애셋(들리는 거리)"))
	TObjectPtr<USoundAttenuation> BreakSoundAttenuation;

	// 소음 유발형 전용(bMakeNoise일 때만 의미 있음).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "보유 시작 시 기본 소음 반경"))
	float BaseNoiseRadius = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "보유 중 반경이 증가하는 주기(초)", ClampMin = "0.1"))
	float NoiseGrowthIntervalSeconds = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "주기마다 증가하는 반경"))
	float NoiseRadiusGrowthPerInterval = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item",
		meta = (ToolTip = "최대 소음 반경", ClampMin = "0.0"))
	float MaxNoiseRadius = 2000.f;

	// 보유 중 재생할 루프 사운드 + 감쇠 애셋.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "보유 중 재생할 루프 사운드"))
	TObjectPtr<USoundBase> HeldNoiseSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "위 사운드에 적용할 Sound Attenuation 애셋"))
	TObjectPtr<USoundAttenuation> HeldNoiseAttenuation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "소음 반경이 최소(BaseNoiseRadius)일 때 볼륨 배율"))
	float MinNoiseVolumeMultiplier = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "소음 반경이 최대(MaxNoiseRadius)일 때 볼륨 배율"))
	float MaxNoiseVolumeMultiplier = 1.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "소음 반경이 최소일 때 피치 배율"))
	float MinNoisePitchMultiplier = 0.9f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (ToolTip = "소음 반경이 최대일 때 피치 배율"))
	float MaxNoisePitchMultiplier = 1.3f;




};
