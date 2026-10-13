#include "Core/AnnouncerComponent.h"

#include "Core/DockingDoorComponent.h"
#include "Core/ExpeditionEntryBackend.h"
#include "Core/ExplorationGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Player/HealthComponent.h"
#include "Player/OxygenComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogAnnouncer, Log, All);

namespace
{
	const FName OxygenKey(TEXT("O2"));
	constexpr float TimeThresholds[3] = { 60.f, 30.f, 10.f };
	constexpr int32 UrgentPriority = 80;
	constexpr double ExitSeconds = .18;		// 위젯 퇴장 시간 — 긴급 안내는 이것만 기다린다
	constexpr double TimeArmDelay = 1.0;	// 새 월드 직후 서버 시각 동기화 전의 남은 시간으로 무장하지 않게

#if !UE_BUILD_SHIPPING
	void ForEachLocalAnnouncer(UWorld* World, TFunctionRef<void(UAnnouncerComponent&)> Fn)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				if (UAnnouncerComponent* Announcer = PC->FindComponentByClass<UAnnouncerComponent>())
				{
					Fn(*Announcer);
				}
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DebugCommand(
		TEXT("GoHome.Announcer.Debug"),
		TEXT("아나운서 사건 하나를 로컬에 재생한다(게임 상태는 안 바꿈). 인자: Start Return Time60 Time30 Time10 O2Low O2Crit FirstArtifact Artifact Attacked Damaged Vent Mine, 또는 Clear"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : FString(TEXT("Start"));
			const bool bClear = Name.Equals(TEXT("Clear"), ESearchCase::IgnoreCase);
			FAnnouncerEvent Event;
			Event.Type = AnnouncerCatalog::FromName(Name);
			if (!bClear && Event.Type == EAnnouncerEvent::None)
			{
				UE_LOG(LogAnnouncer, Warning, TEXT("GoHome.Announcer.Debug: 모르는 사건 '%s'"), *Name);
				return;
			}
			ForEachLocalAnnouncer(World, [&](UAnnouncerComponent& Announcer)
			{
				if (bClear)
				{
					Announcer.ClearAll();
				}
				else
				{
					Announcer.PushLocal(Event);
				}
			});
		}));

	// 우선순위 선점·큐·만료 확인: 획득(20) → 30초(80, 선점) → 획득 대기 → 산소 위험(100, 선점) → 열수(100, 대기)
	FAutoConsoleCommandWithWorld DebugBurstCommand(
		TEXT("GoHome.Announcer.DebugBurst"),
		TEXT("우선순위 선점·대기열 확인용 사건 묶음을 로컬에 한 번에 넣는다."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			const EAnnouncerEvent Burst[] = { EAnnouncerEvent::ArtifactFirst, EAnnouncerEvent::TimeWarning30,
				EAnnouncerEvent::Artifact, EAnnouncerEvent::OxygenCritical, EAnnouncerEvent::VentWarning };
			ForEachLocalAnnouncer(World, [&](UAnnouncerComponent& Announcer)
			{
				for (const EAnnouncerEvent Type : Burst)
				{
					FAnnouncerEvent Event;
					Event.Type = Type;
					Announcer.PushLocal(Event);
				}
			});
		}));
#endif
}

UAnnouncerComponent::UAnnouncerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Client RPC 를 이 컴포넌트에 둔다(복제 프로퍼티는 없음)
	SetIsReplicatedByDefault(true);
}

void UAnnouncerComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAnnouncerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UHealthComponent* Health = ServerHealth.Get())
	{
		Health->OnDamageTaken.Remove(DamageHandle);
	}
	ServerHealth.Reset();
	if (UOxygenComponent* Oxygen = ServerOxygen.Get())
	{
		Oxygen->OnDrainBoostStarted.Remove(DrainHandle);
	}
	ServerOxygen.Reset();
	if (UOxygenComponent* Oxygen = BoundOxygen.Get())
	{
		Oxygen->OnOxygenChanged.RemoveDynamic(this, &UAnnouncerComponent::HandleOxygenChanged);
	}
	BoundOxygen.Reset();
	Super::EndPlay(EndPlayReason);
}

void UAnnouncerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ServerRebindPawn();
	}
	if (IsLocal())
	{
		LocalTick();
	}
}

// ---------------------------------------------------------------- 서버

