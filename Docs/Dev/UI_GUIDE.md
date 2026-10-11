# GoHome UI 구조 가이드

> 신규 화면은 C++ 베이스 + BP 레이아웃(첫 적용 `UI/Title/`), 기존 화면은 Blueprint(`Content/GoHome/UI/`) — C++/BP 분담은 [CODING_CONVENTIONS.md](CODING_CONVENTIONS.md#ui-위젯-로직--c로-뺄지-판단-기준-2026-09-18). 이 문서가 코드가 강제 못 하는 UI 구조 규칙의 단일 출처다. 개별 위젯 필드/함수는 담지 않고 화면 분할·소유·연출 배치만 고정한다. 표시 전용/입력 위젯의 데이터 방향은 [ARCHITECTURE.md UI 절](ARCHITECTURE.md#ui)이 출처.

## 목차

- [UI 패턴](#ui-패턴)
- [UI 레이어 모델](#ui-레이어-모델)
- [화면 카탈로그](#화면-카탈로그)
- [HUD 묶음 소유 규칙](#hud-묶음-소유-규칙)
- [화면 위젯 규칙](#화면-위젯-규칙)
- [라우터 패턴 (정산 화면)](#라우터-패턴-정산-화면)
- [연출 규칙](#연출-규칙)
- [UI Reference Pack](#ui-reference-pack)
- [정산 화면군 상세 스펙 (담당자 개인 문서)](#정산-화면군-상세-스펙-담당자-개인-문서)
- [네이밍](#네이밍)

## UI 패턴

**"뷰는 멍청하게, 상태가 민다" (수동 ViewModel).** 위젯은 폴링하지 않고 델리게이트/복제 프로퍼티를 구독만 하며, 화면마다 `Setup(구조체)` 하나로 데이터를 받는다.

반려:

- **UMG MVVM 플러그인** — 보일러플레이트·학습 비용 대비 이득 없음. 화면 십수 개 미만이면 `Setup(struct)`가 ViewModel 몫을 커버. 상점·강화·도감까지 늘고 게임패드 포커스가 필요해지면 재검토.
- **CommonUI** — 메뉴 계층 깊고 콘솔 대응 필요할 때의 프레임워크. 협동 서바이벌 + 화면 소수엔 러닝커브가 이득을 넘음.

## UI 레이어 모델

성격별 층으로 나눈다. 같은 층은 동시에 하나만.

| 레이어 | 예 | 성격 |
|---|---|---|
| HUD | 산소·나침반(`WBP_Compass`)·HP·무게·상호작용 프롬프트(`WBP_InteractionPrompt`)·위험 비네트(`WBP_DangerVignette`)·인벤토리(`WBP_InventorySlots`)·제한시간·할당량 | 상시, 게임플레이 위. 표시 전용 |
| Modal | 존 선택(`WBP_ZoneSelect`), 강화 상점, 잠수정 보관함(`WBP_SharedLocker`), 인게임 시스템 메뉴(`WBP_PauseMenu`) | 게임 위, 입력 잡음. 확정은 Server RPC. **열 때 `bShowMouseCursor`를 켜고 닫을 때 끈다** — 인게임 메뉴는 커서가 켜져 있으면 다른 Modal이 Escape를 소유한 것으로 보고 열리지 않는다(`AGoHomePlayerController::CanOpenPauseMenu`) |
| Fullscreen / Sequence | 정산·게임오버·엔딩(`WBP_Settlement` 계열), 타이틀(`WBP_Title`), 로딩(`WBP_Loading`) | 게임 가림. 자체 enter/exit 연출. 동시에 하나만 |
| System | 세션 진입(`WBP_SessionEntry`), 연결 끊김/에러 | 최상단 |

## 화면 카탈로그

새 화면은 이 표에 한 줄. 소유자 = `Create Widget` + `AddToViewport` 호출 클래스.

| 화면 | 레이어 | 소유자 | 생성/파괴 | 데이터 소스 |
|---|---|---|---|---|
| 정산/게임오버/엔딩 (`WBP_Settlement` 라우터) | Sequence | BP PlayerController | 클라 BeginPlay에 숨겨 생성 + `OnSettlementReady` 바인딩 / 로비 트래블 시 파괴 | `FSettlementResult` (이벤트 인자). 사망자명 = `PlayerState->GetPlayerName()` — 스팀 페르소나가 실리는지 세션 배선 확인, 아니면 로그인 시 `IOnlineIdentity::GetPlayerNickname` → `ServerChangeName` 보정 |
| HUD 묶음 | HUD | BP PlayerController | 탐사 레벨 Possess 후 / 레벨 전환 | GameState 복제 필드 / 컴포넌트 델리게이트 |
| 존 선택 | Modal | 존 셀렉트 모니터 또는 PlayerController | 상호작용 트리거 / 확정·취소 | Zone DataAsset |
| 타이틀 (`UTitleScreenWidget` 부모 WBP) | Fullscreen | `ATitlePlayerController`(C++, `TitleScreenClass`) | 로컬 BeginPlay 생성 / 로비·세션 트래블 시 파괴 | `ITitleBackend`(컨트롤러가 구현 — 검색 스냅샷·세이브 요약·Host/Join 결과) |
| 잠수정 보관함 (`USharedLockerWidget` 부모 `WBP_SharedLocker`, 행 `WBP_SharedLockerRow`) | Modal | `AGoHomePlayerController`(C++, `SharedLockerWidgetClass`) | `ASharedLockerActor` 상호작용 → `Client_OpenSharedLocker`(UIOnly·커서 on) / 닫기·Escape·폰 변경·월드 교체·정산 시작 시 파괴 | `AGoHomeGameState::SharedLockerItems`(복제, `OnSharedLockerChanged`) + 카탈로그 표시명/아이콘. 꺼내기/넣기는 `ISharedLockerBackend` 요청, 결과 문구는 `OnSharedLockerResult` |
| 우측 알림 피드 (`UHudNoticeFeedWidget` 부모 `WBP_HudNoticeFeed`, 행 `WBP_HudNoticeRow`) | HUD | `AGoHomePlayerController`(C++, `HudNoticeFeedClass`) | 진행도 HUD 와 같이 로비·탐사 GameState 마다 (재)생성 / 월드 교체·EndPlay 시 파괴. 사망·폰 없음이면 비우고 숨김 | `UHudNoticeSubsystem::OnNoticePushed`(LocalPlayer) ← 서버 `Client_PushHudNotice`. 본인 알림만 |
| 인게임 시스템 메뉴 (`UPauseMenuWidget` 부모 `WBP_PauseMenu`) | Modal | `AGoHomePlayerController`(C++, `PauseMenuClass`) | 로비·탐사에서 Escape/게임패드 Start로 생성(UIOnly, 로컬 입력만 — 세션은 멈추지 않음) / 닫힘 연출 끝·나가기 성공·월드 교체 시 파괴 | `IPauseMenuBackend`(컨트롤러가 구현 — 역할(호스트/참가자)·복귀·나가기 요청·실패 이벤트). 설정 화면은 타이틀과 같은 `UTitleSettingsPanel` 클래스(레이아웃만 `WBP_PauseSettingsPanel`) |

PIE 주의: 에디터의 Stop 단축키가 Esc라 PIE 창에서 Esc를 누르면 메뉴보다 먼저 PIE가 종료된다(엔진에 끄는 옵션 없음, `DebuggerCommands.cpp`). 메뉴 키 흐름은 Standalone(`-game`)으로 확인하거나 에디터 단축키 Stop을 바꿔서 본다.

## HUD 묶음 소유 규칙

- **상시 HUD 위젯은 BP PlayerController(또는 그 `AHUD`)가 소유. Pawn/Character 소유 금지.** 근거: (1) 데이터가 폰 스코프여도 위젯은 뷰 — `GetOwningPlayerPawn()`으로 읽고 `OnPossessedPawnChanged`에 재바인딩. (2) 사망 시 부활 없이 관전이 수 분 지속되고 그동안도 목표·타이머 HUD가 필요 → 지속 HUD는 필수이고, 폰 소유면 관전 내내 검은 화면 + 별도 관전 HUD를 또 만듦. (3) 폰은 로비 도착마다 재생성되는 소모품이라 폰 소유 위젯은 매 사이클 재생성.
- **per-pawn 패널(HP·산소·무게·인벤토리)은 "폰 없음/사망" 상태를 명시적으로 가짐** — 재바인딩 함수 안 `if` 하나로 패널이 자기 visibility 관리. 토글이 커지면 대안: 캐릭터의 `UHUDPanelComponent`가 `BeginPlay`에 PC HUD NamedSlot로 위젯 주입 / `EndPlay`에 제거 (라이프타임은 폰, 트리는 PC HUD).
- **현황**: HUD 요소가 캐릭터 BP·컨트롤러 BP에 흩어짐. 신규(제한시간·할당량)는 처음부터 PC HUD에 넣어 이주 레퍼런스로 삼고, 기존 요소는 해당 시스템 PR마다 하나씩 이주. 빅뱅 리팩터 금지.
- **데이터**: 제한시간·라운드·다음 관문·자금·할당량 = 로비·탐사 공통 상시 진행도 HUD(`UExpeditionProgressHUDWidget`, 화면 좌상단 한 블록 — 상단 중앙은 나침반·산소)가 `AGoHomeGameState`의 Get*()와 가상 `GetTimeLimitProgress()`/`GetMapQuotaProgress()`(탐사맵만 true)로 읽는다. 시간은 매 틱, 나머지는 `OnExpeditionProgressChanged` 하나에 바인딩 + 바인딩 직후 1회 당김. 생성은 BP가 아니라 `AGoHomePlayerController::RefreshProgressHUD`(seamless travel 대응) — 상세는 ARCHITECTURE.md Save 절.
- **배치**: 팀 공유 상태(제한시간·할당량) 상단, per-player(HP·산소·무게) 하단. 상세는 담당자 개인 문서.

## 화면 위젯 규칙

각 Sequence/Modal 화면은 **자기완결 유닛** — 자기 UMG 애니메이션, `Setup(구조체)` / (필요 시) `Teardown()`, 자기 연출 시퀀스를 소유한다. `Setup` 밖에서 게임 상태를 조회하지 않는다. 라우터/부모는 **어느 화면을 켤지 + 데이터 전달**만 하고 연출 로직을 갖지 않는다.

## 라우터 패턴 (정산 화면)

`WBP_Settlement`는 화면이 아니라 **스텝 러너**. `OnSettlementReady(FSettlementResult Result)`를 받으면 상황에 맞는 **스텝 리스트**를 데이터에서 뽑아 순서대로 실행한다.

**스텝 = 영상 클립 | 페이지 | 전환연출.** `bForfeited` + `Outcome` → 스텝 리스트 매핑은 데이터 주도(DataAsset) — 라우터에 하드코딩 `switch`를 두지 않는다. 새 연출 시퀀스 추가 = 데이터 행 추가, 라우터·페이지 코드 불변.

```
ShowSettlement(Result):
    Steps = PresentationConfig.Resolve(Result)      // bForfeited + Outcome 판정
    for each step:
        Clip → 풀스크린 미디어 레이어 재생 → (EndReached | Skip) → 다음
        Page → Create Widget → SetContent → page.Setup(Result) → page.PlayIntro()
               → page.OnIntroComplete → 다음
    마지막 스텝 후 → OnPresentationComplete 발행 → 자동복귀 창 시작
```

현재 스텝 리스트 (전부 길이 1~2, 확장 여지):

| 상황 | 스텝 |
|---|---|
| `Normal` / `CheckPointPassed` | `[정산표]` |
| `GameOver_Strike` / `GameOver_CheckPoint` | `[게임오버]` |
| `Ending` | `[엔딩]` |
| `bForfeited` (`Outcome` 무시하고 선점) | `[임무 실패]`, `Outcome`이 terminal이면 `+ [게임오버/엔딩]` |

> 확장 예: `GameOver_Strike` → `[정산표, 영상, 게임오버]`(미달·3스트라이크를 정산표에서 보여준 뒤 게임오버로), `Ending` → `[영상, 엔딩]`. 데이터만 바꾸면 됨.

**페이지 위젯 계약**: 각 페이지는 `Setup(FSettlementResult)` / `PlayIntro()` / `OnIntroComplete`(이벤트)만 구현하면 스텝 러너가 종류를 안 가린다. `Setup` 밖에서 게임 상태 조회 금지 — `Result`만 읽는다(`FinalizeRound`가 `ResetSave()` 전에 전부 스냅샷하므로 세이브가 리셋돼도 표시값은 살아있음).

`FSettlementResult` 하나가 `bForfeited` + `Outcome` + 파생값을 모두 실어 다중 페이지 시퀀스도 추가 이벤트 없이 같은 Result로 구동. `DetermineOutcome`은 forfeit와 무관하게 돈다(`GoHomeSaveSubsystem.cpp`) → 3스트라이크·체크포인트 판정이 실패 턴에도 정상 수행되고, 게임오버 페이지는 정상복귀·forfeit 경로 공용.

**체크포인트 레일 데이터**: 정산표가 상시 표시하는 진행도 레일(턴 3/6/9 관문)은 전체 스케줄이 필요하다. `FSettlementResult::CheckPointSchedule`(`TArray<FCheckPoint>`)이 정산 시점에 `UEconomyConfigDataAsset::CheckPoints`를 스냅샷 복사해 싣는다 — 레일은 이 배열 + `ExpeditionProgress`(`CurrentRound`/`FinalRound`/`CurrentFunds`)만으로 그린다.

**페이지 인스턴스화**: `WidgetSwitcher` 말고 **on-demand 생성**(`NamedSlot` + `Create Widget` → `SetContent`). 세션당 한 페이지만 뜨고 각자 인트로가 있어 안 쓸 페이지를 미리 Construct할 이유가 없다. 재검토 트리거:

- 화면군을 **왕복 토글**하기 시작하면 그 부분만 `WidgetSwitcher`.
- 한 페이지에 **수십 행 반복 항목**이 생기면 커스텀 풀이 아니라 UMG `ListView`/`TileView`. 풀스크린 화면 자체는 세션당 1회 생성이라 위젯 풀링 대상이 아니다 — 무거워지는 건 텍스처/연출이지 위젯 수가 아님.
- 페이지가 독립 위젯 + `Setup` 계약이라 라우터의 생성/전환 방식만 바꾸면 됨(페이지 내부 불변). 이행 비용 낮아 지금 안 만듦.

**클라 바인딩 타이밍**: 클라는 `OnSettlementReady`(→ `OnRep_SettlementResult`) 전에 바인딩이 살아있어야 한다. PlayerController BeginPlay에 `AExplorationGameState`가 null일 수 있어 유효화 대기 가드 필요. state 델리게이트가 아니라 `OnSettlementReady`에 바인딩 — CurrentState/SettlementResult OnRep 순서 미보장(ARCHITECTURE.md Save 절 "정산 결과 복제").

**자동복귀 — 2단계 타임라인**: 정산 타임라인은 **연출(가변, 클라 소유) + 복귀 창(고정)** 으로 나뉜다. 서버가 "연출이 얼마나 걸리는지" 모른 채 복귀 시각을 정하면 영상·다중 페이지가 붙는 순간 깨지므로, 연출 길이 결정권은 그걸 아는 스텝 러너에 두고 서버엔 복귀 창 + 안전망만 남긴다.

- **Phase 1 연출** — 스텝 러너 소유. 길이는 클립·페이지 수에 따라 가변. 서버는 이 동안 복귀 타이머를 걸지 않고 **안전 타임아웃**(넉넉히)만 유지.
- **Phase 2 복귀 창** — `OnPresentationComplete` 시점부터 `AutoReturnDelay` 만큼. "이제 읽고 나갈 시간". 만료 시 서버 트래블(`ReturnToLobby` → `ServerTravelViaLoadingScreen(로비맵)`).
- 리턴 바는 **`OnPresentationComplete`에서 시작**(위젯 Construct도, `OnSettlementReady` 수신 즉시도 아님) — 연출 중엔 바 꽉 참 + 라벨 고정. 시작 타이밍·지속시간을 한 곳에서 받아, 나중에 그 소스를 복제 deadline으로 바꿔도 바 로직 불변.

**현재 구현 (단순화, 테스트용)** — 스텝 리스트 전부 길이 1~2. 서버 `AExplorationGameMode::AutoReturnDelay`(8초) 고정 타이머 그대로(`SetSettlementResult` 시점 시작, 복제 안 됨, `GetRemainingSeconds()`/`ExpeditionDeadline`은 탐사 제한시간 전용이라 재사용 불가 → 클라 바는 로컬 타이머 미러, 드리프트 무해). forfeit 2페이지 등 hold 합이 8초를 넘으면 잘림 → `AutoReturnDelay` 상향으로 임시 대응.

**Phase 2 착수 = 영상 클립 실제 도입 직전.** C++ 세부(`Server_SettlementReady` RPC / `ReturnDeadline` 복제 / `AutoReturnTimer` 교체)는 [ARCHITECTURE.md](ARCHITECTURE.md#ui) "정산 자동복귀 — 2단계 타임라인" 절.

- **스킵**: 현재 없음(입력 경로 없음, 타이머 만료가 유일한 복귀 트리거). 긴 영상이 붙으면 "영상만 스킵 → 다음 스텝"(로비 스킵 아님)을 스텝의 `bSkippable` 플래그로 허용 검토.
- **모든 Outcome은 로비로 복귀** — 게임오버/엔딩은 `FinalizeRound`가 `ResetSave()`한 상태로 도착 → 로비가 새 런 허브. "타이틀로" 버튼 없음.

## 연출 규칙

우선순위: (1) UMG Widget Animation — 대부분의 트랜지션. (2) 위젯 BP 소규모 시퀀스(Delay/Timeline) — "카운트업 → 카운터 갱신 → outro" 같은 화면 내부 순서. (3) 화면 간 연출(정산 → 로비 페이드) — 라우터/상위 레이어 소유, 개별 페이지에 두지 않음.

## UI Reference Pack

[UI_REFERENCE_WORKFLOW.md](UI_REFERENCE_WORKFLOW.md)는 신규 화면 또는 화면의 사용자 흐름·시각적 계층·입력·모션을 바꾸는 작업 전에 인터랙티브 시안과 상태·모션·UMG 매핑을 정리하는 절차다. **개인 환경 세팅**(산출물은 `Docs/Dev/UI/`, gitignore) — 팀 전체에 강제하는 규칙이 아니며, 이 문서의 레이어·소유자·라우터 규칙을 대체하지도 않는다. 담당자 개인 환경에 이 설정이 있으면 구현 전에 참고한다.

## 정산 화면군 상세 스펙 (담당자 개인 문서)

페이지별 `FSettlementResult` 매핑, UMG 구현 매핑, 연출 타이밍, 비주얼 아이덴티티는 정산 UI 담당의 개인 문서(`Docs/Dev/_personal/settlement-ui-spec.md`, gitignore). 담당 이관 시 승계. 팀 계약(라우터 패턴·바인딩 대상·로비 복귀·forfeit 연쇄)은 위 [라우터 패턴](#라우터-패턴-정산-화면) 절.

## 네이밍

- `WBP_<화면>` — 화면/라우터 (`WBP_Settlement`, `WBP_Title`).
- `WBP_<화면>_<파트>` 또는 `WBP_<파트>` — 재사용 조각 (`WBP_CasualtyRow`, `WBP_ZoneButton`).
- 폴더는 화면군 단위 (`Content/GoHome/UI/Compass/`).
