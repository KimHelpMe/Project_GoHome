#pragma once

#include "CoreMinimal.h"

/** 표시 전용 데이터. 월드/UObject 조회 없이 로딩 화면이 소비한다. */
enum class ETravelPresentationPhase : uint8 { Enter, Loop, Exit, Error, Covered };

struct FTravelPresentation
{
	static constexpr float EnterSeconds = .76f;
	static constexpr float ExitSeconds = .64f;
	int32 Direction = 1;
	ETravelPresentationPhase Phase = ETravelPresentationPhase::Enter;
	float Elapsed = 0.f;
	float MotionTime = 0.f;
	float ExitStartedAt = 0.f;
	bool bReady = false;
	bool bReducedMotion = false;
	bool bCoverPainted = false;
};
