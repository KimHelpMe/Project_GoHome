// 

#pragma once

#include "CoreMinimal.h"
#include "GoHomeGameState.h"
#include "LobbyGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedZoneChanged, FName, NewZoneId);

class UExpeditionZoneDataAsset;

/**
 * 
 */
UCLASS()
class GOHOME_API ALobbyGameState : public AGoHomeGameState
{
	GENERATED_BODY()
	
public:
	ALobbyGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버에서만 호출
	UFUNCTION(BlueprintCallable, Category = "Zone")
	void SetSelectedZone(FName ZoneId);

	UFUNCTION(BlueprintPure, Category = "Zone")
	const UExpeditionZoneDataAsset* GetSelectedZone() const;

	FName GetSelectedZoneId() const { return SelectedZoneId; }

	// 지정 가능한 탐사지(BP 기본값 — 복제 안 해도 클라 GameState에도 있다)
	const TArray<TObjectPtr<UExpeditionZoneDataAsset>>& GetAvailableZones() const { return AvailableZones; }

	// UI 갱신용 델리게이트 브로드 캐스트
	UFUNCTION()
	void OnRep_SelectedZone();

	UPROPERTY(BlueprintAssignable, Category = "Zone")
	FOnSelectedZoneChanged OnSelectedZoneChanged;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Zone")
	TArray<TObjectPtr<UExpeditionZoneDataAsset>> AvailableZones;

	UPROPERTY(ReplicatedUsing=OnRep_SelectedZone, BlueprintReadOnly, Category="Zone")
	FName SelectedZoneId = NAME_None;


};
