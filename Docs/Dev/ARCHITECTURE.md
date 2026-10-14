# GoHome 기술 아키텍처 (개발용)

> `Docs/Dev/` 문서군. 코드로 알 수 있는 것(클래스/필드 목록)은 담지 않는다 — 컴파일러가 강제하지 않는 경계 규칙·코드만 봐선 안 보이는 계약·결정 이유만 다룬다. 인일/복잡도는 GoHome 기술분석서(Notion), 정확한 시그니처는 `Source/GoHome/Public/` 헤더가 각각 유일한 출처.
>
> AI 내부 구현(상태 머신·조향 로직)은 팀원 재량이라 다루지 않는다 — 데미지 전달·소음 감지·도킹 문 상태 구독 등 경계 인터페이스만 고정한다.
>
> 기획 문서(GoHome 기획서·기술분석서)는 Notion에 있고 이 저장소엔 없다 — 설계 근거가 필요하면 사용자에게 요청할 것.

## 목차

- [모듈/폴더 구조](#모듈폴더-구조)
- [시스템별 결정과 경계](#시스템별-결정과-경계)
- [시스템 간 인터페이스 계약](#시스템-간-인터페이스-계약)
- [공유 헤더 규칙](#공유-헤더-규칙)
- [남은 의존성](#남은-의존성)
- [2인 협동 게이트 실제 배치](#2인-협동-게이트-실제-배치)
- [Replication 권한 원칙](#replication-권한-원칙)

## 모듈/폴더 구조

`Source/GoHome/`은 기능별 분리(`Player`/`Interaction`/`AI`/`Item`/`UI`/`Save`)를 따르되 `Core/`를 추가 신설 — GameState 상태 머신·세션/로비 흐름은 특정 시스템이 아닌 게임 흐름 자체라서.

`Core/` 내부: 흐름 제어 클래스(GameMode/GameState/PlayerController/SessionSubsystem, 월드 미배치)는 바로 아래, 월드 배치 액터(`AZoneSelectMonitor`, `ADepartureButton`)는 `Core/Actors/`로 분리(`Item/AItemActorBase`처럼 배치형은 성격이 달라 구분).

폴더별 클래스/인터페이스 출처는 `Source/GoHome/<폴더>/`. 아래 표는 코드에 없는 정보만 — 성격이 섞인 폴더의 비고("정식 홈"이 아니라 "일단 여기" — 기준은 아래 "새 폴더를 파야 하는가"):

| 폴더 | 대응 시스템 | 비고 |
|---|---|---|
| `Core/` | 1. 온라인 서브시스템 + 보이스 (세션/게임 흐름/도킹 문/보이스챗) | 보이스챗은 세션/흐름과 무관한 독립 서브시스템 — 얹혀 있을 뿐 |
| `Player/` | 2. 이동 + 사운드 + 환경 위해요소, 3. 산소 + HP | 환경 위해요소(수류/열수구)는 이동 시스템과 무관한 독립 시스템 — 얹혀 있을 뿐 |
| `Interaction/` | 4. 상호작용/운반/납품/협동 게이트 | 협동 게이트는 원래 `Core/` 배치가 검토됐다가 상호작용 트리거라는 이유로 여기 옮겨짐 |
| `AI/` | 5. AI 몬스터 | - |
| `Item/` | 4. 상호작용/운반/납품 (아이템 정의 + 물리/부력) | - |
| `UI/` | 6. UI | 신규 화면은 화면군 하위 폴더(`UI/Title/`)에 C++ 베이스. 기존 BP 전용 화면은 해당 화면 작업 시에만 이주 |
| `Save/` | 9. 세이브 데이터 스키마, 8. 장비 강화(미구현) | - |
| `Shop/` | 아이템 상점 + 잠수정 공유 보관함 | 보관함 상자 액터(`ASharedLockerActor`)는 상점 단말(`AShopStation`)과 같은 상호작용 스테이션이라 `Interaction/`에 있다 |

`Public/`·`Private/` 최상위 배치 규칙은 [CODING_CONVENTIONS.md 폴더 규칙](CODING_CONVENTIONS.md#폴더-규칙) 참고.

### 새 폴더를 파야 하는가

기준: 기술적 의존만으론 기존 폴더에 끼워 넣지 않는다 — 전용 Component/Actor 세트를 갖는 독립 시스템이면 새 폴더("이 시스템만의 클래스 개수"로 판단). 실제 사례는 위 표 "비고" 열 참고.

## 시스템별 결정과 경계

폴더별 결합도 경계·결정 이유만(클래스/필드/구현 상태 출처는 위 "모듈/폴더 구조").

yaml 세 키: `decoupling`(안 바뀌는 경계) · `decisions`(반려된 대안 있을 때만 사유 남기고 단순 상태갱신은 덮어씀 — 상태 태그 없음, 코드가 출처) · `known_gaps`(코드만 봐선 놓치는 함정 — 해소되면 항목 삭제).

### Core
```yaml
decoupling:
  - 도킹 문 상태는 GameState가 아니라 UDockingDoorComponent가 별도 소유한다 — GameState는 탐사 진행 단계(로비/탐사/정산 등)만 책임진다. (구현 확인: `AGoHomeGameState`가 생성자에서 `UDockingDoorComponent`를 서브오브젝트로 소유하지만, 문 개폐 bool 자체와 `OnDoorStateChanged`는 컴포넌트가 갖는다 — "GameState 미참조"는 이 상태값에 대한 이야기이지 포인터를 안 갖는다는 뜻이 아니다.)
  - AI와 ASubmarine 둘 다 도킹 문 상태를 읽을 때 GameState 전체가 아니라 UDockingDoorComponent의 공개 상태(IsOpen(), OnDoorStateChanged)만 구독하고 GameState는 참조하지 않는다 — 게임 흐름 전체와 결합되지 않도록.

decisions:
  - name: 잠수정 오브젝트 구성
    detail: |
      단일 클래스 ASubmarine(허브/문 메시, InteriorVolume 트리거만 정의)을 로비/탐사 맵에 동일 배치, 하나의
      BP_Submarine으로 관리(ZoneSelectMonitor·ADepartureButton은 Child Actor Component). 기능 차이는
      서브클래스가 아니라 런타임 `GetWorld()->GetGameState<ALobbyGameState>()` 유효 여부로 판별한다 —
      서브클래스로 나누면 두 맵의 외형이 각 BP에 따로 들려 드리프트 위험이 생기므로 기각.
        - ZoneSelectMonitor: `ALobbyGameState` 유효할 때만 CanInteract() true
        - ADepartureButton: 로비면 Departure 동작(선택된 존 에셋을 `UExpeditionTravelSubsystem::SetActiveZone`으로
          넘긴 뒤 `ServerTravelViaLoadingScreen`으로 탐사맵 트래블), 탐사맵이면 Return 동작 — 자체 처리 없이
          `AExplorationGameMode::HandleReturn()`에 위임한다. 문 닫기·연출 대기·Settlement 진입·`FinalizeRound(false)`·
          자동복귀 타이머·로비 트래블은 전부 GameMode 소유(실패 경로 `HandleFail`과 같은 곳에 라운드 종료 시퀀싱을 모음).
          할당량·제한 시간 같은 존별 런타임 설정은 `AExplorationGameMode::BeginPlay`가 `ActiveZone` 하나에서 읽는다
          (`SetTargetMapQuota` 호출도 여기, DepartureButton 아님 — 존 데이터 단일 출처). `LobbyMapPath`는
          `GoHomeGameMode`의 실재 필드(기본값 `/Game/GoHome/Maps/LV_Lobby`), 목적지는
          `UExpeditionTravelSubsystem`의 `PendingDestinationMap`(C++ 쓰기 전용, 로딩 맵 BP가 읽음)에 저장 후 `LoadingMapPath`로 먼저 경유.
  - name: 도킹 문 밖 생존자 처리
    detail: |
      문이 닫히는 순간(bOpen: true → false) ASubmarine이 InteriorVolume 밖에 있는 플레이어를 즉시 사망
      처리한다(리썰컴퍼니식 — 동료를 버리고 출발하는 연출). AUnderwaterEnemyBase도 APawn이라 같은 콜리전
      채널로 트리거에 걸리므로, 킬 로직에서 반드시 PlayerState/Controller 보유 여부로 플레이어만 걸러야
      한다(콜리전 채널만으로는 몬스터/플레이어 구분 불가).
  - name: 세션 생명주기
    detail: USessionSubsystem은 UGameInstanceSubsystem(트래블 간 유지). UI는 BP 델리게이트만 구독, GameState는 세션 트리거로 쓰지 않음.
  - name: 레벨 트래블 방식
    detail: "GoHomeGameMode.cpp: bUseSeamlessTravel = true. GameState(따라서 서브오브젝트인 DockingDoorComponent)는 트래블마다 새로 스폰되므로, Save뿐 아니라 도킹 문 상태를 구독하는 AI/ASubmarine도 매 레벨 BeginPlay에서 재구독해야 한다. 전환용 임시 GameState 구간의 재구독 타이밍은 미검증."
  - name: 상태 전이 트리거 (EExpeditionState 7종)
    detail: |
      - Lobby → Departure: 출발 버튼 상호작용 즉시(전원 확정 없음). **주의**: `SetState(Departure)`는 `ServerTravelToMap` 내부에 무조건 있어, `ServerTravelViaLoadingScreen`(Return도 재사용)을 포함한 모든 서버 트래블에서 발동 — 이 전이 전용이 아니다.
      - `ZoneSelect`: 미사용 — `LobbyGameState::SelectedZoneId` 값 변경만으로 처리(상태 전이 아님)
      - Departure → Exploration: 트래블 완료 후 `ExplorationGameState` 생성자가 즉시 초기화. 실제 탐사 개시는 진입 게이트 뒤 — `AExplorationGameMode::BeginPlay`가 존 데이터로 진입 스냅샷(`FExpeditionEntryState`, 복제)을 Preparing으로 싣고, 각 클라의 `UExpeditionEntryComponent`가 폰을 받으면 `Server_ReportEntryReady` → 전원 준비(트래블 중 인원 0, 또는 `EntryReadyTimeoutSeconds` 20s) 시 Opening(시작 서버시각 = 지금+`OpeningStartDelaySeconds` 2s — 맵 로드 직후 스트리밍·셰이더 준비 끊김을 검은 대기 화면 뒤로 보낸다, `OpeningDurationSeconds` 5.74s — 위젯 타임라인과 같은 값) → `StartExploration`이 Active로 바꾸며 모든 플레이어 산소를 최대치(강화 포함 — 보너스는 폰 BeginPlay 뒤에 최대치만 올린다)로 채우고 제한시간 마감·`TimeLimitTimer`를 건다. 도킹 문은 `InitGameState`(모든 액터 BeginPlay 전)에서 미리 열고, BP_Submarine은 BeginPlay에 이미 열려 있으면 연출 없이 열린 각도로 놓는다 — 탐사맵은 문이 완전히 열린 채 시작(사용자 결정 2026-10-14). 그 전엔 귀환(`HandleReturn`)·이동·상호작용이 막힌다(UI 절 "탐사 진입 오프닝").
      - Exploration → Settlement: 귀환 버튼 상호작용 → `AExplorationGameMode::HandleReturn()` → `DockingDoorComponent->SetOpen(false)`(→ `ASubmarine`이 `InteriorVolume` 밖 플레이어 즉시 사망 처리) → `DoorCloseTimer`(`DoorCloseDelay` 기본 8초, 문 닫힘 연출 대기) 만료 시 `EnterSettlement` → `SetState(Settlement)` + `FinalizeRound(bForfeited=false, CasualtyNames)` → `AutoReturnDelay` 후 `ReturnToLobby`. 버튼 연타 재진입은 `bReturnPending`, 종료 락은 `bRoundResolved`. `EExpeditionState::Return` 값은 여전히 미사용.
      - Exploration/Settlement → Failed: 제한 시간 만료, 도킹 문 위협 판정, 생존자 0명(→ Player 절 HP 0 처리) 중 하나 → `AExplorationGameMode::HandleFail`(SetState(Failed) + FinalizeRound(bForfeited=true) + 자동복귀 타이머). 전원사망은 `CheckAllDead`가, 타임오버는 `AExplorationGameMode::StartExploration`(탐사 개시)이 존 `TimeLimitSeconds`로 건 `TimeLimitTimer`(만료 시 `Fail(EFailReason::TimeExpired)`)가, 도킹문 위협은 `AGoHomeGameState::Fail(EFailReason)`가 각각 `HandleFail`로 라우팅한다(모두 서버). `HandleFail`·`EnterSettlement`는 진입 시 `TimeLimitTimer`를 `ClearTimer`하고, `HandleFail`은 `DoorCloseTimer`도 끊으므로, 귀환 연출 중(아직 `EnterSettlement` 전) 마지막 생존자까지 밖에서 죽거나 시간이 만료되면 정산이 아니라 실패(forfeit)가 선점한다. **AI 도킹문 위협 콜러만 아직 미구현.**

known_gaps:
  - name: 실패 사유별 진입점
    detail: "타임오버·전원사망은 콜러까지 배선 완료(`TimeLimitTimer`, `CheckAllDead`). `AGoHomeGameState::Fail(EFailReason)` 포워더도 살아있으나, 이를 호출하는 AI 도킹문 위협 판정만 미구현(미연결 스텁 전체는 `남은 의존성`)."
```

### Player
```yaml
decoupling:
  - "실제 경계는 3단이다: `UInventoryComponent`(Interaction)가 `IWeightProvider`(Interaction)를 구현해 원시 무게 합을 노출하고, `UCarryWeightComponent`(Player, 신설 — 이 문서에 이전까지 누락)가 같은 Owner의 `IWeightProvider` 구현체를 찾아 합산한 뒤 최대무게/업그레이드를 반영해 `ICarryWeightProvider`(Player, `CarryWeightProvider.h`)로 재노출한다. `UOxygenComponent`는 `IWeightProvider`가 아니라 이 `ICarryWeightProvider`만 참조한다 — Interaction 원시 무게가 아니라 Player가 계산한 초과무게 결과만 본다."
  - "UHealthComponent는 \"누가 데미지를 주는지\" 몰라야 한다 → IDamageable(아래 인터페이스 계약 참고)로 노출해 호출자가 Character 타입을 몰라도 되게 한다."
  - "UHealthComponent는 IDeathNotifier도 구현한다 — Interaction(`UInventoryComponent`)이 사망 시점에 아이템을 드롭시켜야 하는데, HealthComponent 구체 타입 대신 이 인터페이스만 보고 구독한다(구현됨, `InventoryComponent::BeginPlay`에서 `FindComponentByInterface`로 구독)."

decisions:
  - name: 산소 0 → 데미지 경로
    detail: UOxygenComponent가 산소 0시 매 틱 IDamageable::ApplyDamage(질식 데미지, Owner, "Suffocation") 동기 호출.
  - name: 중복 사망 방지
    detail: OnDeath는 캐릭터당 탐사 1회만 브로드캐스트 — `AExplorationGameMode`가 생존자 수를 추적하므로 이 보장이 깨지면 카운트가 틀어진다.
  - name: 플레이어 이름(이름표) 출처
    detail: "서버 `AGoHomeGameMode::PostLogin`이 PlayerState UniqueNetId로 OSS `GetPlayerNickname`을 조회해 `SetPlayerName` → 기본 PlayerName 복제. 접속 URL의 `Name=`은 덮어쓴다(클라 자기신고 이름 불신). 클라 RPC로 이름을 보내는 안은 기각(서버 권한 원칙). 표시는 `AGoHomeCharacter`의 `UWidgetComponent`(Screen) + `UPlayerNameplateWidget`이 PlayerState 이름을 매 Tick 비교 반영. Steam OSS는 엔진 설계상 에디터 PIE에서 항상 꺼지므로(`IsRunningGame()`일 때만 활성) 실제 Steam 이름 검증은 `-game` Standalone으로."

decisions:
  - name: HP 0 처리
    detail: "HealthComponent가 서버 전용 OnDeath 브로드캐스트 → `AExplorationGameMode`가 `RestartPlayer`마다 폰의 `IDeathNotifier`를 구독(`TrackPawnDeath`), 사망 시 `DeadPawns`/`CasualtyNames` 집계 후 `CheckAllDead`. 생존자 0명이면 `HandleFail(EFailReason::AllPlayersDead)`. 사망 추적은 GameState가 아니라 GameMode 소유(서버 전용이라 HasAuthority 가드 불필요). 클라가 읽을 사망자 목록만 `AExplorationGameState::DeadPlayers`(PlayerState, 복제)로 미러 — 폰의 `IsDead`는 관련성 거리 밖이면 클라에 없어서 관전 대상 목록·음성 뮤트가 이걸 본다."
  - name: 사인(사망 원인)
    detail: "`UHealthComponent`가 사망 확정 틱에 마지막 `ApplyDamage`의 `DamageType`을 `DeathCause`(복제)로 남긴다. 몬스터 BP는 DamageType을 비워 보내므로 DamageType 없이 AI 폰(비플레이어 Instigator)이 준 치명타는 `Monster`로 기록. 잠수정 문 닫힘 즉사는 `SubmarineLeftBehind`. 문구 매핑은 UI(`USpectateScreenWidget::CauseText`) 몫 — 새 위해요소를 추가하면 DamageType 이름을 거기에도 한 줄."

known_gaps:
  - name: 접속 종료 처리
    detail: "설계 의도: Logout이 사망(OnDeath)과 같은 생존자 집계 경로에 합류(TSet으로 멱등 보장). 현재 Logout은 Super::Logout 한 줄, `AGoHomeGameState::OnPlayerRemovedFromParty`도 빈 함수 — 미연결이라 접속 종료가 생존자 수에 반영되지 않는다. 사망 추적이 `AExplorationGameMode`로 옮겨졌으므로 이탈 처리도 GameMode의 `DeadPawns` 집계에 합류시켜야 한다."
```

### Interaction
```yaml
decoupling:
  - 소켓 어태치는 Player의 스켈레탈 메시 소켓에 접근해야 하므로 두 폴더 사이의 결합 지점이다 — 헬퍼는 Interaction에 두되, Character는 "오른손/왼손 소켓 이름"만 공개 프로퍼티로 노출해 Interaction이 Character 내부 구조를 몰라도 되게 한다.

decisions:
  - name: 동시 픽업 레이스 컨디션
    detail: 동시 픽업 요청 시 한 명만 성공이 보장됨(구현 방식은 아이템 액터 내부).
  - name: 정산 값 전달
    detail: Interaction이 GameState->AddDeliveredValue(int32)를 직접 호출(델리게이트로 감싸지 않음). 수신 측 미연결 — `남은 의존성` 참고.
  - name: 상호작용 대상 탐지 방식
    detail: 로컬 컨트롤 폰에서만 탐지가 동작한다 — 다른 클라이언트의 리플리케이트된 폰까지 트레이스하면 안 된다. 구체 파라미터는 `UInteractionComponent` 소스.
```

### AI (경계만)
```yaml
note: 내부 구현 범위는 문서 상단 note 참고(AI 상태 머신·조향 로직은 팀원 재량).

boundary_interfaces:
  - 공격 시 대상의 IDamageable::ApplyDamage를 호출 — 몬스터는 대상이 Player인지 몰라도 된다.
  - 소음 감지는 발생원이 호출하는 GenerateNoise가 대신 처리 — AI는 발생원이 Player인지 Item인지 몰라도 된다.
  - 도킹 문 위협 판정은 UDockingDoorComponent의 공개 상태만 읽는다(현재 C++/BP 어느 쪽에도 이를 구독하는 코드가 없다 — 아래 known_gaps 참고).

decisions:
  - name: GenerateNoise 호출 방식
    detail: GenerateNoise는 동기 함수로 반경 내 각 몬스터의 IMonsterNoiseListener::OnNoiseHeard를 호출까지만 책임진다. 전이 로직은 각 몬스터의 내부 구현(경계 밖)이 정한다. IMonsterNoiseListener는 C++ 베이스가 아니라 몬스터 BP(`BP_UnderwaterMonster`, `BP_WormBase`)가 직접 구현한다(Blueprint Implemented Interfaces — C++ 부모 상속과 무관, `.uasset`에 `MonsterNoiseListener`/`OnNoiseHeard` 참조로 확인됨. 현재 BP 쪽에서 구현 진행 중). `AMonsterBase`(C++, 이 인터페이스를 포함한 경계 3종을 다 구현)는 실제로는 아무 BP도 상속하지 않는 고아 클래스다 — 아래 known_gaps 참고.

known_gaps:
  - name: AMonsterBase 미사용 (고아 클래스)
    detail: "`AI/MonsterBase.h`의 `AMonsterBase`는 경계 3종(IDamageable 호출·IMonsterNoiseListener 구현·도킹 문 상태 구독)을 다 갖췄지만 아무 BP도 상속하지 않는다. 실제 스폰되는 `BP_UnderwaterMonster`는 `AUnderwaterEnemyBase`(빈 APawn), `BP_WormBase`는 `AActor`를 직접 상속한다. 데미지 처리·도킹 문 위협 구독은 `AMonsterBase` 경유 없이 각 BP가 별도 구현해야 한다(소음 감지는 원래 BP가 직접 구현이라 무관)."
  - name: 도킹 문 위협 판정 미구현
    detail: "수신 측(`AGoHomeGameState::Fail(EFailReason)` → `AExplorationGameMode::HandleFail`)은 배선됐으나, `UDockingDoorComponent::OnDoorStateChanged`를 구독해 위협 판정 후 `Fail(EFailReason::DockThreatened)`을 호출하는 코드가 AI 쪽에 없다."
```

### Shop (상점 + 잠수정 공유 보관함)
```yaml
decoupling:
  - "상점 구매품은 플레이어가 아니라 팀 소유다 — 구매는 자금 차감 + `SaveGame.SharedLockerItems`(상품별 팀 보유 수량) 증가만 하고 액터를 만들지 않는다. 액터는 잠수정 보관함에서 꺼낼 때만 생성돼 핫바(`UInventoryComponent`)에 들어간다. 핫바는 유물 운반·손에 들기용으로 그대로이고 구매품을 저장하지 않는다."
  - "영구/소모 구분은 `FItemShopProduct::Lifetime`(`EItemShopItemLifetime`)과 세이브 항목 `FSharedLockerEntry::Lifetime`이 출처다 — `IsDeliverable()`(납품 여부)에서 추론하지 않는다. 보관함 상품은 `AItemActorBase::IsSharedLockerItem()`으로 납품 정산에서 따로 빠진다."
  - "UI는 수량을 `AGoHomeGameState::SharedLockerItems`(복제 미러, `OnSharedLockerChanged`)로만 읽고, 꺼내기/넣기는 `ISupplyScreenBackend::RequestLockerTransfer`(Core, `AGoHomePlayerController` 구현) 요청만 한다."

decisions:
  - name: 수량 모델 (보유 − 꺼냄 = 보관)
    detail: |
      SaveGame에는 팀 보유 수량(OwnedQuantity)만 저장하고, 꺼내 간 액터는 `UItemShopSubsystem::Checkouts` 런타임 장부(약참조 + `OnEndPlay` 구독)만 안다. 보관함 안 수량 = 보유 − 꺼냄.
      그래서 아무 시점에 세이브가 디스크로 나가도 꺼내 간 것은 "보유"로 남는다(크래시해도 분실로 바뀌지 않음).
      꺼낸 액터가 플레이 중 명시적으로 파괴되면(`EEndPlayReason::Destroyed`, 월드 정리·seamless travel 아님) 보유 −1 — 회복 캡슐은 효과가 실제 적용됐을 때만 Destroy하므로 "HP/산소 가득이면 안 줄어듦"이 자연히 성립하고, 텔레포트 리모컨 소모·납품·바닥 소멸도 같은 경로다. 맵 정리로 사라지면(로비→탐사 트래블 등) 보관함으로 돌아간 것으로 본다.
  - name: 라운드 종료 회수/분실
    detail: |
      `FinalizeRound` 첫머리에서 `ResolveCheckoutsForRoundEnd`: 꺼낸 액터 중 살아있는 플레이어 핫바에 있거나(사망 폰은 이미 `ServerDropAllItems`로 떨굼) `ASubmarine::IsLocationInsideInterior` 안에 있는 것만 회수, 나머지는 분실(보유 −1). 다 쓴 소모품(`AUsableItemBase::IsDepleted`)은 회수 위치와 무관하게 소모 처리.
      회수·분실 모두 액터를 바로 Destroy해 정산 화면 중 재사용/납품으로 수량이 이중으로 움직이지 않게 한다. 영구 장비 분실은 사용자 결정(리썰컴퍼니식 분실), 미사용 소모품은 다음 라운드까지 보관함에 남는다.
  - name: 구매 장소·한도
    detail: |
      서버는 클라가 보낸 `FItemShopPurchaseRequest::Context`를 쓰지 않고 GameState 단계로 판정(Lobby=LobbyLoadout→`bCanBuyInLobby`, Exploration=InGameImmediate→`bCanBuyInGame`, 그 외 거절). 탐사 중 구매도 같은 보관함으로 들어간다.
      `MaxPurchasesPerRun`은 이름만 유지(위젯 바인딩 호환)하고 의미를 "팀 전체·라운드당"으로 바꿨다 — `SaveGame.RoundShopPurchases`에 저장, `FinalizeRound`마다 비움(0 이하 = 무제한). 이전엔 플레이어별 누적이 `ResetSave`에서만 리셋돼 사실상 게임 전체 한도였다. 팀 보유 상한은 `MaxOwnedQuantity`. `bUniquePerPlayer`는 미사용(위젯 핀 호환용으로만 잔존).
      커밋은 검증 전부 통과 후 `TrySpendFunds` → 상품별 `SetLockerOwnedQuantity`, 실패 시 이전 값 복원 + `RefundFunds`.
  - name: 기존 플레이어별 로드아웃 폐기
    detail: |
      반려: 구매품을 플레이어 핫바에 지급하고 라운드 끝에 남은 수량을 플레이어 키(`Player_<PlayerId>`)별로 저장, 다음 탐사 `SetPawn`에서 재지급하던 구조 — PlayerId가 세션마다 바뀌어 저장 보유가 주인을 잃고, 회복 캡슐은 지급 경로(`OnInteract` = 즉시 회복) 때문에 구매 자체가 실패했다.
      `SaveVersion` 0 세이브는 로드 시 `MigrateSaveGame`이 옛 `ItemShopPurchaseStates`의 보유 수량을 상품별로 합산해 보관함으로 옮긴다(카탈로그에 없는 상품은 버림).
  - name: 보관함 꺼내기 서버 검증
    detail: 꺼내기/넣기 RPC마다 서버가 게임 단계(로비/탐사), 요청 폰이 `ASharedLockerActor::UseRadius` 안인지, 보관 수량·빈 핫바 슬롯을 다시 확인한다. 지급은 `AItemActorBase::ServerGrantToPawn`(서브클래스 `OnInteract` 재정의 우회 — 회복 캡슐이 꺼내자마자 쓰이지 않게).
  - name: 상점 표시 데이터
    detail: |
      라운드 구매 누적은 `AGoHomeGameState::RoundShopPurchases`(복제 미러, `OnSharedLockerChanged`로 같이 알림)로 클라에 보낸다 — 상점 "이번 라운드 남은 구매 N개" 표시용. 서버 검증은 여전히 세이브 기준이고 미러는 `RefreshLockerView`·GameState BeginPlay 시점에만 갱신.
      상점·보관함에 보일 이름/설명/평면 도해는 `FItemShopProduct::ShopDisplayName·ShopDescription·ShopArt`(비면 ItemData.DisplayName/ProductId·설명 없음·물음표). `ItemData.DisplayName`은 다른 화면이 쓰는 영문 이름이라 덮지 않는다.
      구매 결과(`FItemShopPurchaseResult`)에는 상품이 없어 요청자 PC가 진행 중 요청(상품·수량, 한 번에 하나)을 기억했다가 결과에 붙여 화면에 알린다(`ISupplyScreenBackend::OnSupplyPurchaseResult`).
  - name: 보관함 상자 역할 · 내 소지 수량
    detail: |
      `ASharedLockerActor` 하나가 맵 단계로 역할을 바꾼다(사용자 결정 2026-10-12): 로비 = 보급 메인 분기(상점 / 조회 전용 보관함), 탐사 = 아이템 박스(보관함 꺼내기·넣기 직행). 로비 "조회 전용"은 UI 규칙만 — 서버 꺼내기·넣기 검증(위 절)은 로비도 그대로 허용한다(사용자 결정). 동작이 갈려 문제가 생기면 오브젝트를 분리하고, 분리 시 상점 오브젝트는 보급 상점 화면을 연다.
      "내 소지(넣기 가능)" = 내 핫바에 있는, 보관함에서 꺼낸 같은 상품 수. 꺼낼 때 `AItemActorBase::SharedLockerProductId`(복제)를 찍고 클라가 자기 인벤토리에서 센다 — 서버 `TryDepositToLocker`가 받는 대상(Checkouts 장부 + 요청자 핫바)과 같은 집합. 팀 반출(`Owned − Stored`)로 추정하지 않는다.
      꺼내기·넣기 요청은 한 번에 하나(`ISupplyScreenBackend::RequestLockerTransfer`, PC가 결과 RPC까지 pending 보존 — 화면을 닫았다 열어도 유지).
  - name: 상점 단말은 로비 전용
    detail: |
      `AShopStation`은 로비에서만 보급 메인 분기를 연다. 탐사 맵에 놓인 단말은 `CanInteract` false + `BeginPlay`에서 Visibility 채널을 Ignore로 바꿔 조준 트레이스에 안 걸린다(프롬프트·외곽선 없음, 사용자 결정 2026-10-12).
      서버의 탐사 중 구매 허용(위 "구매 장소·한도"의 Exploration→`bCanBuyInGame`)은 그대로라 탐사 구매 진입 UI가 없는 상태 — 탐사 구매를 막을지/열지는 미결정.
```

### Item
```yaml
decisions:
  - name: 타이머/충돌 감지 소유
    detail: 파손형의 충돌 속도 감지, 소음 유발형의 "30초 보유마다 +200" 누적 타이머는 아이템 액터 자신이 소유(인벤토리는 무게/개수만 앎).
```

### UI

신규 화면은 위젯마다 C++ 베이스(`UUserWidget` 상속)를 두고, BP 서브클래스는 레이아웃·스타일·`BindWidget`·Widget Animation만 갖는다. 기존 BP 전용 화면(정산·HUD 등)은 그대로 두고 해당 화면을 크게 손볼 때만 이주한다(빅뱅 금지). 화면 구조·레이어·카탈로그·연출·네이밍은 [UI_GUIDE.md](UI_GUIDE.md)가 출처(새 화면 만들 때만). 여기엔 코드 경계에 걸리는 규칙만.

```yaml
decoupling:
  - >
    표시 전용 위젯(게이지·인벤토리·카운트다운)은 복제 프로퍼티/델리게이트를 구독만 한다.
    입력 위젯(탐사 목적지 지정, 강화 구매)은 서버 RPC(`Server_SelectZone` 등)만 호출하고
    로컬 상태를 직접 바꾸지 않는다.
  - >
    화면(정산/게임오버/엔딩 등)은 자기완결 유닛 — 자기 UMG 애니메이션·연출 시퀀스를 소유하고
    Setup(구조체)로 데이터를 받는다. 라우터/부모는 "어느 화면 + 데이터 전달"만, 연출 로직은 갖지 않는다.

decisions:
  - name: 정산 UI 바인딩 대상
    detail: |
      정산/실패/게임오버/엔딩 UI는 state 델리게이트가 아니라 AExplorationGameState::OnSettlementReady(FSettlementResult)에 바인딩 — CurrentState/SettlementResult OnRep 순서 미보장(Save 절 "정산 결과 복제"). 클라는 OnRep 전에 바인딩이 살아있어야 해 PlayerController BeginPlay에 GameState 유효화 가드 필요.
      자동복귀 카운트다운은 클라 로컬 타이머 — AutoReturnDelay는 서버 전용이라 복제 안 되고, GetRemainingSeconds()/ExpeditionDeadline은 탐사 제한시간 전용.
      정산 화면은 라우터가 아니라 스텝 러너(영상 클립 / 페이지 / 전환의 리스트를 상황별 DataAsset에서 뽑아 실행) — UI_GUIDE.md 라우터 패턴 절. 현재는 스텝 리스트 길이 1~2로 기존 동작과 동형.
  - name: 정산 자동복귀 — 2단계 타임라인 (신설 제안)
    detail: |
      현 AExplorationGameMode::AutoReturnDelay(8s 고정, SetSettlementResult 시점 시작)는 연출 길이가 상수일 때만 성립 — 영상/다중 페이지가 붙으면 서버가 모르는 연출 길이를 추측하게 됨.
      목표: Phase1 연출(가변, 클라 스텝 러너 소유, 서버는 안전 타임아웃만) + Phase2 복귀 창(OnPresentationComplete 후 시작). C++: Server_SettlementReady() RPC + ReturnDeadline(복제, 절대 서버시각) + 안전 타임아웃 → 생존 전원 ready(or 타임아웃)에 deadline 세팅 → 만료 시 ReturnToLobby. AutoReturnTimer 2곳(ExplorationGameMode.cpp:111,244) 교체, AutoReturnDelay는 "읽기 창" 값으로 잔존. 클라 리턴 바가 복제 ReturnDeadline 바인딩 → 드리프트 0.
      착수 시점: 영상 클립 실제 도입 직전. 그전까진 8s 상향으로 임시 대응.
  - name: UI C++ 베이스 도입 (타이틀 파일럿)
    detail: |
      이전 결정 "UI/는 C++ 베이스 없음(BP 전용)"을 신규 화면부터 뒤집음 — 상태 전이·포커스·세션 요청 흐름이 BP 그래프에 쌓이면 리뷰·diff·병합(바이너리)이 불가능해서. 타이틀(UI/Title/)이 첫 적용.
      UI는 Core 구체 클래스를 include하지 않고 백엔드 인터페이스만 본다(타이틀: ITitleBackend, ATitlePlayerController가 구현) — 폴더 간 의존은 인터페이스로만이라는 컨벤션 + 디자이너 프리뷰용 목 백엔드 여지.
      CommonUI는 계속 반려 — 타이틀에 필요한 건 패널 1단 스택·포커스 복귀·Escape뿐이라 UTitleScreenWidget 하나로 충분. 화면 간 스택이 깊어지면 재검토.
  - name: 인게임 시스템 메뉴 (UI/Pause)
    detail: |
      타이틀과 같은 백엔드 패턴 — UI는 IPauseMenuBackend만 보고, 위젯 생성·입력 모드·세션 정리는 AGoHomePlayerController가 소유한다.
      나가기는 "USessionSubsystem::DestroySession → OnDestroyComplete(성공)" 뒤에만 OpenLevel(TitleMapPath)/QuitGame. 정리할 세션이 없으면(IP 직접 접속·세션 없이 연 PIE) 바로 이동, 실패·타임아웃은 OnLeaveFailed로 팝업 안 재시도.
      호스트/참가자 구분은 NetMode(Client=참가자)로만 — 정리 API는 같고 문구만 다르다(리슨 호스트가 레벨을 떠나면 참가자는 연결 끊김으로 기본 맵 복귀).
      반려: SetGamePaused(협동 세션 전체가 멈춤). 반려: WBP_TitleSettingsPanel 인스턴스를 그대로 임베드(타이틀 드로어 배경·슬라이드 연출이 딸려 옴) — 대신 UTitleSettingsPanel 클래스를 부모로 한 별도 WBP. 클래스는 백엔드 없이도 동작(GetBackend 미사용)함을 확인.
  - name: 보급 화면 (UI/Supply) — 위젯 트리까지 C++
    detail: |
      로비 보급 오브젝트 → 메인 분기(TEAM READY) → 보급 상점 / 조회 전용 팀 보관함, 탐사 아이템 박스 → 꺼내기·넣기 보관함을 한 Modal(USupplyScreenWidget)이 페이지 교체로 처리한다. 생성·입력 모드·제거는 AGoHomePlayerController(ISupplyScreenBackend), 진입 경로는 ESupplyEntry.
      위 "BP 서브클래스가 레이아웃·Widget Animation" 관례의 예외: 페이지(USupplyPageBase 파생)가 위젯 트리·스타일·모션을 C++로 직접 만들고 WBP가 없다(PC의 SupplyScreenClass 기본값이 C++ 클래스). 이유: 시안(PRESSURE INDEX HTML)의 cubic-bezier 곡선·CSS 수치·line-height/기준선을 그대로 재현하려면 UMG 커브 키·디자이너 배치로는 근사밖에 안 된다(사용자 결정 2026-10-12 "시안을 완벽하게 구현할 수 있는 방식").
      텍스트는 대체 글꼴(Noto Sans KR·Archivo Black — 시안의 맑은 고딕·Arial Black은 배포 불가)을 SupplyUI::Typeset으로 시안 글꼴의 줄 상자·기준선에 맞추고, HarfBuzz FullShaping으로 GPOS 커닝을 켠다. 포커스·선택은 논리 상태로 직접 그리고 Slate 포커스는 호스트가 항상 쥔다(선택 면과 조작 커서 분리 규칙).
      시안의 높이 850 이하 압축 변형(상점·보관함만)은 페이지가 논리 높이 ≤850이면 트리를 압축 수치로 다시 만들고 들고 있던 상태(분류·선택·확인 패널)로 다시 그린다(USupplyPageBase::ApplyViewSize/OnPageRebuilt). UE 기본 DPI 규칙(ShortestSide, 1080→1.0)에선 가로 화면 논리 높이가 항상 ≈1080이라 지금은 발동하지 않는다 — 사용자 결정(2026-10-12)으로 UI 배율 옵션 등에 대비해 구현해 두고, 개발용 GoHome.Supply.ViewHeight 850으로 확인한다. 시안의 모션 축소(prefers-reduced-motion)는 연결할 게임 설정이 없어 생략.
  - name: 탐사 목적지 화면 (UI/ZoneSelect) — 보급 화면과 같은 C++ 트리 방식
    detail: |
      로비 ZoneSelectMonitor → UZoneSelectScreenWidget 한 Modal. 생성·입력 모드·제거와 지정 요청은 AGoHomePlayerController(IZoneSelectBackend). 보급 화면 예외(위 절)를 같은 이유로 따른다 — 사용자 결정(2026-10-13) "C++ 위주, 시안 100%". 사진 보정(CSS filter·mix-blend color·그라데이션)만 UI 머티리얼 M_ZoneSelectPhoto가 sRGB로 합성(UMG 속성으로 대응 불가), 앱 배경 방사 그라데이션은 C++이 런타임 텍스처로 만든다.
      로컬 미리보기·조작 포커스·서버 팀 목적지를 별개 상태로 그린다. 지정은 요청만 보내고 표시·문구는 복제 SelectedZoneId로만 바꾼다(낙관 출력 금지) — 서버 SetSelectedZone은 거절을 알리지 않으므로 PC가 요청 값과 같은 OnRep이 오면 "내 지정", 아니면 "다른 팀원", DestinationRequestTimeoutSeconds 안에 안 오면 실패(그때 이미 같은 값이면 성공)로 판정한다. 재진입은 마지막 미리보기 카드로 포커스 복원(PC가 기억).
      카드 사진·영문명·설명·사진 보정값은 UExpeditionZoneDataAsset 표시 필드(게임 규칙엔 안 씀). 시안의 폭 1000/720 이하 재배치는 16:9 기본 DPI에서 논리 폭이 ≥1700이라 구현하지 않았다(≥1700 규칙만). 맑은 고딕 700~900은 Malgun Bold로 그려지므로 Noto SemiBold, Consolas는 Inconsolata wdth 112 인스턴스(FF_Inconsolata_SemiExpanded)로 대체. 구 WBP_ZoneSelect/WBP_ZoneButton·BP OnOpenSelectZone은 삭제.
  - name: 사망 → 관전 (Core/USpectateComponent + UI/Spectate) — 위젯 트리까지 C++
    detail: |
      로컬 폰 사망 → 자기 시체 시점(≈2.6s, 사망 카드·사인) → 생존 팀원 3인칭 오빗 관전(사용자 결정 2026-10-13, Pack A안 DIVER FEED). 흐름·카메라·입력·위젯 수명은 `AGoHomePlayerController`가 생성자에서 붙이는 `USpectateComponent`가 로컬에서만 소유하고, 위젯(`USpectateScreenWidget`)은 `ISpectateBackend` 스냅샷만 본다(보급·목적지 화면과 같은 C++ 트리 방식, WBP 없음).
      카메라는 로컬 전용 `ACameraActor`(복제 안 함). 클라 화면은 로컬이 정하지만 대상 폰이 관련성 거리 밖이면 클라에 복제되지 않으므로 `Server_SetSpectateTarget`으로 서버 쪽 뷰 타깃만 대상 폰에 맞춘다(엔진 규칙: 뷰 타깃 액터는 항상 관련성 있음). 서버가 보내는 `ClientSetViewTarget`은 관전 동안 `bClientSimulatingViewTarget`으로 무시. 리슨 호스트는 서버 뷰 타깃을 건드리지 않는다(로컬 카메라를 덮음).
      입력은 관전 동안만 푸시하는 입력 컴포넌트(좌클릭/→/D/RB 다음, 우클릭/←/A/LB 이전, 말하기 IA_Talk 재바인딩 — 폰 입력은 사망 시 캐릭터 BP가 끔). 궤도 회전은 축 바인딩 대신 마우스 델타·우스틱을 직접 읽는다(EnhancedInputComponent가 레거시 축 바인딩을 막음). 휠 거리 조절은 없음(사용자 결정). 입력 없이 전환 확인: 비Shipping 콘솔 `GoHome.Spectate.Step <1|-1>`.
      개인 HUD(조준점·LOAD·핫바·상호작용 프롬프트 등)는 캐릭터·컨트롤러 BP가 따로 만들어 붙이는 최상위 위젯이라, 각 BP를 고치는 대신 `USpectateComponent::HiddenHUDClasses` 목록으로 관전 동안 숨기고 끝나면 복원한다 — 개인 HUD 위젯 클래스를 새로 만들면 이 목록에도 추가. 예외: 바이탈 계기(`UVitalsWidget`)는 목록이 사망 프레임에 즉시 접어 시안의 250ms 페이드가 안 보여서 넣지 않고, 복제 `IsDead`로 스스로 페이드해 다음 폰까지 0으로 남는다. 진행도 HUD·보이스 목록·나침반은 유지.
      정산/실패 Sequence가 뜨면(`OnSettlementReady` 또는 상태 복제가 먼저 오면 Settlement/Failed) 관전 화면만 걷고 카메라는 화면 뒤에 둔다. 구 `WB_DeathSpectate`(Developers/LSA 플레이스홀더)와 BP PC의 사망 시 커서·UIOnly 체인은 제거 — 관전 입력을 막았다.
      음성: 사망자 목소리는 생존자에게 안 들린다(사용자 결정). `UVoiceChatSubsystem` 근접 뮤트가 생존자 쪽만 사망자를 `ServerMutePlayer`(한 방향), 사망자는 생존자·다른 사망자를 거리 무관하게 듣고, 사망자 발화는 소음을 내지 않는다.
  - name: HUD 위젯 소유
    detail: |
      상시 HUD 위젯은 BP PlayerController(또는 그 AHUD) 소유 — Pawn/Character 소유 금지. 폰 스코프 데이터(HP·산소·인벤토리)도 위젯은 뷰라 GetOwningPlayerPawn으로 읽고 OnPossessedPawnChanged에 재바인딩. 폰 소유 불가 이유: 사망 시 부활 없이 관전이 수 분 지속되며 그동안도 목표/타이머 HUD가 필요(폰 소유면 관전 내내 검은 화면), 폰은 로비마다 재생성되는 소모품. per-pawn 패널은 "폰 없음/사망" 상태를 명시.
      로비·탐사 공통 상시 진행도 HUD는 AGoHomePlayerController::RefreshProgressHUD가 GameStateSet/SetPawn마다 "현재 GameState가 Lobby/Exploration이고 아직 그 GameState용 위젯이 없으면" 생성한다(클래스는 BP의 ProgressHUDClass). bUseSeamlessTravel이라 컨트롤러가 트래블을 넘어 살아남아 BeginPlay 1회 생성은 다음 맵에서 사라지므로 금지 — 떠나는 월드(bIsTearingDown)에선 만들지 않는다.
      현재 캐릭터 BP·컨트롤러 BP에 흩어짐 → 시스템 PR마다 하나씩 이주(빅뱅 금지). 상세 UI_GUIDE.md.
  - name: 음성 발화자 목록 + 프로필 아바타
    detail: |
      `UVoiceSpeakerListWidget`(UI/HUD)은 진행도 HUD(WBP_ExpeditionProgressHUD)의 LeftColumn에 패널 아래로 내장 — 패널 높이가 로비/탐사마다 달라 별도 뷰포트 위젯의 고정 오프셋 대신 세로 흐름으로 붙인다. `UVoiceChatSubsystem::OnTalkingStateChanged`만 구독(이 머신 OSS voice 상태 그대로, 복제 없음 — 근접 뮤트로 안 들리는 사람은 안 뜬다). 이탈은 PlayerArray 대조로 즉시 제거.
      아바타는 `UPlayerAvatarSubsystem`(Core)이 Steamworks `ISteamFriends::GetMediumFriendAvatar`를 직접 호출해 텍스처 캐시(OSS v1에 아바타 API 없음 → Build.cs에 Steamworks 서드파티 의존). Steam 아닌 환경은 이니셜 원으로 대체. 마이크 없이 확인: 비Shipping 콘솔 `GoHome.Voice.FakeTalk <인덱스> <0|1>`.
  - name: 인게임 HUD 배치 (하단 중앙 콘솔)
    detail: |
      좌상단=정보(진행도 패널 + 그 아래 보이스 목록, 간격 16), 상단 중앙=나침반만, 하단 중앙=인벤토리 핫바(아래 LOAD), 좌하단=바이탈 계기(left 24 / bottom 24, 폭 264).
      핫바는 `UInventoryHotbarWidget`/`UInventorySlotWidget`(UI/HUD, BP 로직 제거) — 캐릭터 BP의 기존 `InitInventory(InInventory)` 호출을 그대로 받는다. 슬롯 크기는 핫바가 런타임 SizeBox로 강제(슬롯 UserWidget desired가 100에 고정되던 원인 미상 문제 회피).
      바이탈은 `UVitalsWidget`(UI/HUD, 트리까지 C++ — 확정 시안 vitals-announcer B안): 산소 바늘 계기 + 큰 강화 레벨을 한 외장에, 디지털 HP(심장·숫자·파형)를 아래 별도 모듈에. 소유 폰 `UOxygenComponent`·`UHealthComponent`에 자가 바인딩, 생성은 `RefreshProgressHUD`(`VitalsClass`, 탐사 월드만 — 구 WB_HP_OxygenUI 가 BP에서 탐사 HUD와 함께만 생기던 것과 같게). 산소 경고(현재/최대 25%·10%)는 바늘·잔량 숫자·상태 문구에만, 강화 등급색(청록→초록→파랑→보라)은 외장 테두리·O₂·LV.에 독립 적용 — 레벨은 복제 안 하고 복제된 MaxOxygenBonus를 DA_OxygenUpgrade 레벨별 누적 보너스와 대조해 역산. 도형은 텍스트와의 겹침 순서(눈금 숫자 < 바늘 < 수치)를 지키려고 뒤판·앞판 두 장의 NativePaint 정점 메시. 시안 mono는 미설치 브라우저가 GulimChe(.5em, 합성 굵기)로 그려서 JetBrains Mono Medium을 측정한 글자 폭에 맞춘다. WB_HP_OxygenUI는 LOAD(WB_CarryWeightStatus)만 남김 — 구 `UOxygenRingWidget`/WBP_OxygenRing·WB_HealthStatus 에셋은 미사용. 기각: 등급별 구간 분할(예리도식) — 사용자 결정.
  - name: 우측 알림 피드 (본인 알림)
    detail: |
      유물 획득·파손(들고 있을 때)·소음 단계 상승·납품을 화면 우측에 띄운다. 본인 알림만 — 서버가 판정 지점(AItemActorBase::ServerGrantToPawn/TryApplyBreakFromImpact/GrowNoiseRadius, UInventoryComponent::ServerDeliverAllItems, ACoopCarryObjectBase::ServerDeliver)에서 문구까지 담은 FHudNotice 를 만들어 UHudNoticeSubsystem::SendToPawnOwner → AGoHomePlayerController::Client_PushHudNotice 로 그 폰의 플레이어에게만 보낸다. 클라는 LocalPlayer 서브시스템(UHudNoticeSubsystem::OnNoticePushed)만 거쳐 UHudNoticeFeedWidget 이 받는다 — Item/Interaction 은 UI 를 include 하지 않는다.
      반려: 클라 OnRep(HoldingPawn/BreakCount/CurrentNoiseRadius) 로컬 감지 — 늦은 접속·관련성 재진입 때 OnRep 이 다시 돌아 오알림이 나고, 리슨 호스트는 OnRep 이 안 돌아 경로가 둘로 갈린다.
      같은 Key(아이템별 소음)는 새 줄 대신 기존 줄 갱신. 피드는 진행도 HUD 와 같은 시점(RefreshProgressHUD, HudNoticeFeedClass)에 (재)생성. 사망은 폰이 남으므로 UHealthComponent::IsDead(복제)까지 보고 숨긴다.
  - name: 유물 조준 정보 (UI/HUD/UItemInspectWidget) — 위젯 트리까지 C++
    detail: |
      줍기 사거리 안 바닥 아이템을 조준하면 대상 주변에 패널 5개(이름·가치·무게·등급·정보)를 리더 라인으로 잇는다(Pack item-inspect A안). 트리거는 소유 폰 `UInteractionComponent::OnInteractableTargetChanged`, 대상은 `AItemActorBase` + `CanInteract`(=미확보, 복제)일 때만. 위치는 매 프레임 메시 바운드 투영으로 다시 계산하고 브래킷·선·레티클은 NativePaint. 생성은 알림 피드와 같은 `RefreshProgressHUD`(`ItemInspectClass`), 관전 중엔 `HiddenHUDClasses`.
      등급(`EItemGrade`)·설명은 `UItemDataAsset` 표시 전용 필드(가치 구간 자동 계산 아님 — 게임 규칙엔 안 씀). 가치가 패널에 나오므로 줍기 프롬프트는 `줍기`만.
  - name: 아나운서 (Core/UAnnouncerComponent + UI/HUD/UAnnouncerWidget) — 위젯 트리까지 C++
    detail: |
      탐사 보조 AI 부이가 우측 상단 통신 컷인(660×184, right 24 / top 48)으로 상황을 한 줄씩 안내한다(확정 시안 vitals-announcer). 흐름·큐·수명은 `AGoHomePlayerController`가 생성자에서 붙이는 `UAnnouncerComponent`, 위젯은 `IAnnouncerBackend` 스냅샷만 보고 등장·퇴장(180ms, Y -8→0)만 소유. 생성은 `RefreshProgressHUD`(`AnnouncerClass`, 탐사 월드만), 관전 중에도 유지(`HiddenHUDClasses`에 넣지 않는다 — 팀 시간 안내).
      판정 위치를 사건 성격으로 나눈다. 팀 진행(시작·귀환·제한시간 60/30/10)과 본인 산소 단계(현재/최대 비율, 더 위험한 구간 진입 때만, 0.30 위로 회복하면 재무장)는 각 클라가 복제 상태(`ExplorationGameState` 마감시각·`UDockingDoorComponent` 열림→닫힘 엣지·`UOxygenComponent`)로 판정한다. 개인 사건(유물 최초 획득·피해·열수 예고·기뢰 작동)은 서버가 판정 지점에서 `UAnnouncerComponent::SendToPawnOwner`/`CancelForPawnOwner` → 컴포넌트 Client RPC로 그 플레이어에게만 보낸다(알림 피드와 같은 경로, 서버 쪽 발화 간격·탐사당 첫 유물은 컴포넌트가 거른다). 피해는 `UHealthComponent::OnDamageTaken`(서버 네이티브 델리게이트)을 컴포넌트가 직접 구독 — 질식·열수·잠수정 즉사·치명타는 다른 안내가 맡아 제외, DamageType 없음 + 플레이어가 아닌 가해자를 "공격"으로 분류(지렁이 `BP_WormBase`는 폰이 아닌 Actor라 "AI 폰" 조건으로는 빠졌다). 피해 없이 시작하는 물기·촉수 잡기는 모든 몬스터 BP가 공격 지점에서 부르는 `UOxygenDrainBoostLibrary::StartOxygenDrainBoostFromInstigator` → `UOxygenComponent::OnDrainBoostStarted`로 잡는다(피해 안내와 간격 공유). 문어 먹물처럼 BP가 로컬 조종 폰에서만 처리하는 연출은 BP가 `UAnnouncerComponent::AnnounceToLocalPlayer`를 부른다(`BP_InkSplatTriggerZone`, 반복은 카탈로그 간격으로 로컬에서 거름).
      반려: 계획안의 "모든 사건을 서버가 판정해 RPC + 클라 준비 핸드셰이크(Server_AnnouncerReady/ContextId)" — 팀 진행 사건은 이미 복제되는 상태에서 결정적으로 나오므로 RPC·준비 확인 없이도 늦은 접속(무장 시점 이전 단계 미재생)·리슨 호스트 이중 재생 문제가 생기지 않는다. 반려: 표시용 대사를 GameState/알림 피드 문구 파싱으로 띄우기.
      큐: 한 번에 1줄 + 대기 최대 3, 우선순위(100 임박 위험·80 위협·60 임무·20 획득) 높은 쪽이 즉시 끊고(끊긴 쪽 재생 안 함), 같은 100끼리는 만료가 빠른 쪽이 먼저, 같은 Key/사건은 갱신, TTL 지난 대기는 버림. 사망 시 개인·생존자 범위를 지우고 팀 시간 안내만 남김, 정산/실패 진입·월드 전환 시 전부 비움. 대사·신호색·정책은 `AnnouncerCatalog`(C++ LOCTEXT).
      밑판(패싯 패널 그라데이션·마스크·BUI 탭·신호선·초상 계기창·초상 그림자)은 텍스트 아래에 깔려야 해서 자식 `UAnnouncerPlate`의 NativePaint가 정점 색 메시로 그린다(UUserWidget::NativePaint는 자식 위에 그려짐). 정점 메시는 텍스처 리소스 핸들이 필요해 `T_Announcer_White`를 쓴다(FCoreStyle WhiteBrush는 색 브러시라 핸들 없음). 초상 `T_Announcer_BUI`, 폴더 `/Game/GoHome/UI/Announcer`는 DirectoriesToAlwaysCook. 시안 mono는 JetBrains Mono Bold(FF_JBMono_Bold), 미설치 브라우저가 그린 Consolas 폭에 맞춰 자간 -0.05em 보정.
      입력 없이 확인: 비Shipping 콘솔 `GoHome.Announcer.Debug <Start|Return|Time60|Time30|Time10|O2Low|O2Crit|FirstArtifact|Artifact|Attacked|Damaged|Vent|Mine|Ink|Clear>`, `GoHome.Announcer.DebugBurst`(선점·대기·만료), 로그 `log LogAnnouncer Verbose`. 공격 "사전 예고"(공격 시작 전 경고)는 몬스터 BP마다 공격 준비 지점 연결이 필요해 아직 없다 — 지금은 물기·잡기 시작 또는 피해 시점 안내.
  - name: 탐사 진입 오프닝 (Core/UExpeditionEntryComponent + UI/Entry/UExpeditionOpeningWidget) — 위젯 트리까지 C++
    detail: |
      탐사맵 도착 → 팀 대기 → INTO THE DEEP 오프닝(5.74s, 시안 expedition-opening) → 탐사 개시. 첫 컷(컷인)은 진입·퇴장 속도는 시안 그대로 두고 판·문구가 모두 멈춰 있는 순간(시안 240ms)에 0.54s 시간을 멈춰 정지 컷만 늘리고(문구 완전 노출 0.13s → 0.67s), 이후 트랙은 그만큼 뒤로 민다(사용자 결정 2026-10-14). 반려: 컷인 전체 1.5배 감속 + 키 구간별 이징 — 등장 속도감이 죽는다. 흐름·위젯 수명은 `AGoHomePlayerController`가 생성자에서 붙이는 `UExpeditionEntryComponent`, 위젯은 `IExpeditionEntryBackend` 스냅샷만 본다(ZOrder 90 — 관전·시스템 메뉴 아래).
      타임라인 시각 = 서버 시각 − `OpeningStartServerTime`이라 모든 클라가 같은 프레임을 보고, 서버는 같은 시각에 탐사를 연다. 늦은 접속·지연 복제는 지난 구간을 재생하지 않는다(서버 시각으로 만료 판정). 개시 뒤 1.3s(`OpeningTailSeconds`)는 도착 안내만 남았다 퇴장.
      시작 위치: `AExplorationGameMode::RestartPlayer`가 PlayerStart(잠수정 안) 대신 `ASubmarine` 로컬 기준 문 앞(원점에서 문 방향 14m, 플레이어마다 옆 1.5m 슬롯)에 세우고 시점을 −15° 내려다보게 한다 — 오프닝 배경이 잠수정 내부면 어색해서(사용자 결정 2026-10-14). 같은 BP_Submarine이라 탐사맵 3개 공통, 끄려면 `bSpawnInFrontOfSubmarine`.
      HUD 지연: PC가 만드는 HUD(진행도·피드·조준 정보·바이탈·아나운서, BP의 `OnExplorationHUDReady`=WBP_HUD)는 `View.bHUDVisible`(오프닝 종료) 전엔 생성 자체를 미룬다. 캐릭터·컨트롤러 BP가 직접 만드는 개인 HUD(인벤토리·상호작용·비네트·나침반·LOAD)는 `LegacyHUDClasses`로 숨겼다 복원 — 개인 HUD를 BP에서 새로 만들면 이 목록에도 추가. 아나운서는 HUD가 보인 뒤부터 판정(시작 안내 = 그 뒤 `StartDelaySeconds`).
      잠금: 로컬은 입력 무시(엔진 `ClientRestart`가 `ResetIgnoreInputFlags`로 지우므로 실제 플래그를 보고 다시 건다), 서버는 폰 `DisableMovement`(BP가 수영 모드로 덮으면 다시 잠금). 상호작용은 Interaction이 GameState 구체 클래스 대신 `IExpeditionPreparation`만 보고 거른다.
      반려: 스킵(시안의 Space/Esc 건너뛰기·빈 데이터 버튼) — 클라마다 화면이 다른 시점에 끝나 동기화가 번거로워 뺐다(사용자 결정 2026-10-14). 반려: 시안의 정적 맵 사진·상단 표기·조준점 — 실제 게임 화면의 대용이라 그리지 않고 브리핑 오른쪽은 실제 월드를 보여준다(사용자 결정 2026-10-14). 화살표(↘ ↗ ⟶)는 시안 글꼴에 없어 브라우저 대체 글꼴로 그려진 모양이라 `UStrokeGlyphWidget` 선으로 옮겼다. Consolas Bold는 Inconsolata wdth 112·wght 700 인스턴스(`FF_Inconsolata_SemiExpanded_Bold`, `SupplyUI::EFace::MonoBold`).
      입력 없이 확인: 비Shipping 콘솔 `GoHome.Opening.Preview <존 1~3|0> [ms] [mode]`(그 시각 정지 또는 재생), 탐사맵 직접 PIE에서 존 데이터 쓰기 `GoHome.Entry.PIEZone <1~3>`.
  - name: 정산 진행도 레일 데이터
    detail: |
      정산표/엔딩의 자금 관문 레일은 전 노드 위치·목표가 필요하나 FExpeditionProgress는 다음 관문 하나만 싣는다.
      전체 스케줄은 FSettlementResult::CheckPointSchedule(TArray<FCheckPoint>)로 정산 시점에 EconomyConfig에서 스냅샷 복사 — WBP_CheckPointRail은 이 배열 + ExpeditionProgress만으로 그린다.
```

### Save
```yaml
decisions:
  - name: 로드/생성
    detail: UGoHomeSaveSubsystem::Initialize()에서 디스크에 세이브 파일이 있으면 로드, 없으면(또는 캐스트 실패) 새 SaveGame 인스턴스를 생성.
  - name: 저장 트리거
    detail: FCoreUObjectDelegates::PostLoadMapWithWorld 구독으로 레벨 전환마다 새 AGoHomeGameState::OnStateChanged에 재구독하고, NewState == Lobby일 때 SaveToDisk() 호출. 재구독 직후 이미 Lobby 상태면 즉시 저장(델리게이트 엣지 트리거 누락 방지). FinalizeRound()도 끝에서 직접 SaveToDisk() 하므로 정산 결과는 트래블 전에 디스크에 남는다.
  - name: 저장 주체
    detail: OnPostLoadMap이 NM_Client면 즉시 리턴 — 호스트만 저장을 수행한다. SaveGame은 호스트에만 실재(클라의 서브시스템은 자기 로컬 빈 세이브만 가짐) → 클라 UI는 세이브를 직접 읽지 못하고 리플리케이트된 struct(FSettlementResult / FExpeditionProgress)로만 상태를 안다.
  - name: 진행/판정 모델
    detail: |
      탐사 1회 = 1턴. 게임오버 경로 2개가 병존:
        - 3스트라이크: 매 턴 유효 납품액 < 그 맵의 MapQuota → QuotaMissCount +1(감소 없음). 3 도달 시 게임오버.
        - 체크포인트: EconomyConfigDataAsset.CheckPoints의 Round(예: 3·6·9) 종료 시 CurrentFunds < 그 체크포인트 목표 → 즉시 게임오버.
      마지막 체크포인트 Round = 게임 총 턴 수 = 엔딩 판정 턴. 목표 달성 시 엔딩 → 세이브 초기화 → 타이틀.
      게임오버·엔딩 모두 ResetSave()(새 SaveGame 인스턴스).
      CurrentFunds: 납품 +, 강화/구매 −, 음수 허용(빚). 강화 투자가 곧 체크포인트 리스크(의도된 텐션).
  - name: 정산 페이즈는 탐사맵 안에서 (트래블 아님)
    detail: |
      귀환/실패가 확정되는 순간(탐사맵, 서버)에 SaveSubsystem::FinalizeRound(bForfeited, CasualtyNames)를 명시적으로 호출해 세이브 데이터를 확정하고, 그 결과 FSettlementResult를 정산 UI로 넘긴다. 로비 도착 감지·라운드진행 플래그 같은 우회 없음.
      - MapQuota는 FinalizeRound 인자가 아니라, ADepartureButton 로비 브랜치가 선택된 존 에셋을 UExpeditionTravelSubsystem::SetActiveZone으로 넘기고, 탐사맵 도착 후 AExplorationGameMode::BeginPlay가 ActiveZone->MapQuota를 읽어 SaveSubsystem::SetTargetMapQuota로 넘긴다(내부 필드명은 CurrentMapQuota, GameInstanceSubsystem이라 트래블 간 유지, FinalizeRound 끝에서 0으로 소비). 같은 BeginPlay가 ActiveZone->TimeLimitSeconds로 제한 시간 타이머도 건다.
      - 정상 복귀: ADepartureButton 탐사 브랜치 → AExplorationGameMode::HandleReturn → SetOpen(false) → DoorCloseTimer(DoorCloseDelay) 후 EnterSettlement → FinalizeRound(false, CasualtyNames) → 그 반환 FSettlementResult를 AExplorationGameState::SetSettlementResult로 넘김 → SetState(Settlement) → AutoReturnDelay 후 ReturnToLobby. UI는 아직 미구현.
      - 완전 실패(전원사망): AExplorationGameMode::CheckAllDead → HandleFail → FinalizeRound(true, ...) (그 턴 납품액 몰수 = CurrentFunds에서 되돌림) → SetSettlementResult(bForfeited=true) → SetState(Failed) → AutoReturnDelay 후 로비 트래블. "구조 실패" 화면 + 자동복귀만(정산표 없음). 실패도 그 턴 스트라이크·체크포인트 판정은 정상 수행. 타임오버는 TimeLimitTimer 만료 → HandleFail(TimeExpired)로 배선됨. 도킹문 위협만 AGoHomeGameState::Fail(EFailReason) 포워더는 살아있으나 콜러(AI)가 아직 없음.
      - Submarine 외부인원 즉사(SetOpen(false) 구독)가 FinalizeRound보다 먼저 동기 실행되므로 사망자 수가 정산에 반영됨 — HandleReturn이 SetOpen(false) 직후 DoorCloseDelay를 기다린 뒤에야 EnterSettlement를 부르므로 이 순서는 자연히 유지된다.
      - bRoundResolved 단일 락이 FinalizeRound 1회·자동복귀 타이머 1회를 보장(EnterSettlement/HandleFail 공유). HandleFail·EnterSettlement은 진입 시 TimeLimitTimer를 ClearTimer하고, HandleFail은 DoorCloseTimer도 끊는다 → 귀환 연출 중 전원사망하거나 시간이 만료되면 실패가 정산을 선점.
      - EExpeditionState의 Failed·Settlement 값은 탐사맵 인맵 페이즈로 실제 사용(HandleFail·EnterSettlement이 SetState). Return 값은 아직 미사용.
      - UI 카운트다운은 AExplorationGameState의 복제 진입 스냅샷(탐사 개시 서버시각 + 제한시간 — 개시 전엔 총 시간 고정)을 GetRemainingSeconds()/HasTimeLimit()로 읽는다(표시 전용, 실제 실패 트리거는 서버 TimeLimitTimer). 상시 진행도 HUD는 이를 베이스 가상 GetTimeLimitProgress(Remaining, Total)(AExplorationGameState만 true)로 매 틱 읽어 맨 위 시간 줄(mm:ss + 바, 남은 비율 25%↓ 주황 / 10%↓ 빨강+깜빡임, 제한시간 없으면 접음)을 그린다 — 별도 타이머 위젯(WBP_HUD_TimeLimit)은 폐기.
      - 상시 진행도 HUD(로비·탐사 공통, UExpeditionProgressHUDWidget)는 AGoHomeGameState(공유 헤더)의 복제 read-model만 읽는다: CurrentRound·FinalRound·CurrentFunds·NextCheckPointRound·NextCheckPointQuota(전부 ReplicatedUsing=OnRep_ExpeditionProgress) + 탐사맵이면 가상 GetMapQuotaProgress(Delivered, Quota)(AExplorationGameState가 MapQuota·RoundDeliveredValue로 오버라이드, 베이스는 false). 바인딩은 OnExpeditionProgressChanged(파라미터 없음) 하나 — 값은 콜백 안에서 Get*()로 꺼내고, 바인딩 직후 1회 당긴다. 초기값은 AGoHomeGameState::BeginPlay(서버)가 SaveSubsystem::BuildProgress()로 SetExpeditionProgress 1회(로비·탐사 둘 다, GameState가 트래블마다 새로 스폰되므로 맵마다 최신), MapQuota는 AExplorationGameMode::BeginPlay가 SetMapQuota. 이후 CurrentFunds는 AddDeliveredValue(납품)·Server_RequestEquipmentUpgrade(강화, 로비 포함)가 베이스 SetCurrentFunds로, RoundDeliveredValue는 SetRoundDeliveredValue로 미러(자금을 먼저 실어 호스트 브로드캐스트 시점에 최신값이 보이도록). 세이브는 여전히 호스트 전용 SoT, GameState 필드는 표시용 미러. OnQuotaProgressChanged(Delivered, Quota)는 기존 구독자(강화 UI)용으로 유지 — AExplorationGameState::NotifyExpeditionProgressChanged가 자금 변화에도 함께 쏜다.
      - 정산 결과 복제: AExplorationGameState::SettlementResult(DOREPLIFETIME, ReplicatedUsing=OnRep_SettlementResult) 한 필드. 서버 SetSettlementResult가 값 대입 + 호스트 로컬 OnSettlementReady 수동 브로드캐스트, 클라는 OnRep이 같은 델리게이트 브로드캐스트. FSettlementResult(중첩 FExpeditionProgress·TArray<FString>·TArray<FCheckPoint> CheckPointSchedule 포함)는 기본 struct 복제로 충분(커스텀 NetSerialize 불필요). 정산/실패 UI는 state 델리게이트가 아니라 OnSettlementReady에 바인딩 — 두 복제 필드(CurrentState/SettlementResult)의 OnRep 순서가 보장되지 않으므로.
  - name: EconomyConfig 로딩
    detail: "UGoHomeSaveSubsystem::Initialize()에서 하드코딩 경로로 LoadObject<UEconomyConfigDataAsset>(/Game/GoHome/Data/DA_EconomyConfig) + ensureMsgf. DeveloperSettings 방식은 클래스 하나 더 필요해 보류. CheckPoints 배열은 Round 오름차순+중복 없음을 UEconomyConfigDataAsset::IsDataValid(#if WITH_EDITOR)가 강제 — FindNextCheckPoint 등은 이 불변식을 가정한다."

known_gaps:
  - name: 정산 배선 (SaveSubsystem 로직 O, 실패·정상복귀 경로 O, 결과 복제 O, 라이브 할당량·자금 미러 O, UI·애셋 X)
    detail: |
      구현됨: UGoHomeSaveSubsystem::AccumulateDeliveredValue / FinalizeRound(forfeit 롤백·CasualtyFee 차감·스트라이크·DetermineOutcome·터미널 시 ResetSave·SaveToDisk) / BuildProgress / ResetSave / SetTargetMapQuota.
      호출부 연결됨: AGoHomeGameState::AddDeliveredValue → AccumulateDeliveredValue(반환값을 AExplorationGameState::SetRoundDeliveredValue로 미러), AExplorationGameMode::BeginPlay → SetTargetMapQuota(ActiveZone->MapQuota) + AExplorationGameState::SetMapQuota, AExplorationGameMode::HandleFail → FinalizeRound(true), AExplorationGameMode::EnterSettlement(귀환) → FinalizeRound(false). 두 경로 모두 FinalizeRound 반환값을 AExplorationGameState::SetSettlementResult로 넘겨 복제(위 decisions "정산 결과 복제" 참고).
      관련 struct: FSettlementResult(ESettlementOutcome, CheckPointSchedule), FExpeditionProgress, FCheckPoint(Data/FCheckPoint.h), UEconomyConfigDataAsset(CheckPoints+CasualtyFee), UGoHomeSaveGame 필드(CurrentFunds/CurrentRoundDeliveredValue/CurrentRound/QuotaMissCount), ExpeditionZoneDataAsset.MapQuota/TimeLimitSeconds.
      미연결분은 `남은 의존성` 참고.
```

## 시스템 간 인터페이스 계약

시스템 경계를 넘는 접점만 모은 표(시그니처 출처는 위 "모듈/폴더 구조" 참고).

| 접점 | 정의 위치 | 호출·구독 측 |
|---|---|---|
| `IWeightProvider` | Interaction | `UCarryWeightComponent`(Player)가 원시 무게 합산에만 소비 — Oxygen은 이 인터페이스를 직접 보지 않음(아래 `ICarryWeightProvider` 참고) |
| `ICarryWeightProvider` | Player (`CarryWeightProvider.h`) | `UCarryWeightComponent`가 구현(최대무게/업그레이드 반영한 초과무게 계산), `UOxygenComponent`(산소 소모 계산)·환경 위해요소(수류 존)·장비 강화(페널티 곡선 파라미터)가 소비 |
| `IInteractable` | Interaction | Item이 구현, 협동 게이트의 레버(`ACoopLeverActor`)가 구현, `UInteractionComponent`(Interaction)가 트레이스로 찾은 대상에 호출 |
| `IDamageable` | Player | Player(HealthComponent)가 구현, AI(몬스터 공격)와 Player 자신(`UOxygenComponent`의 질식 데미지)·환경 위해요소(열수구)가 소비 |
| `GenerateNoise` | AI | Item(구현됨, `ItemActorBase.cpp`)과 Player(구현됨, 수영 중 주기 소음 — `GoHomeCharacter.cpp`의 `SwimNoiseInterval`/`SwimNoiseRadius`, 보이스챗 발화 시)가 발생원. 호출 즉시 서버 동기 처리 |
| `IMonsterNoiseListener` | AI | `BP_UnderwaterMonster`/`BP_WormBase`가 각각 직접 구현(BP 쪽에서 구현 진행 중, C++ 몬스터 베이스 상속과 무관 — AI 절 decisions 참고) |
| GameState 상태 델리게이트 | Core | UI(바인딩), `UGoHomeSaveSubsystem`(진행 지점 저장), Interaction(정산 시점 갱신) |
| 도킹 문 상태 | Core | AI(위협 판정 구독 → `Fail()` — 설계 의도, 현재 미연결. AI 절 known_gaps 참고), `ASubmarine`(안전볼륨 밖 즉사 처리 구독, 구현됨), `AExplorationGameMode::HandleReturn`(귀환 시 `SetOpen(false)` 호출), `UMultiActorGateComponent`(기반 재사용) |
| 인벤토리 슬롯 | Interaction | UI(슬롯별 바인딩) |
| `IDeathNotifier::OnDeath` | Player (`DeathNotifier.h`, `UHealthComponent`가 구현) | Interaction(`UInventoryComponent`가 `BeginPlay`에서 `FindComponentByInterface<IDeathNotifier>()`로 구독 → `ServerDropAllItems` 구현됨), Core(`AExplorationGameMode`가 `RestartPlayer`→`TrackPawnDeath`에서 폰별 구독 → 전원 사망 시 `HandleFail`, 구현됨). 구체 타입을 몰라도 사망 시점을 구독하게 하는 게 목적 |
| `AGoHomeGameState::Fail(EFailReason)` | Core (`GoHomeGameState.h`) | 서버 전용 얇은 포워더 — `HasAuthority` 가드 후 `GetAuthGameMode<AExplorationGameMode>()->HandleFail(Reason)`. 콜러는 AI 도킹문 위협만 남음(미구현) — 타임오버는 `AExplorationGameMode`가 `TimeLimitTimer`로 `HandleFail`을 직접 부른다 |
| 공유 보관함 | Shop (`UItemShopSubsystem`) → Core 미러 | 저장은 `UGoHomeSaveSubsystem`(`SharedLockerItems`), 표시는 `AGoHomeGameState::SharedLockerItems`/`OnSharedLockerChanged`, 요청은 `ISupplyScreenBackend`(Core) → `AGoHomePlayerController` 서버 RPC, 상호작용 진입은 `ASharedLockerActor`(Interaction). 상세 Shop 절 |
| HUD 알림(`FHudNotice`) | Core (`HudNoticeTypes.h`, `UHudNoticeSubsystem`) | Item·Interaction(서버 판정 지점에서 `UHudNoticeSubsystem::SendToPawnOwner`) → `AGoHomePlayerController::Client_PushHudNotice` → `UHudNoticeFeedWidget`(UI). 상세 UI 절 "우측 알림 피드" |
| 탐사 진입(`IExpeditionEntryBackend`, `IExpeditionPreparation`) | Core (`ExpeditionEntryBackend.h`, `ExpeditionPreparation.h`) | `UExpeditionEntryComponent`가 백엔드 구현 → `UExpeditionOpeningWidget`(UI)·`UAnnouncerComponent`가 스냅샷 구독. `AExplorationGameState`가 준비 여부 구현 → `UInteractionComponent`(Interaction)가 탐사 개시 전 상호작용 차단 |
| 아나운서 사건(`EAnnouncerEvent`) | Core (`AnnouncerTypes.h`, `UAnnouncerComponent`) | Item(`ServerGrantToPawn` 유물)·Player(`AHydrothermalVentZone`·`AUnderwaterMine`)가 서버에서 `UAnnouncerComponent::SendToPawnOwner`/`CancelForPawnOwner`, 피해는 `UHealthComponent::OnDamageTaken` 구독 → 컴포넌트 Client RPC → `IAnnouncerBackend` → `UAnnouncerWidget`(UI). 상세 UI 절 "아나운서" |
| 정산/납품 값 흐름 | Interaction → Core → Save | Interaction(딜리버리 존 진입 시 `AddDeliveredValue` 호출), Core(`AGoHomeGameState::AddDeliveredValue` → `SaveSubsystem::AccumulateDeliveredValue` 포워드, 구현됨), Save(스키마·로직 O) |

## 공유 헤더 규칙

**공유 헤더**(`GoHomeGameState.h`, 인터페이스 헤더 — `IInteractable`/`IWeightProvider`/`IDeathNotifier` 등)는 다른 곳에서 include해 **소비만** 한다: 값 변경은 퍼블릭 함수로만 하고 필드 직접 대입은 금지한다(검증: `grep -rn "GameState->\w*\s*=" Source/GoHome/`가 Core 밖에서 나오면 위반).

**단, GameState에 게임 규칙 로직을 쌓지 않는다**: `AGoHomeGameState`는 클라가 읽을 복제 상태(read-model)만 소유한다. 서버 권위 규칙(정산 누적, 파산·게임오버 판정, 파티 이탈 처리)은 서버 전용인 `AGoHomeGameMode`/`AExplorationGameMode` 또는 서버 서브시스템(`UGoHomeSaveSubsystem`)에 둔다 — GameMode는 클라에 존재하지 않아 `HasAuthority` 가드조차 필요 없다. cross-folder 호출부가 GameState를 거쳐야 하면 `AddDeliveredValue`류 퍼블릭 서버 함수는 **로직 없는 얇은 포워더**로 두고 실제 처리는 GameMode/서브시스템에 위임한다.

## 남은 의존성

미연결 스텁 마스터 목록 — 각 절 known_gaps는 여기를 가리킨다.

- **`AGoHomeGameState::Fail` 콜러 부재 / `OnPlayerRemovedFromParty` 빈 스텁**:
  - `Fail(EFailReason)` → `HandleFail` 라우팅은 배선됨(포워더). 남은 미구현 콜러는 AI 도킹문 위협 판정 하나 (전원사망은 `CheckAllDead`, 타임오버는 `AExplorationGameMode::StartExploration`이 건 `TimeLimitTimer`가 각각 `HandleFail` 직접 호출)
  - 접속 종료: `OnPlayerRemovedFromParty` 빈 함수 — GameMode의 `DeadPawns` 집계에 미합류 (생존자 수에 미반영)
  - (`AddDeliveredValue`는 `SaveSubsystem::AccumulateDeliveredValue`로 포워드 완료)
- **도킹 문 위협 판정** — AI가 `OnDoorStateChanged` 구독해 `Fail(EFailReason::DockThreatened)` 호출하는 코드 없음
- **정산 배선 나머지** — 복귀 버튼 RPC, DA_EconomyConfig 애셋 생성, 정산/게임오버/엔딩 UI 위젯. (사망자 추적, 실패 경로, 타임오버 경로, 정상복귀 Settlement 경로 `HandleReturn→EnterSettlement`, 자동복귀 타이머, `FSettlementResult` GameState 복제(`AExplorationGameState::SettlementResult`/`OnSettlementReady`)는 구현됨)
- `Save/` 장비 강화 구매 로직 미구현 — 스키마 필드(`PurchasedUpgrades`)만 있음.
- 레벨/그레이박스는 `Source/GoHome/` 코드가 아니라 레벨 애셋 작업.

## 2인 협동 게이트 실제 배치

`UMultiActorGateComponent`/`ACoopLeverActor`/`AWeightPlateActor`: 도킹 문 컴포넌트 재사용 이유로 `Core/` 배치를 검토했으나 실제론 `Interaction/`에 구현(상호작용 트리거 계열 판단) — 계획과 코드가 갈린 사례(폴더 기준은 "새 폴더를 파야 하는가" 참고).

**동시 활성화 판정**: 트리거별 서버 전용 `bool` 배열 소유, 각 `OnInteract`가 `Server_SetTriggerActive(int32, bool)` 호출 시 같은 호출 안에서 전원 활성 여부를 동기 검사해 게이트를 연다 — 서버는 한 틱에 한 RPC씩 순차 처리하므로 "동시" 입력도 레이스 없음(Interaction 동시 픽업과 같은 논리).

## Replication 권한 원칙

- 서버 권위: 도킹 문 상태, 이동/위치, 산소/HP, 아이템 소유권, 소음 이벤트, 정산/재화, 목표 점수
- 클라이언트: 로컬 폰(autonomous proxy)은 `CharacterMovementComponent` 표준 클라이언트 예측 + 서버 보정, 다른 클라이언트의 폰(simulated proxy)은 순수 보간만 (AGoHomeCharacter는 이동 관련 오버라이드 없이 표준 ACharacter 권한 모델을 그대로 사용). 입력 요청만 (상호작용/구매 — 서버 승인 후 반영)