void UAnnouncerComponent::SendToPawnOwner(const APawn* Pawn, EAnnouncerEvent Event, FName Key)
{
	if (!Pawn || !Pawn->HasAuthority() || Event == EAnnouncerEvent::None)
	{
		return;
	}
	const AController* Controller = Pawn->GetController();
	if (UAnnouncerComponent* Announcer = Controller ? Controller->FindComponentByClass<UAnnouncerComponent>() : nullptr)
	{
		FAnnouncerEvent Envelope;
		Envelope.Type = Event;
		Envelope.Key = Key;
		Announcer->ServerSend(Envelope);
	}
}

void UAnnouncerComponent::CancelForPawnOwner(const APawn* Pawn, FName Key)
{
	if (!Pawn || !Pawn->HasAuthority() || Key.IsNone())
	{
		return;
	}
	const AController* Controller = Pawn->GetController();
	if (UAnnouncerComponent* Announcer = Controller ? Controller->FindComponentByClass<UAnnouncerComponent>() : nullptr)
	{
		FAnnouncerEvent Envelope;
		Envelope.Key = Key;
		Envelope.bCancel = true;
		Announcer->ServerSend(Envelope);
	}
}

void UAnnouncerComponent::AnnounceToLocalPlayer(APawn* Pawn, EAnnouncerEvent Event)
{
	if (!Pawn || !Pawn->IsLocallyControlled() || Event == EAnnouncerEvent::None)
	{
		return;
	}
	const AController* Controller = Pawn->GetController();
	UAnnouncerComponent* Announcer = Controller ? Controller->FindComponentByClass<UAnnouncerComponent>() : nullptr;
	if (!Announcer)
	{
		return;
	}
	const float Cooldown = AnnouncerCatalog::Get(Event).ServerCooldownSeconds;
	const double Time = Announcer->Now();
	if (const double* Last = Announcer->LocalLastTime.Find((uint8)Event); Cooldown > 0.f && Last && Time - *Last < Cooldown)
	{
		return;
	}
	Announcer->LocalLastTime.Add((uint8)Event, Time);

	FAnnouncerEvent Envelope;
	Envelope.Type = Event;
	Announcer->Enqueue(Envelope);
}

void UAnnouncerComponent::ServerResetIfNewWorld()
{
	AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (ServerContext.Get() != GameState)
	{
		ServerContext = GameState;
		LastSentTime.Reset();
		bFirstArtifactSent = false;
	}
}

void UAnnouncerComponent::ServerSend(const FAnnouncerEvent& InEvent)
{
	ServerResetIfNewWorld();

	const AGoHomeGameState* GameState = Cast<AGoHomeGameState>(ServerContext.Get());
	if (!Cast<AExplorationGameState>(ServerContext.Get()) || !GameState || GameState->GetCurrentState() != EExpeditionState::Exploration)
	{
		return;
	}

	FAnnouncerEvent Event = InEvent;
	if (!Event.bCancel)
	{
		const double Time = GetWorld()->GetTimeSeconds();

		// 탐사당 첫 유물만 따로 칭찬하고, 이후 유물은 발화 간격을 지킨다
		if (Event.Type == EAnnouncerEvent::Artifact && !bFirstArtifactSent)
		{
			bFirstArtifactSent = true;
			Event.Type = EAnnouncerEvent::ArtifactFirst;
			LastSentTime.Add((uint8)EAnnouncerEvent::Artifact, Time);
		}

		// 피해 두 종류는 간격을 공유한다(연속 피격 중 문구만 바뀌며 반복되지 않게)
		const EAnnouncerEvent CooldownSlot = Event.Type == EAnnouncerEvent::Damaged ? EAnnouncerEvent::Attacked : Event.Type;
		const float Cooldown = AnnouncerCatalog::Get(Event.Type).ServerCooldownSeconds;
		if (Cooldown > 0.f)
		{
			if (const double* Last = LastSentTime.Find((uint8)CooldownSlot); Last && Time - *Last < Cooldown)
			{
				return;
			}
			LastSentTime.Add((uint8)CooldownSlot, Time);
		}
	}

	// 리슨 호스트 본인이면 로컬에서 바로 실행된다(따로 로컬 호출하지 않는다 — 이중 재생 방지)
	Client_PushAnnouncerEvent(Event);
}

