// 


#include "Core/ExplorationGameMode.h"
#include "Core/ExplorationGameState.h"
#include "Player/DeathNotifier.h"
#include "GameFramework/PlayerState.h"
#include "Save/GoHomeSaveSubsystem.h"
#include "Core/DockingDoorComponent.h"
#include "Core/ExpeditionTravelSubsystem.h"
#include "Core/ExpeditionZoneDataAsset.h"


AExplorationGameMode::AExplorationGameMode()
{
	GameStateClass = AExplorationGameState::StaticClass();
}

void AExplorationGameMode::BeginPlay()
{
	Super::BeginPlay();

	UExpeditionTravelSubsystem* TravelSubsystem = GetGameInstance()->GetSubsystem<UExpeditionTravelSubsystem>();

	UExpeditionZoneDataAsset* Zone = TravelSubsystem ? TravelSubsystem->GetActiveZone() : nullptr;
	if (!Zone)
	{
		// DepartureButton을 거치지 않고 탐사맵을 직접 연 경우(PIE 등). 제한 시간/할당량 없음
		UE_LOG(LogTemp, Warning, TEXT("AExplorationGameMode: ActiveZone 없음 — 제한 시간/할당량 미적용"));
		return;
	}

	// 할당량 세팅
	UGoHomeSaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<UGoHomeSaveSubsystem>();
	if (SaveSubsystem)
	{
		SaveSubsystem->SetTargetMapQuota(Zone->MapQuota);
	}

	// 라이브 HUD용 복제 소스(할당량·자금)
	if (AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>())
	{
		ExplorationGameState->SetMapQuota(Zone->MapQuota);

		// 납품 전에도 정확한 보유 자금이 HUD에 뜨도록 초기값 1회 push
		if (SaveSubsystem)
		{
			ExplorationGameState->SetCurrentFunds(SaveSubsystem->GetCurrentFunds());
		}
	}

	// 정상적으로 타이며가 세팅 되어 있는 경우
	if (Zone->TimeLimitSeconds > 0.f)
	{
		if (AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>())
		{
			const float Deadline = ExplorationGameState->GetServerWorldTimeSeconds() + Zone->TimeLimitSeconds;
			ExplorationGameState->SetExpeditionDeadline(Deadline, Zone->TimeLimitSeconds);
		}

		GetWorldTimerManager().SetTimer(TimeLimitTimer, this, &AExplorationGameMode::HandleTimeExpired,
		                                Zone->TimeLimitSeconds, false);
	}

	// 참조 방지, 이미 읽었으니 null 초기화
	TravelSubsystem->SetActiveZone(nullptr);
}


void AExplorationGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);

	if (NewPlayer)
	{
		if (APawn* Pawn = NewPlayer->GetPawn())
		{
			TrackPawnDeath(Pawn);
		}
	}
}

void AExplorationGameMode::HandleFail(EFailReason Reason)
{
	if (bRoundResolved)
	{
		return;
	}
	bRoundResolved = true;

	GetWorldTimerManager().ClearTimer(TimeLimitTimer);
	GetWorldTimerManager().ClearTimer(DoorCloseTimer);

	AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>();

	// 정산부터 확정한 뒤 상태를 넘긴다 — 호스트 로컬에서 SetState 리스너가 결과를 이미 읽을 수 있도록
	if (UGoHomeSaveSubsystem* GoHomeSaveSubsystem = GetGameInstance()->GetSubsystem<UGoHomeSaveSubsystem>())
	{
		const FSettlementResult Result = GoHomeSaveSubsystem->FinalizeRound(/*bForfeited=*/true, CasualtyNames.Array());
		if (ExplorationGameState)
		{
			ExplorationGameState->SetSettlementResult(Result);
		}
	}

	if (ExplorationGameState)
	{
		ExplorationGameState->SetState(EExpeditionState::Failed);
	}

	GetWorldTimerManager().SetTimer(AutoReturnTimer, this, &AExplorationGameMode::ReturnToLobby, AutoReturnDelay,
	                                false);
}

