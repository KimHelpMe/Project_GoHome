// 

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ExpeditionTravelSubsystem.generated.h"

class UExpeditionZoneDataAsset;

/**
 * 로딩 화면(LV_Loading) 경유 트래블 중, 진짜 목적지 맵 경로를 잠깐 들고 있는다.
 * GameMode는 트래블마다 새로 생성되므로, 트래블 간 유지되는 GameInstanceSubsystem에 둔다
 */
UCLASS()
class GOHOME_API UExpeditionTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 최종 목적지 맵 경로. 로딩 맵(LV_Loading) BP가 읽어 다음 트래블 대상을 안다.
	void SetPendingDestinationMap(const FString& MapPath) { PendingDestinationMap = MapPath; }

	UFUNCTION(BlueprintPure, Category = "Expedition")
	const FString& GetPendingDestinationMap() const { return PendingDestinationMap; }

	// 출발 시 로비에서 선택된 존 에셋. 탐사맵 GameMode가 BeginPlay에서 읽어
	// 할당량/제한 시간을 꺼낸다. 쓰기는 C++ 전용
	// ZoneNumber: 로비 목적지 목록 순번(1부터, 0 = 모름) — 탐사 진입 오프닝의 "ZONE 01" 표기용
	void SetActiveZone(const UExpeditionZoneDataAsset* ZoneDataAsset, int32 ZoneNumber = 0);
	UExpeditionZoneDataAsset* GetActiveZone() const { return ActiveZone; }
	int32 GetActiveZoneNumber() const { return ActiveZoneNumber; }

private:
	UPROPERTY()
	FString PendingDestinationMap;

	// 하드 레퍼런스라 트래블 중 언로드되지 않는다.
	UPROPERTY()
	TObjectPtr<UExpeditionZoneDataAsset> ActiveZone = nullptr;

	int32 ActiveZoneNumber = 0;
};