void UAnnouncerComponent::ServerRebindPawn()
{
	const AController* Controller = Cast<AController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UHealthComponent* Health = Pawn ? Pawn->FindComponentByClass<UHealthComponent>() : nullptr;
	UOxygenComponent* Oxygen = Pawn ? Pawn->FindComponentByClass<UOxygenComponent>() : nullptr;
	if (Oxygen != ServerOxygen.Get())
	{
		if (UOxygenComponent* Old = ServerOxygen.Get())
		{
			Old->OnDrainBoostStarted.Remove(DrainHandle);
		}
		DrainHandle.Reset();
		ServerOxygen = Oxygen;
		if (Oxygen)
		{
			DrainHandle = Oxygen->OnDrainBoostStarted.AddUObject(this, &UAnnouncerComponent::HandleDrainBoostStarted);
		}
	}
	if (Health == ServerHealth.Get())
	{
		return;
	}
	if (UHealthComponent* Old = ServerHealth.Get())
	{
		Old->OnDamageTaken.Remove(DamageHandle);
	}
	DamageHandle.Reset();
	ServerHealth = Health;
	if (Health)
	{
		DamageHandle = Health->OnDamageTaken.AddUObject(this, &UAnnouncerComponent::HandleDamageTaken);
	}
}

void UAnnouncerComponent::HandleDamageTaken(float Amount, AActor* DamageInstigator, FName DamageType, bool bLethal)
{
	// 치명타는 사망 화면이 맡는다. 질식은 산소 안내가, 열수는 분출 경고가 이미 말했다
	static const FName Suffocation(TEXT("Suffocation"));
	static const FName LeftBehind(TEXT("SubmarineLeftBehind"));
	static const FName Vent(TEXT("HydrothermalVent"));
	if (bLethal || Amount <= 0.f || DamageType == Suffocation || DamageType == LeftBehind || DamageType == Vent)
	{
		return;
	}

	// 몬스터 BP 는 DamageType 을 비워 보낸다. 지렁이처럼 폰이 아닌 액터 몬스터도 있으므로 "플레이어가 아닌 가해자"면 공격으로 분류
	const bool bMonster = DamageType.IsNone() && IsMonsterInstigator(DamageInstigator);

	FAnnouncerEvent Event;
	Event.Type = bMonster ? EAnnouncerEvent::Attacked : EAnnouncerEvent::Damaged;
	ServerSend(Event);
}

bool UAnnouncerComponent::IsMonsterInstigator(const AActor* InstigatorActor) const
{
	if (!InstigatorActor)
	{
		return false;
	}
	const AController* Controller = Cast<AController>(GetOwner());
	if (Controller && InstigatorActor == Controller->GetPawn())
	{
		return false;
	}
	const APawn* InstigatorPawn = Cast<APawn>(InstigatorActor);
	return !(InstigatorPawn && InstigatorPawn->IsPlayerControlled());
}

void UAnnouncerComponent::HandleDrainBoostStarted(AActor* InstigatorActor)
{
	// 물기·촉수 잡기처럼 피해 전에 시작하는 몬스터 공격(모든 몬스터 BP 공격 지점이 산소 흡입 가속을 건다). 피해 안내와 간격을 공유한다
	if (IsMonsterInstigator(InstigatorActor))
	{
		FAnnouncerEvent Event;
		Event.Type = EAnnouncerEvent::Attacked;
		ServerSend(Event);
	}
}

void UAnnouncerComponent::Client_PushAnnouncerEvent_Implementation(const FAnnouncerEvent& Event)
{
	// 로딩 중 이전 월드 RPC 가 늦게 와도 Enqueue 가 현재 탐사 컨텍스트를 확인한다
	if (IsLocal())
	{
		if (!LocalContext.IsValid())
		{
			LocalTick();
		}
		Enqueue(Event);
	}
}

// ---------------------------------------------------------------- 로컬

bool UAnnouncerComponent::IsLocal() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC && PC->IsLocalController();
}

double UAnnouncerComponent::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UAnnouncerComponent::PushLocal(const FAnnouncerEvent& Event)
{
	Enqueue(Event);
}

