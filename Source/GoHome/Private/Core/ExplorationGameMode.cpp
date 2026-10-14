// 


#include "Core/ExplorationGameMode.h"
#include "Core/ExplorationGameState.h"
#include "Player/DeathNotifier.h"
#include "GameFramework/PlayerState.h"
#include "Save/GoHomeSaveSubsystem.h"
#include "Core/DockingDoorComponent.h"
#include "Core/ExpeditionTravelSubsystem.h"
#include "Core/ExpeditionZoneDataAsset.h"
#include "Core/Actors/Submarine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"


#if !UE_BUILD_SHIPPING
static TAutoConsoleVariable<int32> CVarEntryPIEZone(
	TEXT("GoHome.Entry.PIEZone"),
	0,
	TEXT("탐사맵을 로비 출발 없이 직접 열었을 때 쓸 존(DA_Zone_Lv<n>). 0이면 존 없음(기존 동작)."),
	ECVF_Cheat);
#endif

AExplorationGameMode::AExplorationGameMode()
{
	GameStateClass = AExplorationGameState::StaticClass();
}

void AExplorationGameMode::BeginPlay()
{
	Super::BeginPlay();

	UExpeditionTravelSubsystem* TravelSubsystem = GetGameInstance()->GetSubsystem<UExpeditionTravelSubsystem>();

	UExpeditionZoneDataAsset* Zone = TravelSubsystem ? TravelSubsystem->GetActiveZone() : nullptr;
#if !UE_BUILD_SHIPPING
	// 탐사맵을 직접 연 PIE에서 존 데이터(오프닝·할당량·제한시간)를 확인할 때: GoHome.Entry.PIEZone <1~3>
	if (!Zone && TravelSubsystem && CVarEntryPIEZone.GetValueOnGameThread() > 0)
	{
		const int32 Number = CVarEntryPIEZone.GetValueOnGameThread();
		const FString Path = FString::Printf(TEXT("/Game/GoHome/Data/DA_Zone_Lv%d.DA_Zone_Lv%d"), Number, Number);
		if (UExpeditionZoneDataAsset* DebugZone = LoadObject<UExpeditionZoneDataAsset>(nullptr, *Path))
		{
			TravelSubsystem->SetActiveZone(DebugZone, Number);
			Zone = DebugZone;
		}
	}
#endif
	if (!Zone)
	{
		// DepartureButton을 거치지 않고 탐사맵을 직접 연 경우(PIE 등). 제한 시간/할당량 없음
		UE_LOG(LogTemp, Warning, TEXT("AExplorationGameMode: ActiveZone 없음 — 제한 시간/할당량 미적용"));
	}

	AExplorationGameState* EntryGameState = GetGameState<AExplorationGameState>();
	if (EntryGameState)
	{
		FExpeditionEntryState Entry;
		Entry.OpeningDuration = FMath::Clamp(OpeningDurationSeconds, 0.f, 30.f);
		if (Zone)
		{
			Entry.ZoneId = Zone->ZoneId;
			Entry.ZoneNumber = TravelSubsystem->GetActiveZoneNumber();
			Entry.DisplayName = Zone->DisplayName;
			Entry.EnglishName = Zone->EnglishName;
			Entry.Caption = Zone->Caption;
			Entry.MapQuota = Zone->MapQuota;
			Entry.TimeLimitSeconds = FMath::Max(0.f, Zone->TimeLimitSeconds);
		}
		RoundTimeLimitSeconds = Entry.TimeLimitSeconds;
		EntryGameState->SetEntryState(Entry);
	}

	// 할당량 세팅
	UGoHomeSaveSubsystem* SaveSubsystem = GetGameInstance()->GetSubsystem<UGoHomeSaveSubsystem>();
	if (SaveSubsystem && Zone)
	{
		SaveSubsystem->SetTargetMapQuota(Zone->MapQuota);
	}

	// 라이브 HUD용 복제 소스(할당량·자금)
	if (AExplorationGameState* ExplorationGameState = GetGameState<AExplorationGameState>())
	{
		ExplorationGameState->SetMapQuota(Zone ? Zone->MapQuota : 0);

		// 납품 전에도 정확한 보유 자금이 HUD에 뜨도록 초기값 1회 push
		if (SaveSubsystem)
		{
			ExplorationGameState->SetCurrentFunds(SaveSubsystem->GetCurrentFunds());
		}
	}

	// 참조 방지, 이미 읽었으니 null 초기화
	if (TravelSubsystem) TravelSubsystem->SetActiveZone(nullptr);
	if (OpeningDurationSeconds <= 0.f)
	{
		StartExploration();
	}
	else
	{
		EntryWaitStartedAt = GetWorld()->GetTimeSeconds();
		GetWorldTimerManager().SetTimer(EntryTimer, this, &AExplorationGameMode::TryScheduleEntry, .1f, true);
	}
}

void AExplorationGameMode::NotifyEntryReady(APlayerController* Player, AExplorationGameState* Context)
{
	if (!Player || Player->GetWorld() != GetWorld() || !Player->GetPawn() ||
		Context != GetGameState<AExplorationGameState>() || bEntryScheduled || bExplorationStarted || bRoundResolved) return;
	EntryReadyPlayers.Add(Player);
}

