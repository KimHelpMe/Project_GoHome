#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ZoneSelectBackend.generated.h"

class UExpeditionZoneDataAsset;

// 팀 목적지가 바뀐 이유. 성공 문구는 복제 결과(Mine)로만 낸다 — 요청을 보냈다고 낙관적으로 출력하지 않는다.
enum class EZoneDestinationChange : uint8
{
	Mine,	// 내 요청이 서버에 반영됨
	Other,	// 다른 팀원이 바꿈
	Failed	// 내 요청이 제한 시간 안에 반영되지 않음(ZoneId는 현재 값)
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FZoneDestinationChangedEvent, FName /*ZoneId*/, EZoneDestinationChange /*Change*/);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UZoneSelectBackend : public UInterface
{
	GENERATED_BODY()
};

/**
 * 탐사 목적지 화면(UI/ZoneSelect)이 아는 요청 계약. 구현체(AGoHomePlayerController)가 위젯 생성·입력 모드·제거와
 * 서버 요청을 소유한다. 화면은 로컬 미리보기만 직접 바꾸고, 팀 목적지는 요청 후 복제 결과로만 갱신한다.
 */
class GOHOME_API IZoneSelectBackend
{
	GENERATED_BODY()

public:
	// 로비 GameState의 지정 가능한 탐사지(순서 = 카드 순서). 로비가 아니면 빈 배열.
	virtual TArray<const UExpeditionZoneDataAsset*> GetSelectableZones() const = 0;

	// 서버가 복제한 현재 팀 목적지(미지정 NAME_None)
	virtual FName GetTeamDestination() const = 0;

	// 팀 목적지 지정 요청. 진행 중인 요청이 있거나 이미 그 값이면 무시하고 false.
	virtual bool RequestTeamDestination(FName ZoneId) = 0;
	virtual bool IsDestinationRequestPending() const = 0;
	virtual FZoneDestinationChangedEvent& OnTeamDestinationChanged() = 0;

	// 재진입 시 마지막 미리보기 카드로 포커스를 복원한다(화면보다 오래 사는 구현체가 기억).
	virtual FName GetLastPreviewedZone() const = 0;
	virtual void SetLastPreviewedZone(FName ZoneId) = 0;

	// 위젯 제거와 게임 입력·커서 복원(퇴장 연출은 화면이 끝낸 뒤 부른다)
	virtual void RequestCloseZoneSelect() = 0;
};