void UAnnouncerComponent::ResetForNewWorld()
{
	Queue.Reset();
	Current.Reset();
	if (Display.bVisible)
	{
		Display.bVisible = false;
		++Display.Serial;
		DisplayChanged.Broadcast();
	}
	LocalContext.Reset();
	LocalLastTime.Reset();
	bRoundOver = false;
	bStartQueued = false;
	StartAt = -1.0;
	bSeenDoorOpen = false;
	bReturnInProgress = false;
	bTimeArmed = false;
	bLocalDead = false;
	NextShowAt = 0.0;
	LastOxygenBand = 0;
	LocalPawn.Reset();
	if (UOxygenComponent* Oxygen = BoundOxygen.Get())
	{
		Oxygen->OnOxygenChanged.RemoveDynamic(this, &UAnnouncerComponent::HandleOxygenChanged);
	}
	BoundOxygen.Reset();
}

void UAnnouncerComponent::LocalTick()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AGameStateBase* GameState = World->GetGameState();
	if (GameState != LocalContext.Get())
	{
		ResetForNewWorld();
		LocalContext = GameState;
		ContextStartTime = Now();
	}

	AExplorationGameState* Exploration = Cast<AExplorationGameState>(GameState);
	if (!Exploration || bRoundOver)
	{
		return;
	}

	// 정산·실패 진입은 즉시 발화 중단. 다시 탐사 월드가 오기 전까지 새 대사를 받지 않는다
	const EExpeditionState State = Exploration->GetCurrentState();
	if (State == EExpeditionState::Settlement || State == EExpeditionState::Failed)
	{
		bRoundOver = true;
		ClearAll();
		return;
	}

	// 탐사 진입 오프닝 동안은 판정·발화하지 않는다(아나운서 위젯도 오프닝 뒤에 생긴다). 시작 안내는 HUD가 뜬 뒤 StartDelaySeconds.
	if (const IExpeditionEntryBackend* Entry = GetOwner() ? GetOwner()->FindComponentByInterface<IExpeditionEntryBackend>() : nullptr)
	{
		if (Entry->GetEntryView().Context != Exploration || !Entry->GetEntryView().bHUDVisible)
		{
			return;
		}
	}

	RebindLocalPawn();

	// 사망: 개인·생존자 안내를 지우고 팀 시간 안내만 남긴다(관전 중에도 표시)
	const UHealthComponent* Health = LocalPawn.IsValid() ? LocalPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bDead = Health && Health->IsDead();
	if (bDead && !bLocalDead)
	{
		bLocalDead = true;
		DropScope(EAnnouncerScope::Personal);
		DropScope(EAnnouncerScope::Survivor);
	}
	bLocalDead = bDead;

	// 시작 안내: 탐사 월드·내 폰이 잡힌 뒤 1회
	if (!bStartQueued && LocalPawn.IsValid() && State == EExpeditionState::Exploration)
	{
		bStartQueued = true;
		StartAt = Now() + StartDelaySeconds;
	}
	if (StartAt >= 0.0 && Now() >= StartAt)
	{
		StartAt = -1.0;
		FAnnouncerEvent Event;
		Event.Type = EAnnouncerEvent::ExpeditionStart;
		Enqueue(Event);
	}

	// 귀환: 열린 걸 본 도킹 문이 닫히는 엣지(HandleReturn 의 SetOpen(false) 복제). 실패가 뒤따르면 위 상태 검사가 지운다
	if (const UDockingDoorComponent* Door = Exploration->GetDockingDoorComponent())
	{
		if (Door->IsOpen())
		{
			bSeenDoorOpen = true;
		}
		else if (bSeenDoorOpen && !bReturnInProgress)
		{
			bReturnInProgress = true;
			RefreshCurrentText();
			Queue.RemoveAll([](const FQueued& Item) { return Item.Event.Type == EAnnouncerEvent::ExpeditionStart; });
			FAnnouncerEvent Event;
			Event.Type = EAnnouncerEvent::ReturnStarted;
			Enqueue(Event);
		}
	}

	// 제한시간: 서버 마감시각 기준. 무장 시점에 이미 지난 단계는 재생하지 않고, 한 번에 여러 단계가 지나면 가장 급한 것 하나만
	if (Exploration->HasTimeLimit() && Now() - ContextStartTime >= TimeArmDelay)
	{
		const float Remaining = Exploration->GetRemainingSeconds();
		if (!bTimeArmed)
		{
			bTimeArmed = true;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				TimeArmed[Index] = Remaining > TimeThresholds[Index] + .5f;
			}
		}
		else
		{
			int32 Hit = INDEX_NONE;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (TimeArmed[Index] && Remaining <= TimeThresholds[Index])
				{
					TimeArmed[Index] = false;
					Hit = Index;
				}
			}
			if (Hit != INDEX_NONE && Remaining > 0.f)
			{
				FAnnouncerEvent Event;
				Event.Type = (EAnnouncerEvent)((uint8)EAnnouncerEvent::TimeWarning60 + Hit);
				Event.Key = TEXT("Time");
				Enqueue(Event);
			}
		}
	}

	// 표시 수명·대기열
	const double Time = Now();
	if (Current.IsSet() && Time >= CurrentEnd)
	{
		EndCurrent();
	}
	Queue.RemoveAll([Time](const FQueued& Item) { return Item.ExpireTime < Time; });
	if (!Current.IsSet() && Queue.Num() > 0)
	{
		const bool bUrgent = Queue[0].Priority >= UrgentPriority;
		if (Time >= (bUrgent ? LastEndTime + ExitSeconds : NextShowAt))
		{
			const FQueued Next = Queue[0];
			Queue.RemoveAt(0);
			ShowNow(Next);
		}
	}
}

