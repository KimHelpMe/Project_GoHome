#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Core/HudNoticeTypes.h"
#include "HudNoticeSubsystem.generated.h"

class APawn;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnHudNoticePushed, const FHudNotice& /*Notice*/);

/**
 * 우측 알림 피드의 로컬 진입점. 피드 위젯(UHudNoticeFeedWidget)이 OnNoticePushed 만 구독한다.
 * LocalPlayer 수명이라 seamless travel 로 HUD 가 다시 만들어져도 구독 대상이 그대로다.
 * 게임 코드(Item/Interaction)는 서버에서 SendToPawnOwner 만 부른다 — UI 를 include 하지 않는다.
 */
UCLASS()
class GOHOME_API UHudNoticeSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	void Push(const FHudNotice& Notice);

	FOnHudNoticePushed OnNoticePushed;

	// 서버 전용: Pawn 을 조종하는 플레이어 화면에만 띄운다(본인 알림). 리슨 호스트 본인이면 Client RPC 가 로컬에서 바로 실행된다.
	// 컨트롤러가 없거나(AI·사망 후 관전) GoHome 컨트롤러가 아니면 무시.
	static void SendToPawnOwner(const APawn* Pawn, const FHudNotice& Notice);
};