void AExplorationGameMode::TryScheduleEntry()
{
	if (bRoundResolved || bEntryScheduled || bExplorationStarted) return;
	bool bAllReady = NumTravellingPlayers == 0;
	int32 Participants = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || (PC->PlayerState && PC->PlayerState->IsOnlyASpectator())) continue;
		++Participants;
		bAllReady &= EntryReadyPlayers.Contains(PC) && PC->GetPawn() != nullptr;
	}
	if (Participants == 0) return;
	if (!bAllReady && GetWorld()->GetTimeSeconds() - EntryWaitStartedAt < FMath::Max(1.f, EntryReadyTimeoutSeconds)) return;
	AExplorationGameState* GS = GetGameState<AExplorationGameState>();
	if (!GS) return;
	bEntryScheduled = true;
	GetWorldTimerManager().ClearTimer(EntryTimer);
	FExpeditionEntryState Entry = GS->GetEntryState();
	Entry.Phase = EExpeditionEntryPhase::Opening;
	Entry.OpeningStartServerTime = GS->GetServerWorldTimeSeconds() + .25;
	Entry.GameplayStartServerTime = Entry.OpeningStartServerTime + Entry.OpeningDuration;
	GS->SetEntryState(Entry);
	GetWorldTimerManager().SetTimer(EntryTimer, this, &AExplorationGameMode::StartExploration, Entry.OpeningDuration + .25f, false);
	UE_LOG(LogTemp, Log, TEXT("ExpeditionEntry: opening scheduled, players=%d allReady=%d duration=%.2f"), Participants, bAllReady, Entry.OpeningDuration);
}

void AExplorationGameMode::StartExploration()
{
	AExplorationGameState* GS = GetGameState<AExplorationGameState>();
	if (!GS || bRoundResolved || bExplorationStarted || GS->GetCurrentState() != EExpeditionState::Exploration) return;
	bExplorationStarted = true;
	GetWorldTimerManager().ClearTimer(EntryTimer);
	FExpeditionEntryState Entry = GS->GetEntryState();
	Entry.Phase = EExpeditionEntryPhase::Active;
	Entry.GameplayStartServerTime = GS->GetServerWorldTimeSeconds();
	GS->SetExpeditionDeadline(RoundTimeLimitSeconds > 0.f ? Entry.GameplayStartServerTime + RoundTimeLimitSeconds : 0.f, RoundTimeLimitSeconds);
	GS->SetEntryState(Entry);
	if (UDockingDoorComponent* Door = GS->GetDockingDoorComponent()) Door->SetOpen(true);
	if (RoundTimeLimitSeconds > 0.f)
	{
		GetWorldTimerManager().SetTimer(TimeLimitTimer, this, &AExplorationGameMode::HandleTimeExpired, RoundTimeLimitSeconds, false);
	}
	UE_LOG(LogTemp, Log, TEXT("ExpeditionEntry: active, limit=%.2f"), RoundTimeLimitSeconds);
}

void AExplorationGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(EntryTimer);
	Super::EndPlay(EndPlayReason);
}


void AExplorationGameMode::RestartPlayer(AController* NewPlayer)
{
	FTransform SpawnTransform;
	if (NewPlayer && !NewPlayer->GetPawn() && FindSubmarineSpawn(NewPlayer, SpawnTransform))
	{
		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
		if (APlayerController* PC = Cast<APlayerController>(NewPlayer))
		{
			const FRotator View(SpawnViewPitch, SpawnTransform.Rotator().Yaw, 0.f);
			PC->SetControlRotation(View);
			PC->ClientSetRotation(View, true);
		}
	}
	else
	{
		Super::RestartPlayer(NewPlayer);
	}

	if (NewPlayer)
	{
		if (APawn* Pawn = NewPlayer->GetPawn())
		{
			TrackPawnDeath(Pawn);
		}
	}
}

bool AExplorationGameMode::FindSubmarineSpawn(AController* Player, FTransform& OutTransform)
{
	if (!bSpawnInFrontOfSubmarine || !Player)
	{
		return false;
	}
	TActorIterator<ASubmarine> It(GetWorld());
	if (!It)
	{
		return false;
	}

	// 슬롯: 이미 받은 컨트롤러는 그대로, 새로 오면 비어 있는 가장 작은 번호
	int32 Slot = INDEX_NONE;
	TSet<int32> Taken;
	for (auto Iter = SpawnSlots.CreateIterator(); Iter; ++Iter)
	{
		if (!Iter.Key().IsValid())
		{
			Iter.RemoveCurrent();
		}
		else if (Iter.Key().Get() == Player)
		{
			Slot = Iter.Value();
		}
		else
		{
			Taken.Add(Iter.Value());
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = 0;
		while (Taken.Contains(Slot))
		{
			++Slot;
		}
		SpawnSlots.Add(Player, Slot);
	}

	const ASubmarine* Submarine = *It;
	const FVector Out = (-Submarine->GetActorForwardVector()).GetSafeNormal2D();	// 문 쪽
	const FVector Right(-Out.Y, Out.X, 0.f);
	const float Lateral = Slot == 0 ? 0.f : SpawnSpacing * ((Slot + 1) / 2) * (Slot % 2 == 1 ? 1.f : -1.f);
	const FVector Location = Submarine->GetActorLocation() + Out * SpawnDistanceFromSubmarine + Right * Lateral + FVector(0.f, 0.f, SpawnHeightOffset);
	OutTransform = FTransform(FRotator(0.f, Out.Rotation().Yaw, 0.f), Location);
	return true;
}

void AExplorationGameMode::HandleFail(EFailReason Reason)
{
	if (bRoundResolved)
	{
		return;
	}
	bRoundResolved = true;
	GetWorldTimerManager().ClearTimer(EntryTimer);

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
	if (bRoundResolved || bReturnPending || !bExplorationStarted)
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