void UAnnouncerComponent::Enqueue(const FAnnouncerEvent& Event)
{
	if (bRoundOver || !Cast<AExplorationGameState>(LocalContext.Get()))
	{
		return;
	}

	if (Event.bCancel)
	{
		Queue.RemoveAll([&](const FQueued& Item) { return Item.Event.Key == Event.Key; });
		if (Current.IsSet() && Current->Event.Key == Event.Key)
		{
			EndCurrent();
		}
		return;
	}

	const FAnnouncerLineDef& Def = AnnouncerCatalog::Get(Event.Type, bReturnInProgress);
	if (Def.Title.IsEmpty() || (bLocalDead && Def.Scope != EAnnouncerScope::Team))
	{
		return;
	}

	FQueued Item;
	Item.Event = Event;
	Item.Priority = Def.Priority;
	Item.ExpireTime = Now() + Def.TtlSeconds;

	auto SameSlot = [&Event](const FQueued& Other)
	{
		return Other.Event.Type == Event.Type || (!Event.Key.IsNone() && Other.Event.Key == Event.Key);
	};

	// 같은 Key·같은 사건은 줄을 늘리지 않고 최신 값으로 갱신(표시 중이면 그 자리에서 교체·유지시간 재시작)
	Queue.RemoveAll(SameSlot);
	if (Current.IsSet() && SameSlot(*Current))
	{
		ShowNow(Item);
		return;
	}

	// 빈 화면이면 바로, 더 급한 사건은 표시 중인 낮은 우선순위를 끊는다(끊긴 쪽은 다시 재생하지 않음)
	// 같은 최상위(100) 위험끼리는 만료가 빠른(더 임박한) 쪽이 먼저다 — 열수 0.8초 예고가 산소 위험 뒤에서 만료되지 않게
	const bool bIdleReady = !Current.IsSet() && (Now() >= NextShowAt || Item.Priority >= UrgentPriority);
	const bool bMoreImminent = Current.IsSet() && Item.Priority >= 100 && Item.Priority == Current->Priority && Item.ExpireTime < Current->ExpireTime;
	if (bIdleReady || bMoreImminent || (Current.IsSet() && Item.Priority > Current->Priority))
	{
		ShowNow(Item);
		return;
	}

	Queue.Add(Item);
	Queue.StableSort([](const FQueued& A, const FQueued& B)
	{
		return A.Priority != B.Priority ? A.Priority > B.Priority : A.ExpireTime < B.ExpireTime;
	});
	while (Queue.Num() > MaxQueue)
	{
		Queue.Pop();
	}
}

void UAnnouncerComponent::ShowNow(const FQueued& Item)
{
	Current = Item;
	const FAnnouncerLineDef& Def = AnnouncerCatalog::Get(Item.Event.Type, bReturnInProgress);
	const int32 Chars = Def.Title.ToString().Len() + Def.Line.ToString().Len();
	const float Hold = FMath::Clamp(BaseHoldSeconds + FMath::Max(0, Chars - 24) * .08f, BaseHoldSeconds, MaxHoldSeconds);
	CurrentEnd = Now() + Hold;
	UE_LOG(LogAnnouncer, Verbose, TEXT("표시 %s (우선순위 %d, 유지 %.1fs, 대기 %d)"),
		*StaticEnum<EAnnouncerEvent>()->GetNameStringByValue((int64)Item.Event.Type), Item.Priority, Hold, Queue.Num());
	RefreshCurrentText();
}