void AExplorationGameMode::HandleReturn()
{
	if (bRoundResolved || bReturnPending)
	{
		return;
	}

	bReturnPending = true;

	if (AGoHomeGameState* GoHomeGameState = GetGameState<AGoHomeGameState>())
	{
		if (UDockingDoorComponent* DockingDoorComponent = GoHomeGameState->GetDockingDoorComponent())
		{
			DockingDoorComponent->SetOpen(false);
		}
	}

	GetWorldTimerManager().SetTimer(DoorCloseTimer, this, &AExplorationGameMode::EnterSettlement, DoorCloseDelay,
	                                false);
}

void AExplorationGameMode::TrackPawnDeath(APawn* Pawn)
{
	if (UActorComponent* NotifierComponent = Pawn->FindComponentByInterface(UDeathNotifier::StaticClass()))
	{
		IDeathNotifier* DeathNotifier = Cast<IDeathNotifier>(NotifierComponent);
		if (!DeathNotifier)
		{
			return;
		}

		// 재구독 방지
		if (TrackedNotifiers.Contains(NotifierComponent))
		{
			return;
		}

		TrackedNotifiers.Add(NotifierComponent);
		DeathNotifier->GetOnDeathDelegate().AddUObject(this, &AExplorationGameMode::HandlePawnDeath,
		                                               TWeakObjectPtr<APawn>(Pawn));
	}
}

void AExplorationGameMode::HandlePawnDeath(TWeakObjectPtr<APawn> DeadPawn)
{
	APawn* Pawn = DeadPawn.Get();
	if (!Pawn)
	{
		return;
	}

	// 사망 집계
	DeadPawns.Add(Pawn);

	// 사망 순간폰이 이미 파괴돼 있으면 이름을 못 얻음
	if (APlayerState* PlayerState = Pawn->GetPlayerState())
	{
		CasualtyNames.Add(PlayerState->GetPlayerName());
		// 클라 관전 대상 목록·음성 뮤트가 읽는 복제 미러
		if (AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>())
		{
			ExplorationGameState->AddDeadPlayer(PlayerState);
		}
	}

	CheckAllDead();
}

void AExplorationGameMode::HandleTimeExpired()
{
	HandleFail(EFailReason::TimeExpired);
}

void AExplorationGameMode::CheckAllDead()
{
	if (bRoundResolved)
	{
		return;
	}

	AGameStateBase* GameStateBase = GameState;
	if (!GameStateBase)
	{
		return;
	}

	int32 AliveCount = 0;
	for (APlayerState* PlayerState : GameStateBase->PlayerArray)
	{
		APawn* Pawn = PlayerState->GetPawn();
		if (Pawn && !DeadPawns.Contains(Pawn))
		{
			AliveCount++;
		}
	}

	if (AliveCount == 0)
	{
		HandleFail(EFailReason::AllPlayersDead);
	}
}

void AExplorationGameMode::ReturnToLobby()
{
	ServerTravelViaLoadingScreen(GetLobbyMapPath());
}

void AExplorationGameMode::EnterSettlement()
{
	if (bRoundResolved)
	{
		return;
	}

	bRoundResolved = true;

	GetWorldTimerManager().ClearTimer(TimeLimitTimer);

	AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>();

	if (UGoHomeSaveSubsystem* GoHomeSaveSubsystem = GetGameInstance()->GetSubsystem<UGoHomeSaveSubsystem>())
	{
		const FSettlementResult Result = GoHomeSaveSubsystem->FinalizeRound(/*bForfeited=*/false, CasualtyNames.Array());
		if (ExplorationGameState)
		{
			ExplorationGameState->SetSettlementResult(Result);
		}
	}

	if (ExplorationGameState)
	{
		ExplorationGameState->SetState(EExpeditionState::Settlement);
	}

	GetWorldTimerManager().SetTimer(AutoReturnTimer, this, &AExplorationGameMode::ReturnToLobby, AutoReturnDelay,
	                                false);
}
