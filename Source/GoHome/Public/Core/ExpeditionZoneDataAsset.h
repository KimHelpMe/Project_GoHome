// 

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ExpeditionZoneDataAsset.generated.h"

class UTexture2D;

/**
 * 
 */
UCLASS(BlueprintType)
class GOHOME_API UExpeditionZoneDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	FName ZoneId;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	FText DisplayName;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	TSoftObjectPtr<UWorld> MapPath;
	
	// 해당 맵의 할당량
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	int32 MapQuota = 0;

	// 탐사 제한 시간(초). 0 이하면 시간 제한 없음
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	float TimeLimitSeconds = 0.f;

	// --- 탐사 목적지 화면(UI/ZoneSelect) 표시용. 게임 규칙에는 쓰지 않는다. ---

	// 카드 위 영문 표기(예: COAST)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	FText EnglishName;

	// 미리보는 카드에만 보이는 한 줄 설명
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	FText Caption;

	// 카드 사진. 비어 있거나 못 불러오면 맵 이름 대체 화면을 그린다(선택은 막지 않음).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	TSoftObjectPtr<UTexture2D> PreviewImage;

	// 사진 cover 크롭 기준점(CSS object-position, 0..1)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	FVector2D PreviewFocus = FVector2D(.54f, .5f);

	// 미리보는 카드의 사진 보정(CSS filter brightness/saturate). 미리보지 않는 카드는 화면 공통값을 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	float PreviewBrightness = 1.45f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone|Preview")
	float PreviewSaturation = .58f;
};
