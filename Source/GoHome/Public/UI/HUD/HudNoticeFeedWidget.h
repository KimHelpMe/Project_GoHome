#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HudNoticeTypes.h"
#include "HudNoticeFeedWidget.generated.h"

class UPanelWidget;
class UHudNoticeRowWidget;

/**
 * 화면 우측 알림 피드(Reference Pack: Docs/Dev/UI/ingame-hud 알림 피드, 행 디자인 B안).
 * UHudNoticeSubsystem::OnNoticePushed 만 구독한다 — 본인 알림만 온다(서버가 소유 플레이어에게만 보냄).
 * 아래로 쌓이고(최신이 맨 아래) 살아 있는 줄이 MaxRows 를 넘으면 가장 오래된 줄을 빠르게 내보낸다.
 * 같은 Key 는 기존 줄을 갱신하고 맨 아래로 옮긴다. 폰이 없거나 사망(UHealthComponent::IsDead, 복제)이면 비우고 숨긴다.
 * 생성은 AGoHomePlayerController::RefreshProgressHUD(HudNoticeFeedClass). 배치는 BP(WBP_HudNoticeFeed).
 */
UCLASS(Abstract)
class GOHOME_API UHudNoticeFeedWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 디버그·테스트용 직접 추가(평소엔 서브시스템 경유)
	UFUNCTION(BlueprintCallable, Category = "HUD Notice")
	void PushNotice(const FHudNotice& Notice);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 줄을 쌓는 VerticalBox
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UPanelWidget> NoticeBox;

	UPROPERTY(EditAnywhere, Category = "HUD Notice")
	TSubclassOf<UHudNoticeRowWidget> RowClass;

	// 줄 간격은 행 WBP 안쪽 위 여백 — 접힘 모션에 같이 줄어들어야 해서 컨테이너 슬롯 패딩으로 두지 않는다
	UPROPERTY(EditAnywhere, Category = "HUD Notice", meta = (ClampMin = "1"))
	int32 MaxRows = 4;

private:
	void HandleNoticePushed(const FHudNotice& Notice);
	void ClearRows();
	bool HasLivePawn() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHudNoticeRowWidget>> Rows;

	FDelegateHandle NoticeHandle;
	bool bHadPawn = true;
};
