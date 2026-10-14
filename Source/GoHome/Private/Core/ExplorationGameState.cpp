// 


#include "Core/ExplorationGameState.h"
#include "Core/DockingDoorComponent.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"


AExplorationGameState::AExplorationGameState()
{
	// 생성자에서 대입하는 건 아직 리플리케이션이 시작되기 전이라 직접 대입
	// 어차피 이 시점엔 델리게이트 구독자(위젯 등)가 아직 없어서 브로드캐스트할 대상도 없음
	CurrentState = EExpeditionState::Exploration;
}

void AExplorationGameState::SetEntryState(const FExpeditionEntryState& InState)
{
	if (!HasAuthority()) return;
	EntryState = InState;
	ForceNetUpdate();
	OnRep_EntryState();
}

void AExplorationGameState::OnRep_EntryState()
{
	OnEntryChanged.Broadcast();
	OnTimeLimitChanged.Broadcast();
	NotifyExpeditionProgressChanged();
}

void AExplorationGameState::SetExpeditionDeadline(float InDeadlineServerTime, float InDurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	ExpeditionDeadline = InDeadlineServerTime;
	ExpeditionDurationSeconds = InDurationSeconds;

	// 리슨 서버 호스트는 OnRep이 불리지 않으므로 서버에서 직접 브로드캐스트
	OnTimeLimitChanged.Broadcast();
}

void AExplorationGameState::OnRep_ExpeditionTime()
{
	OnTimeLimitChanged.Broadcast();
}

void AExplorationGameState::SetSettlementResult(const FSettlementResult& InResult)
{
	if (!HasAuthority())
	{
		return;
	}

	SettlementResult = InResult;

	// 리슨 서버 호스트는 OnRep이 불리지 않으므로 서버에서 직접 브로드캐스트
	// (AGoHomeGameState::SetState가 같은 이유로 수동 Broadcast + OnRep 양쪽 하는 패턴)
	OnSettlementReady.Broadcast(SettlementResult);
}

void AExplorationGameState::OnRep_SettlementResult()
{
	OnSettlementReady.Broadcast(SettlementResult);
}

void AExplorationGameState::SetMapQuota(int32 InMapQuota)
{
	if (!HasAuthority())
	{
		return;
	}

	MapQuota = InMapQuota;
	// 리슨 서버 호스트는 OnRep이 불리지 않으므로 서버에서 직접 브로드캐스트
	NotifyExpeditionProgressChanged();
}

void AExplorationGameState::SetRoundDeliveredValue(int32 InDeliveredValue)
{
	if (!HasAuthority())
	{
		return;
	}

	RoundDeliveredValue = InDeliveredValue;
	NotifyExpeditionProgressChanged();
}

void AExplorationGameState::OnRep_QuotaProgress()
{
	NotifyExpeditionProgressChanged();
}

void AExplorationGameState::NotifyExpeditionProgressChanged()
{
	Super::NotifyExpeditionProgressChanged();
	OnQuotaProgressChanged.Broadcast(RoundDeliveredValue, MapQuota);
}

bool AExplorationGameState::GetMapQuotaProgress(int32& OutDeliveredValue, int32& OutMapQuota) const
{
	OutDeliveredValue = RoundDeliveredValue;
	OutMapQuota = MapQuota;
	return true;
}

bool AExplorationGameState::GetTimeLimitProgress(float& OutRemainingSeconds, float& OutTotalSeconds) const
{
	if (!HasTimeLimit())
	{
		return false;
	}

	OutRemainingSeconds = GetRemainingSeconds();
	OutTotalSeconds = GetTotalSeconds();
	return true;
}

float AExplorationGameState::GetRemainingSeconds() const
{
	if (!HasExplorationStarted()) return EntryState.TimeLimitSeconds;
	if (EntryState.TimeLimitSeconds > 0.f)
	{
		return FMath::Max(0.0, EntryState.GameplayStartServerTime + EntryState.TimeLimitSeconds - GetServerWorldTimeSeconds());
	}
	if (ExpeditionDeadline <= 0.f)
	{
		return 0.f;
	}
	
	return FMath::Max(0.f, ExpeditionDeadline - GetServerWorldTimeSeconds());
}

void AExplorationGameState::BeginPlay()
{
	Super::BeginPlay();

	// 문 개방은 AExplorationGameMode::InitGameState가 소유한다(BeginPlay 전에 열린 상태).
}

void AExplorationGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExplorationGameState, EntryState);
	DOREPLIFETIME(AExplorationGameState, ExpeditionDeadline);
	DOREPLIFETIME(AExplorationGameState, ExpeditionDurationSeconds);
	DOREPLIFETIME(AExplorationGameState, SettlementResult);
	DOREPLIFETIME(AExplorationGameState, MapQuota);
	DOREPLIFETIME(AExplorationGameState, RoundDeliveredValue);
	DOREPLIFETIME(AExplorationGameState, DeadPlayers);
}

void AExplorationGameState::AddDeadPlayer(APlayerState* PlayerState)
{
	if (HasAuthority() && PlayerState)
	{
		DeadPlayers.AddUnique(PlayerState);
	}
}

bool AExplorationGameState::IsPlayerDead(const APlayerState* PlayerState) const
{
	return PlayerState && DeadPlayers.Contains(PlayerState);
}

