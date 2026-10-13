#pragma once

#include "CoreMinimal.h"
#include "AnnouncerTypes.generated.h"

/**
 * 아나운서(탐사 보조 AI 부이) 사건 종류. 문구·표정·우선순위는 AnnouncerCatalog 가 정한다.
 * 팀 진행 사건(시작·귀환·제한시간)은 각 클라가 복제 상태로 판정하고, 개인 사건(유물·피해·열수·기뢰)은 서버가 판정해 보낸다.
 */
UENUM()
enum class EAnnouncerEvent : uint8
{
	None,
	ExpeditionStart,
	ReturnStarted,
	TimeWarning60,
	TimeWarning30,
	TimeWarning10,
	OxygenLow,
	OxygenCritical,
	ArtifactFirst,
	Artifact,
	Attacked,
	Damaged,
	VentWarning,
	MineArmed,
	Count UMETA(Hidden)
};

// 사건 코드 옆 작은 선 아이콘(위젯이 그린다)
UENUM()
enum class EAnnouncerIcon : uint8
{
	Signal,
	Return,
	Oxygen,
	Clock,
	Salvage,
	Hazard
};

// 누가 받는가 — 사망 후 관전에서도 유지되는지를 가른다
enum class EAnnouncerScope : uint8
{
	Team,		// 관전 중에도 표시(제한시간)
	Survivor,	// 팀 사건이지만 사망자에겐 의미 없음(시작·귀환)
	Personal	// 본인 사건(산소·유물·피해·위해요소)
};

/**
 * 서버 → 소유 클라 사건 봉투(UAnnouncerComponent::Client_PushAnnouncerEvent). 그림·문구는 보내지 않는다.
 * Key 가 같은 사건은 새 줄 대신 갱신되고, bCancel 이면 그 Key 의 표시·대기를 지운다(열수 이탈·기뢰 폭발).
 */
USTRUCT()
struct GOHOME_API FAnnouncerEvent
{
	GENERATED_BODY()

	UPROPERTY()
	EAnnouncerEvent Type = EAnnouncerEvent::None;

	UPROPERTY()
	FName Key;

	UPROPERTY()
	bool bCancel = false;
};

/** 위젯이 그리는 현재 대사 한 줄. bVisible 이 false 면 퇴장. Serial 은 내용이 바뀔 때마다 증가. */
struct FAnnouncerDisplay
{
	bool bVisible = false;
	uint32 Serial = 0;
	FText Title;
	FText Line;
	FText Code;
	FLinearColor Signal = FLinearColor::White;
	EAnnouncerIcon Icon = EAnnouncerIcon::Signal;
};
