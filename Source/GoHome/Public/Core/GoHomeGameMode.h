#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GoHomeGameMode.generated.h"

/**
 * GameMode는 서버(호스트)에만 존재하므로 별도 권한 체크 없이도 이미 "호스트 전용"이 보장
 * 접속 종료(Logout) 처리 전용. GameState의 OnPlayerRemovedFromParty로 합류시켜
 * 사망(OnDeath)과 접속 종료 두 경로 모두 생존자 수 집계가 어긋나지 않게 한다.
 */

UCLASS()
class GOHOME_API AGoHomeGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AGoHomeGameMode();
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	UFUNCTION(BlueprintCallable, Category = "Expedition")
	void ServerTravelToMap(const FString& MapPath);

	UFUNCTION(BlueprintCallable, Category = "Expedition")
	void ServerTravelViaLoadingScreen(const FString& FinalMapPath);

public:
	UFUNCTION(BlueprintPure, Category = "Expedition")
	const FString& GetLoadingMapPath() const { return LoadingMapPath; }
	
	UFUNCTION(BlueprintPure, Category = "Expedition")
	const FString& GetLobbyMapPath() const { return LobbyMapPath; }

protected:
	virtual void BeginPlay() override;
	void ContinuePendingTravel();
	FTimerHandle LoadingTravelTimer;
	bool bLoadingTravelPending = false;

	UPROPERTY(EditDefaultsOnly, Category = "Expedition")
	FString LoadingMapPath = TEXT("/Game/GoHome/Maps/LV_Loading");

	UPROPERTY(EditDefaultsOnly, Category = "Expedition")
	FString LobbyMapPath = TEXT("/Game/GoHome/Maps/LV_Lobby");
};
