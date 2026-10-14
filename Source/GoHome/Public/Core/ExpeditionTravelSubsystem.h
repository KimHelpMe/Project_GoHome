// 

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "ExpeditionTravelSubsystem.generated.h"

class UExpeditionZoneDataAsset;
class UTexture2D;
class SGoHomeTravelScreen;
struct FTravelPresentation;

/**
 * 로딩 화면(LV_Loading) 경유 트래블 중, 진짜 목적지 맵 경로를 잠깐 들고 있는다.
 * GameMode는 트래블마다 새로 생성되므로, 트래블 간 유지되는 GameInstanceSubsystem에 둔다
 */
UCLASS()
class GOHOME_API UExpeditionTravelSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 서버가 각 로컬 클라에 방향/목적지를 명시 전달한다. GameInstanceSubsystem 자체는 복제되지 않는다.
	void BeginPresentation(const FString& Destination, bool bReturning, int32 TravelId);
	void CancelPresentation();
	bool IsPresentationBlockingEntry() const { return Screen.IsValid() && !bDebugPreview; }

#if !UE_BUILD_SHIPPING
	void Preview(bool bReturning, float TimeMs, int32 Mode, bool bReduced);
	void FinishPreview();
#endif

	// 최종 목적지 맵 경로. 로딩 맵의 GameMode가 전원 도착 확인 뒤 읽는다.
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
	bool TickPresentation(float DeltaSeconds);
	void EnsureScreen();
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);

	FTSTicker::FDelegateHandle PresentationTicker;
	TSharedPtr<FTravelPresentation> Presentation;
	TSharedPtr<SGoHomeTravelScreen> Screen;
	FString PresentationDestination;
	TWeakObjectPtr<UWorld> SourceWorld;
	TWeakObjectPtr<class UGameViewportClient> ScreenViewport;
	int32 PresentationId = 0;
	bool bDebugPreview = false;
	bool bDebugPaused = false;
	bool bDestinationSeen = false;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WhiteTexture;

	UPROPERTY()
	FString PendingDestinationMap;

	// 하드 레퍼런스라 트래블 중 언로드되지 않는다.
	UPROPERTY()
	TObjectPtr<UExpeditionZoneDataAsset> ActiveZone = nullptr;

	int32 ActiveZoneNumber = 0;
};
