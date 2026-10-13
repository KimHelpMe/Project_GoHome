//

#pragma once

#include "CoreMinimal.h"
#include "GoHomeGameState.h"
#include "Data/FSettlementResult.h"
#include "Core/ExpeditionEntryTypes.h"
#include "Core/ExpeditionPreparation.h"
#include "ExplorationGameState.generated.h"

class UHealthComponent;

// 정산 결과가 클라에 도착(호스트는 서버에서 직접)했을 때 브로드캐스트 — 정산/실패 UI가 바인딩
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSettlementReady, const FSettlementResult&, Result);

// 라이브 할당량 진행도(납품 누적액 / 맵 할당량)가 바뀔 때 브로드캐스트 — 상시 HUD가 바인딩
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuotaProgressChanged, int32, DeliveredValue, int32, MapQuota);

// 제한시간 정보(마감 서버시각 + 총 길이)가 복제 도착했을 때 브로드캐스트 — 상시 HUD가 바인딩, 바인딩 직후 Get*()로 초기값 1회
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTimeLimitChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExpeditionEntryChanged);

/**
 *
 */

UCLASS()
class GOHOME_API AExplorationGameState : public AGoHomeGameState, public IExpeditionPreparation
{
	GENERATED_BODY()

public:
	AExplorationGameState();

	UFUNCTION(BlueprintPure, Category = "Expedition|Entry")
	const FExpeditionEntryState& GetEntryState() const { return EntryState; }

	UFUNCTION(BlueprintPure, Category = "Expedition|Entry")
	bool HasExplorationStarted() const { return EntryState.Phase == EExpeditionEntryPhase::Active; }

	virtual bool IsExpeditionPreparing() const override
	{
		return CurrentState == EExpeditionState::Exploration && !HasExplorationStarted();
	}

	// 서버 GameMode가 결정한 값만 복제한다. UI에서 호출하지 않는다.
	void SetEntryState(const FExpeditionEntryState& InState);

	UPROPERTY(BlueprintAssignable, Category = "Expedition|Entry")
	FOnExpeditionEntryChanged OnEntryChanged;
	
	// 위젯이 매 틱 호출해 카운트다운 표시 (서버, 클라)
	UFUNCTION(BlueprintPure, Category = "Expedition")
	float GetRemainingSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Expedition")
	bool HasTimeLimit()const {return EntryState.TimeLimitSeconds > 0.f || ExpeditionDeadline > 0.f;}

	// 총 제한시간(초). 프로그레스 바 비율용. 미설정 시 0.
	UFUNCTION(BlueprintPure, Category = "Expedition")
	float GetTotalSeconds() const { return EntryState.TimeLimitSeconds > 0.f ? EntryState.TimeLimitSeconds : ExpeditionDurationSeconds; }

	// 서버 전용. 마감 서버시각 + 총 제한시간을 복제 필드에 싣는다(AExplorationGameMode::BeginPlay).
	void SetExpeditionDeadline(float InDeadlineServerTime, float InDurationSeconds);
	
	// 서버 전용. FinalizeRound 결과를 복제 필드에 싣고 호스트 로컬에도 즉시 알린다.
	void SetSettlementResult(const FSettlementResult& InResult);

	// BlueprintPure 설정 위젯이 델리게이트 콜백 안에서 값을 꺼내 쓸 수 있음
	UFUNCTION(BlueprintPure, Category = "Expedition")
	const FSettlementResult& GetSettlementResult() const { return SettlementResult; }

	// 서버 전용. 출발 시 존 데이터에서 읽은 맵 할당량을 복제 필드에 싣는다(AExplorationGameMode::BeginPlay).
	void SetMapQuota(int32 InMapQuota);

	// 서버 전용. 납품 누적 후 세이브의 새 합계를 복제 필드에 싣는다(AGoHomeGameState::AddDeliveredValue).
	void SetRoundDeliveredValue(int32 InDeliveredValue);

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetMapQuota() const { return MapQuota; }

	UFUNCTION(BlueprintPure, Category = "Expedition")
	int32 GetRoundDeliveredValue() const { return RoundDeliveredValue; }

	virtual bool GetMapQuotaProgress(int32& OutDeliveredValue, int32& OutMapQuota) const override;

	virtual bool GetTimeLimitProgress(float& OutRemainingSeconds, float& OutTotalSeconds) const override;

	// 서버 전용. AExplorationGameMode가 사망 집계 때 부른다(얇은 미러 — 판정은 GameMode).
	void AddDeadPlayer(APlayerState* PlayerState);

	// 이번 탐사에서 죽은 플레이어인가. 폰이 관련성 밖이라 클라에 없어도 판정된다(관전 대상 목록·음성 뮤트용).
	bool IsPlayerDead(const APlayerState* PlayerState) const;

protected:
	virtual void BeginPlay() override;

	// 자금(베이스 필드)이 바뀌어도 OnQuotaProgressChanged 구독자(강화 UI 등)가 계속 받도록 함께 쏜다
	virtual void NotifyExpeditionProgressChanged() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_SettlementResult();

	UFUNCTION()
	void OnRep_QuotaProgress();

	UFUNCTION()
	void OnRep_ExpeditionTime();

public:
	// 정산/실패 UI가 바인딩 (state 델리게이트와 별개 — 아래 주석 참고)
	UPROPERTY(BlueprintAssignable, Category = "Expedition")
	FOnSettlementReady OnSettlementReady;

	// 상시 할당량 HUD가 바인딩 — 최초 값 반영을 위해 바인딩 직후 Get*()로 한 번 당겨오도록
	UPROPERTY(BlueprintAssignable, Category = "Expedition")
	FOnQuotaProgressChanged OnQuotaProgressChanged;

	// 상시 제한시간 HUD가 바인딩 — 바인딩 직후 Get*()로 한 번 당겨오도록
	UPROPERTY(BlueprintAssignable, Category = "Expedition")
	FOnTimeLimitChanged OnTimeLimitChanged;

private:
	UPROPERTY(ReplicatedUsing = OnRep_EntryState)
	FExpeditionEntryState EntryState;

	UFUNCTION()
	void OnRep_EntryState();

	UPROPERTY(ReplicatedUsing = OnRep_ExpeditionTime)
	float ExpeditionDeadline = 0.f;

	// 총 제한시간(초). 마감시각과 함께 1회 세팅, 바 비율 계산용.
	UPROPERTY(Replicated)
	float ExpeditionDurationSeconds = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_SettlementResult, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	FSettlementResult SettlementResult;

	// 현재 맵의 할당량 기준(출발 시 1회 세팅). 라이브 HUD "납품 / 할당량" 표시용.
	UPROPERTY(ReplicatedUsing = OnRep_QuotaProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 MapQuota = 0;

	// 이번 탐사 사망자. 폰(UHealthComponent::IsDead)은 관련성 거리 밖이면 클라에 없으므로 PlayerState로 따로 싣는다.
	UPROPERTY(Replicated)
	TArray<TObjectPtr<APlayerState>> DeadPlayers;

	// 이번 라운드 납품 누적액(세이브 CurrentRoundDeliveredValue 미러). 납품마다 갱신.
	UPROPERTY(ReplicatedUsing = OnRep_QuotaProgress, BlueprintReadOnly, Category = "Expedition", meta = (AllowPrivateAccess = "true"))
	int32 RoundDeliveredValue = 0;

};
