#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SpectateBackend.generated.h"

// 사망 → 관전 흐름 단계(Reference Pack: Docs/Dev/UI/spectate 상태 표). 시간 진행은 USpectateComponent가 소유한다.
UENUM()
enum class ESpectatePhase : uint8
{
	Inactive,	// 살아 있음 / 관전 없음
	Dying,		// 자기 시체 시점 + 사망 카드
	Connecting,	// 첫 대상으로 피드 연결(노이즈) — 대상 폰이 복제될 때까지 머문다
	Spectating,
	TargetLost,	// 보던 대상이 죽음 — 잠시 머문 뒤 다음 생존자로
	NoSurvivor	// 생존자 0 — 실패 화면이 곧 가져간다
};

// 팀 생존 핍 한 칸(PlayerArray 순)
struct FSpectateRosterEntry
{
	bool bSelf = false;
	bool bDead = false;
	bool bCurrent = false;
};

// 관전 화면이 매 틱 읽는 스냅샷. 위젯은 이것만 보고 연출을 정한다.
struct FSpectateView
{
	ESpectatePhase Phase = ESpectatePhase::Inactive;
	float PhaseSeconds = 0.f;		// 현재 단계에 들어온 뒤 경과(실시간)

	FName DeathCause = NAME_None;	// 복제 지연이면 None → 도착하면 채운다
	float DeathElapsedSeconds = -1.f;	// 탐사 시작 후 사망 시각(제한시간 없는 맵이면 -1)

	FString TargetName;
	bool bTargetVitalsValid = false;	// 대상 폰이 아직 관련성 밖이면 false
	float TargetHP = 0.f;
	float TargetHPRatio = 0.f;
	float TargetOxygenRatio = 0.f;
	int32 TargetSlot = 0;			// 1부터, PlayerArray 순(CAM 02 / 04)

	FString LostName;				// TargetLost: 신호가 끊긴 대상

	TArray<FSpectateRosterEntry> Roster;
	int32 AliveCount = 0;

	int32 SwitchSerial = 0;			// 대상이 바뀔 때마다 +1(입력·자동 전환 둘 다) — 위젯 컷 연출 트리거
	int32 SwitchDirection = 1;		// +1 다음, -1 이전
};

UINTERFACE(MinimalAPI)
class USpectateBackend : public UInterface
{
	GENERATED_BODY()
};

// 관전 화면(UI/Spectate)이 보는 유일한 창구. AGoHomePlayerController가 구현한다(Core 구체 클래스 include 금지 규칙).
class GOHOME_API ISpectateBackend
{
	GENERATED_BODY()

public:
	virtual const FSpectateView& GetSpectateView() const = 0;
};