void UAnnouncerComponent::RefreshCurrentText()
{
	if (!Current.IsSet())
	{
		return;
	}
	const FAnnouncerLineDef& Def = AnnouncerCatalog::Get(Current->Event.Type, bReturnInProgress);
	Display.bVisible = true;
	Display.Title = Def.Title;
	Display.Line = Def.Line;
	Display.Code = Def.Code;
	Display.Signal = Def.Signal;
	Display.Icon = Def.Icon;
	++Display.Serial;
	DisplayChanged.Broadcast();
}

void UAnnouncerComponent::EndCurrent()
{
	Current.Reset();
	LastEndTime = Now();
	NextShowAt = LastEndTime + GapSeconds;
	if (Display.bVisible)
	{
		Display.bVisible = false;
		++Display.Serial;
		DisplayChanged.Broadcast();
	}
}

void UAnnouncerComponent::ClearAll()
{
	Queue.Reset();
	StartAt = -1.0;
	if (Current.IsSet())
	{
		EndCurrent();
	}
}

void UAnnouncerComponent::DropScope(EAnnouncerScope Scope)
{
	Queue.RemoveAll([this, Scope](const FQueued& Item) { return AnnouncerCatalog::Get(Item.Event.Type).Scope == Scope; });
	if (Scope == EAnnouncerScope::Survivor)
	{
		StartAt = -1.0;
	}
	if (Current.IsSet() && AnnouncerCatalog::Get(Current->Event.Type).Scope == Scope)
	{
		EndCurrent();
	}
}

void UAnnouncerComponent::RebindLocalPawn()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	// 사망 후 관전으로 폰이 빠져도 이전 폰의 사망 상태는 유지한다(폰이 없으면 바꾸지 않음)
	if (!Pawn || Pawn == LocalPawn.Get())
	{
		return;
	}
	LocalPawn = Pawn;

	if (UOxygenComponent* Old = BoundOxygen.Get())
	{
		Old->OnOxygenChanged.RemoveDynamic(this, &UAnnouncerComponent::HandleOxygenChanged);
	}
	UOxygenComponent* Oxygen = Pawn->FindComponentByClass<UOxygenComponent>();
	BoundOxygen = Oxygen;
	LastOxygenBand = 0;
	if (Oxygen)
	{
		Oxygen->OnOxygenChanged.AddDynamic(this, &UAnnouncerComponent::HandleOxygenChanged);
		// 바인딩 시점의 구간은 알리지 않고 기준으로만 잡는다
		const float Max = Oxygen->GetMaxOxygen();
		LastOxygenBand = OxygenBand(Max > 0.f ? Oxygen->GetOxygen() / Max : 0.f);
	}
}

int32 UAnnouncerComponent::OxygenBand(float Ratio) const
{
	if (Ratio <= OxygenCriticalRatio)
	{
		return 2;
	}
	if (Ratio <= OxygenLowRatio)
	{
		return 1;
	}
	// 주의 구간 바로 위(Low~Rearm)는 이전 단계 유지 — 경계에서 반복 발화하지 않게
	return Ratio > OxygenRearmRatio ? 0 : FMath::Min(LastOxygenBand, 1);
}

void UAnnouncerComponent::HandleOxygenChanged(float CurrentOxygen, float MaxOxygen)
{
	// 최대 산소는 강화로 바뀌므로 항상 현재/최대 비율로 판정(최대 0 = 빈 상태)
	const int32 Band = OxygenBand(MaxOxygen > 0.f ? CurrentOxygen / MaxOxygen : 0.f);
	const int32 Previous = LastOxygenBand;
	LastOxygenBand = Band;

	FAnnouncerEvent Event;
	Event.Key = OxygenKey;
	if (Band > Previous)
	{
		Event.Type = Band == 2 ? EAnnouncerEvent::OxygenCritical : EAnnouncerEvent::OxygenLow;
		Enqueue(Event);
	}
	else if (Band == 0 && Previous > 0)
	{
		// 회복하면 남아 있는 산소 대사를 퇴장시킨다
		Event.bCancel = true;
		Enqueue(Event);
	}
}
