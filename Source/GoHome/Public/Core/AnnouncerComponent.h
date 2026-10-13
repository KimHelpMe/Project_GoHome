#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/AnnouncerBackend.h"
#include "Core/AnnouncerCatalog.h"
#include "AnnouncerComponent.generated.h"

class AGameStateBase;
class APawn;
class UHealthComponent;
class UOxygenComponent;

/**
 * 아나운서(부이) 흐름 소유자. AGoHomePlayerController 가 생성자에서 붙인다.
 *
 * 서버 인스턴스: 개인 사건(유물·피해·열수·기뢰)을 받아 발화 간격을 거른 뒤 Client_PushAnnouncerEvent 로 그 플레이어에게만 보낸다.
 *   게임 코드(Item/Player)는 SendToPawnOwner / CancelForPawnOwner 만 부른다(UHudNoticeSubsystem::SendToPawnOwner 와 같은 경로).
 *   담당 폰의 피해는 UHealthComponent::OnDamageTaken 을 직접 구독한다.
 * 로컬 인스턴스: 팀 진행 사건(시작·귀환·제한시간)과 본인 산소 단계를 복제 상태로 판정하고, 큐·우선순위·유지시간을 돌려
 *   IAnnouncerBackend 로 위젯(UAnnouncerWidget)에 한 줄씩 넘긴다. 정산/실패 진입·월드 전환에서 전부 비운다.
 */
UCLASS(ClassGroup = (Core))
class GOHOME_API UAnnouncerComponent : public UActorComponent, public IAnnouncerBackend
{
	GENERATED_BODY()

public:
	UAnnouncerComponent();

	// 서버 전용: Pawn 을 조종하는 플레이어 화면에만 띄운다. 컨트롤러가 없거나(AI) GoHome 컨트롤러가 아니면 무시.
	static void SendToPawnOwner(const APawn* Pawn, EAnnouncerEvent Event, FName Key = NAME_None);

	// 서버 전용: 그 Key 의 표시·대기를 지운다(열수 영역 이탈·분출 종료, 기뢰 폭발 — 뒤늦은 경고 방지)
	static void CancelForPawnOwner(const APawn* Pawn, FName Key);

	// 로컬에서만 일어나는 사건(문어 먹물처럼 BP 가 로컬 조종 폰에서만 처리하는 연출)을 그 플레이어 화면에 띄운다.
	// 로컬 조종 폰이 아니면 무시. 같은 사건의 반복은 카탈로그 간격(ServerCooldownSeconds)으로 거른다.
	UFUNCTION(BlueprintCallable, Category = "Announcer", meta = (DisplayName = "Announce To Local Player"))
	static void AnnounceToLocalPlayer(APawn* Pawn, EAnnouncerEvent Event);

	// 로컬: 큐에 직접 넣는다(개발용 명령)
	void PushLocal(const FAnnouncerEvent& Event);

	// 로컬: 대기열을 비우고 표시 중인 대사를 퇴장시킨다
	void ClearAll();

	// 로컬: 이전 월드의 대사·판정 기록을 모두 버린다(seamless travel 은 컨트롤러가 살아남는다)
	void ResetForNewWorld();

	// IAnnouncerBackend
	virtual const FAnnouncerDisplay& GetAnnouncerDisplay() const override { return Display; }
	virtual FOnAnnouncerDisplayChanged& OnAnnouncerDisplayChanged() override { return DisplayChanged; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Client, Reliable)
	void Client_PushAnnouncerEvent(const FAnnouncerEvent& Event);

	// 기본 유지 시간(시안 3.5s). 긴 문구는 글자 수만큼 늘린다(MaxHoldSeconds 까지)
	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	float BaseHoldSeconds = 3.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	float MaxHoldSeconds = 6.f;

	// 일반 안내 사이 간격. 우선순위 80 이상은 퇴장(0.18s)만 기다린다
	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	float GapSeconds = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	int32 MaxQueue = 3;

	// HUD 가 뜬 뒤 시작 안내까지 대기(로딩 화면이 걷히는 시간)
	UPROPERTY(EditDefaultsOnly, Category = "Announcer")
	float StartDelaySeconds = 1.5f;

	// 산소 현재/최대 비율 경고 구간. 더 위험한 구간에 들어갈 때만 알리고, RearmRatio 위로 회복하면 다시 무장한다
	UPROPERTY(EditDefaultsOnly, Category = "Announcer|Oxygen")
	float OxygenLowRatio = .25f;

	UPROPERTY(EditDefaultsOnly, Category = "Announcer|Oxygen")
	float OxygenCriticalRatio = .10f;

	UPROPERTY(EditDefaultsOnly, Category = "Announcer|Oxygen")
	float OxygenRearmRatio = .30f;

private:
	struct FQueued
	{
		FAnnouncerEvent Event;
		int32 Priority = 0;
		double ExpireTime = 0.0;
	};

	// --- 서버 ---
	void ServerSend(const FAnnouncerEvent& Event);
	void ServerRebindPawn();
	void HandleDamageTaken(float Amount, AActor* DamageInstigator, FName DamageType, bool bLethal);
	void HandleDrainBoostStarted(AActor* InstigatorActor);
	bool IsMonsterInstigator(const AActor* InstigatorActor) const;
	void ServerResetIfNewWorld();

	TWeakObjectPtr<AGameStateBase> ServerContext;
	TMap<uint8, double> LastSentTime;
	bool bFirstArtifactSent = false;
	TWeakObjectPtr<UHealthComponent> ServerHealth;
	FDelegateHandle DamageHandle;
	TWeakObjectPtr<UOxygenComponent> ServerOxygen;
	FDelegateHandle DrainHandle;

	// --- 로컬 ---
	bool IsLocal() const;
	void LocalTick();
	void Enqueue(const FAnnouncerEvent& Event);
	void ShowNow(const FQueued& Item);
	void EndCurrent();
	void DropScope(EAnnouncerScope Scope);
	void RefreshCurrentText();
	double Now() const;

	void RebindLocalPawn();
	UFUNCTION()
	void HandleOxygenChanged(float CurrentOxygen, float MaxOxygen);
	int32 OxygenBand(float Ratio) const;

	TWeakObjectPtr<AGameStateBase> LocalContext;
	TMap<uint8, double> LocalLastTime;
	double ContextStartTime = 0.0;
	bool bRoundOver = false;
	bool bStartQueued = false;
	double StartAt = -1.0;
	bool bSeenDoorOpen = false;
	bool bReturnInProgress = false;
	bool bTimeArmed = false;
	bool TimeArmed[3] = { false, false, false };
	bool bLocalDead = false;

	TWeakObjectPtr<APawn> LocalPawn;
	TWeakObjectPtr<UOxygenComponent> BoundOxygen;
	int32 LastOxygenBand = 0;

	TOptional<FQueued> Current;
	double CurrentEnd = 0.0;
	double NextShowAt = 0.0;
	double LastEndTime = -100.0;
	TArray<FQueued> Queue;

	FAnnouncerDisplay Display;
	FOnAnnouncerDisplayChanged DisplayChanged;
};
