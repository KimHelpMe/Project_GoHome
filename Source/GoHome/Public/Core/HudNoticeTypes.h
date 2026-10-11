#pragma once

#include "CoreMinimal.h"
#include "HudNoticeTypes.generated.h"

class UTexture2D;

// 알림 행의 액센트 색을 고른다(Reference Pack: Docs/Dev/UI/ingame-hud 알림 피드)
UENUM(BlueprintType)
enum class EHudNoticeSeverity : uint8
{
	Info,
	Good,
	Warning,
	Danger,
};

/**
 * 우측 알림 피드 한 줄. 서버가 문구까지 만들어 소유 클라이언트로 보낸다(AGoHomePlayerController::Client_PushHudNotice).
 * Key 가 같은 알림은 새 줄 대신 기존 줄을 갱신한다(아이템별 소음 단계 등). 비어 있으면 매번 새 줄.
 */
USTRUCT(BlueprintType)
struct GOHOME_API FHudNotice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	FName Key;

	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	EHudNoticeSeverity Severity = EHudNoticeSeverity::Info;

	// 비어 있으면 액센트 테두리의 빈 원
	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	TObjectPtr<UTexture2D> Icon = nullptr;

	// 아이콘 우하단 작은 원 안의 한 글자(+, !, ≈, ✓)
	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	FText Badge;

	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	FText Title;

	UPROPERTY(BlueprintReadWrite, Category = "HUD Notice")
	FText Subtitle;
};
