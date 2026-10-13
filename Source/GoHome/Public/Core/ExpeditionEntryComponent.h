#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/ExpeditionEntryBackend.h"
#include "Core/ExpeditionState.h"
#include "Components/SlateWrapperTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExpeditionEntryComponent.generated.h"

class AExplorationGameState;
class UInputComponent;
class UUserWidget;

/**
 * 탐사 진입 조정자(PlayerController 수명, AGoHomePlayerController 생성자가 부착).
 * 서버: 준비 보고를 GameMode에 넘기고, 탐사 개시 전까지 폰 이동을 잠근다.
 * 로컬: 오프닝 위젯(UExpeditionOpeningWidget — 이 컴포넌트는 소프트 클래스로만 안다) 생성·제거, 스킵 입력, 입력 잠금,
 * BP가 따로 만드는 개인 HUD 숨김. PC가 만드는 HUD는 View.bHUDVisible이 켜질 때까지 생성 자체를 미룬다(AGoHomePlayerController).
 */
UCLASS(ClassGroup = (Core))
class GOHOME_API UExpeditionEntryComponent : public UActorComponent, public IExpeditionEntryBackend
{
	GENERATED_BODY()
public:
	UExpeditionEntryComponent();

	// 월드(GameState)·폰이 바뀔 때 PC가 부른다
	void RefreshContext();

	void RegisterHUD(UUserWidget* Widget);
	virtual const FExpeditionEntryView& GetEntryView() const override { return View; }
	virtual FOnExpeditionEntryViewChanged& OnEntryViewChanged() override { return ViewChanged; }
	virtual double GetEntryServerTime() const override;
	UFUNCTION(BlueprintCallable, Category = "Expedition|Entry")
	virtual void FinishOpening(UObject* Context) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UFUNCTION(Server, Reliable)
	void Server_ReportEntryReady(AExplorationGameState* Context);
	UFUNCTION()
	void HandleEntryChanged();
	UFUNCTION()
	void HandleStateChanged(EExpeditionState State);

	// BP(캐릭터·컨트롤러)가 따로 만드는 개인 HUD — 오프닝이 끝날 때까지 숨긴다. 오프닝/시스템 메뉴는 넣지 않는다.
	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry")
	TArray<TSoftClassPtr<UUserWidget>> LegacyHUDClasses;

	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry")
	TSoftClassPtr<UUserWidget> OpeningClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.ExpeditionOpeningWidget")));

	// 관전(99)·시스템 메뉴(100) 아래, HUD(0~3) 위
	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry")
	int32 OpeningZOrder = 90;

	// 탐사 시작 뒤 도착 안내를 유지·퇴장하는 시간(UExpeditionOpeningWidget 타임라인과 같아야 한다)
	UPROPERTY(EditDefaultsOnly, Category = "Expedition|Entry")
	float OpeningTailSeconds = 1.3f;

private:
	void ResetContext();
	void UpdateView();
	bool IsOpeningExpired() const;
	void UpdateOpeningWidget();
	void ApplyLocalGate();
	void DiscoverLegacyHUD();
	void RestoreHUD();
	void ApplyMovementGate();
	void RestoreMovement();
	void BindSkipInput();
	void UnbindSkipInput();
	void HandleSkip();

	TWeakObjectPtr<AExplorationGameState> BoundState;
	TWeakObjectPtr<APawn> ReadyPawn;
	TWeakObjectPtr<UCharacterMovementComponent> LockedMovement;
	EMovementMode SavedMovementMode = MOVE_None;
	uint8 SavedCustomMode = 0;
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> HiddenHUD;
	TSet<TWeakObjectPtr<UUserWidget>> RegisteredHUD;
	bool bLocalInputLocked = false;
	bool bOpeningFinished = false;
	FExpeditionEntryView View;
	FOnExpeditionEntryViewChanged ViewChanged;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> OpeningWidget;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> SkipInput;
};
