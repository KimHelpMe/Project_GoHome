#pragma once

#include "CoreMinimal.h"
#include "ExpeditionEntryTypes.generated.h"

UENUM(BlueprintType)
enum class EExpeditionEntryPhase : uint8
{
	Preparing,
	Opening,
	Active
};

// 서버가 한 번에 복제하는 탐사 진입 스냅샷. 연출 완료와 실제 탐사 개시는 별개다.
USTRUCT(BlueprintType)
struct GOHOME_API FExpeditionEntryState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) EExpeditionEntryPhase Phase = EExpeditionEntryPhase::Preparing;
	UPROPERTY(BlueprintReadOnly) double OpeningStartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) double GameplayStartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) float OpeningDuration = 0.f;
	UPROPERTY(BlueprintReadOnly) float TimeLimitSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 MapQuota = 0;
	// 로비 목적지 목록 순번(1부터). 0 = 모름(탐사맵을 직접 연 PIE 등)
	UPROPERTY(BlueprintReadOnly) int32 ZoneNumber = 0;
	UPROPERTY(BlueprintReadOnly) FName ZoneId;
	UPROPERTY(BlueprintReadOnly) FText DisplayName;
	UPROPERTY(BlueprintReadOnly) FText EnglishName;
	UPROPERTY(BlueprintReadOnly) FText Caption;
};

USTRUCT(BlueprintType)
struct GOHOME_API FExpeditionEntryView
{
	GENERATED_BODY()

	// 완료/스킵에 그대로 돌려줄 월드별 토큰. 이전 위젯의 늦은 콜백은 거부한다.
	UPROPERTY(BlueprintReadOnly) TObjectPtr<UObject> Context = nullptr;
	UPROPERTY(BlueprintReadOnly) FExpeditionEntryState State;
	// 오프닝 위젯을 띄울 구간: 준비(팀 대기) ~ OpeningStart + OpeningDuration + OpeningTailSeconds
	UPROPERTY(BlueprintReadOnly) bool bOpeningVisible = false;
	// 탐사 시작(OpeningDuration) 뒤 오프닝이 더 남아 있는 시간 — 도착 안내 유지·퇴장
	UPROPERTY(BlueprintReadOnly) float OpeningTailSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly) bool bHUDVisible = false;
	UPROPERTY(BlueprintReadOnly) bool bGameplayReady = false;
};
