#pragma once

#include "CoreMinimal.h"
#include "Core/AnnouncerTypes.h"

/** 사건 하나의 대사·신호색·정책(우선순위·유효시간·서버 발화 간격). */
struct FAnnouncerLineDef
{
	FText Title;
	FText Line;
	FText Code;
	FLinearColor Signal = FLinearColor::White;
	EAnnouncerIcon Icon = EAnnouncerIcon::Signal;

	// 100 임박한 치명 위험 · 80 위협 · 60 임무 안내 · 20 획득 반응. 높은 쪽이 표시 중인 낮은 쪽을 즉시 끊는다
	int32 Priority = 60;
	EAnnouncerScope Scope = EAnnouncerScope::Personal;

	// 받은 뒤 이 시간 안에 못 띄우면 버린다(지나간 경고를 늦게 재생하지 않게)
	float TtlSeconds = 6.f;

	// 서버가 같은 플레이어에게 같은 사건을 다시 보내기까지 최소 간격(0 = 제한 없음)
	float ServerCooldownSeconds = 0.f;
};

/**
 * 사건 → 대사 기본 카탈로그(C++ 정의, 문구는 LOCTEXT). 대사 분량이 늘면 이 인터페이스를 유지한 채 데이터 출처만 바꾼다.
 * 시안: Docs/Dev/UI/ingame-hud/vitals-announcer (개인 작업 폴더) — 신호색 청록·황금·적색·진홍.
 */
namespace AnnouncerCatalog
{
	// bReturnInProgress: 귀환 절차(도킹 문 닫힘) 중이면 제한시간 경고를 "돌아와" 대신 "진행 중" 문구로 바꾼다
	GOHOME_API const FAnnouncerLineDef& Get(EAnnouncerEvent Event, bool bReturnInProgress = false);

	// 개발용 명령 인자("Time10" 등) → 사건. 모르면 None
	GOHOME_API EAnnouncerEvent FromName(const FString& Name);
}
