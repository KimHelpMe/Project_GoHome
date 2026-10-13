// 

#pragma once

#include "CoreMinimal.h"
#include "FailReason.h"
#include "GoHomeGameMode.h"
#include "ExplorationGameMode.generated.h"

class AExplorationGameState;





/**
 * 
 */
UCLASS()
class GOHOME_API AExplorationGameMode : public AGoHomeGameMode
{
	GENERATED_BODY()
	
public:
	AExplorationGameMode();
	
	virtual void RestartPlayer(AController* NewPlayer) override;
	
	void HandleFail(EFailReason Reason);
	void HandleReturn();

	// 소유 클라이언트의 월드/폰 준비 확인. 과거 월드 토큰과 다른 컨트롤러는 받지 않는다.
	void NotifyEntryReady(APlayerController* Player, AExplorationGameState* Context);
	
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 진입 오프닝(UExpeditionOpeningWidget) 길이 = 시안 타임라인 5.2s. 이 시간이 지나야 탐사(타이머·문·이동)가 시작된다. 0이면 오프닝 없이 즉시 시작.
	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float OpeningDurationSeconds = 5.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry", meta = (ClampMin = "1.0"))
	float EntryReadyTimeoutSeconds = 20.f;
	
private:
	void TryScheduleEntry();
	void StartExploration();
	FTimerHandle EntryTimer;
	TSet<TWeakObjectPtr<APlayerController>> EntryReadyPlayers;
	double EntryWaitStartedAt = 0.0;
	bool bEntryScheduled = false;
	bool bExplorationStarted = false;
	float RoundTimeLimitSeconds = 0.f;

	void TrackPawnDeath(APawn* Pawn);
	void HandlePawnDeath(TWeakObjectPtr<APawn> DeadPawn);
	void HandleTimeExpired();
	
	void CheckAllDead();
	void ReturnToLobby();
	void EnterSettlement();
	
private:
	
	// 구조 실패 화면 노출 시간
	UPROPERTY(EditDefaultsOnly, Category = "Expedition")
	float AutoReturnDelay = 8.f;

	// 문이 닫히는 시간
	UPROPERTY(EditDefaultsOnly, Category = "Expedition")
	float DoorCloseDelay = 8.f;

	// 자동 복귀 타이머
	FTimerHandle AutoReturnTimer;
	
	// door close 타이머
	FTimerHandle DoorCloseTimer;
	
	// 제한 시간 타이머
	FTimerHandle TimeLimitTimer;
	
	// 정산에 넘길 사망자 명단
	TSet<FString> CasualtyNames;
	
	// 중복 구독 방지용
	TSet<TWeakObjectPtr<UActorComponent>> TrackedNotifiers;
	
	// 사망 폰 집계
	TSet<TWeakObjectPtr<APawn>> DeadPawns;
	

	
	bool bRoundResolved = false;
	bool bReturnPending = false;
	
	
};
