#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/SpectateBackend.h"
#include "Data/FSettlementResult.h"
#include "SpectateComponent.generated.h"

class ACameraActor;
class APawn;
class APlayerController;
class APlayerState;
class AExplorationGameState;
class UEnhancedInputComponent;
class UInputAction;
class UUserWidget;

/**
 * 사망 → 관전(Reference Pack: Docs/Dev/UI/spectate, A안 DIVER FEED). AGoHomePlayerController가 생성자에서 붙인다.
 * 로컬 컨트롤러에서만 돈다: 자기 폰 사망을 보고 사망 연출(시체 시점) → 생존 팀원 3인칭 오빗 관전으로 넘어가고,
 * 관전 카메라(로컬 전용 ACameraActor)·입력(대상 전환·궤도·말하기)·관전 화면 위젯의 수명을 소유한다.
 * 서버는 관전 대상을 몰라도 되지만, 대상 폰이 관련성 거리 밖이면 클라에 안 오므로 Server_SetSpectateTarget으로
 * 서버 쪽 뷰 타깃만 맞춰 둔다(그 폰은 항상 관련성 있음 — 엔진 규칙). 생존 여부는 AExplorationGameState::IsPlayerDead.
 */
UCLASS(ClassGroup = (Core), meta = (BlueprintSpawnableComponent))
class GOHOME_API USpectateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpectateComponent();

	const FSpectateView& GetView() const { return View; }

	// 폰이 바뀌었거나(로비 재생성) 월드가 바뀌면 관전을 끝내고 처음 상태로
	void ResetSpectate();

	// 다음(+1)/이전(-1) 생존자. 관전 입력이 부르고, 비Shipping 콘솔 GoHome.Spectate.Step 도 부른다.
	void Step(int32 Direction);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 관전 화면. 기본값은 C++ USpectateScreenWidget(트리를 C++이 만든다).
	UPROPERTY(EditDefaultsOnly, Category = "Spectate")
	TSoftClassPtr<UUserWidget> ScreenClass = TSoftClassPtr<UUserWidget>(FSoftClassPath(TEXT("/Script/GoHome.SpectateScreenWidget")));

	// 탐사 HUD(WBP_HUD, 100)·시스템 메뉴(100)보다 아래 — 메뉴는 관전 위에 떠야 한다
	UPROPERTY(EditDefaultsOnly, Category = "Spectate")
	int32 ScreenZOrder = 99;

	// 관전하는 동안 숨길 개인 HUD(뷰포트 최상위 위젯 클래스). 캐릭터·컨트롤러 BP가 따로 만들어 붙이는 것들이라
	// 각 BP 그래프를 고치는 대신 여기서 한 번에 숨기고 관전이 끝나면 원래 가시성으로 되돌린다.
	// 진행도 HUD·보이스 목록·나침반은 팀 공유 정보라 목록에 없다(Pack: 목표·보이스 유지).
	UPROPERTY(EditDefaultsOnly, Category = "Spectate")
	TArray<TSoftClassPtr<UUserWidget>> HiddenHUDClasses;

	// 관전 중에도 말하기(사망자끼리). 폰 입력은 사망 때 꺼지므로 같은 액션을 관전 입력에 다시 묶는다.
	UPROPERTY(EditDefaultsOnly, Category = "Spectate")
	TSoftObjectPtr<UInputAction> TalkAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/GoHome/Input/DefaultActions/IA_Talk.IA_Talk")));

	// --- 시간 (motion.md A안) ---
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Timing")
	float DyingSeconds = 2.6f;

	// 연결 노이즈가 끝나야 첫 대상으로 컷
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Timing")
	float ConnectNoiseSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Timing")
	float TargetLostHoldSeconds = 0.9f;

	// 시체 시점으로 가라앉는 시간
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float CorpseSettleSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float CorpseRoll = -9.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float CorpseDrop = 25.f;

	// --- 오빗 ---
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float OrbitDistance = 320.f;

	// 대상 액터 원점(캡슐 중심) 위로
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float PivotHeight = 50.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float DefaultPitch = -12.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float MinPitch = -70.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float MaxPitch = 40.f;

	// 마우스 델타(GetInputMouseDelta) → 도. 캐릭터 둘러보기(AddControllerYawInput × 기본 InputYawScale 2.5)와 같은 체감
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float MouseDegreesPerUnit = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float GamepadDegreesPerSecond = 150.f;

	// 시뮬레이티드 프록시 보간 떨림 흡수
	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float PivotInterpSpeed = 14.f;

	UPROPERTY(EditDefaultsOnly, Category = "Spectate|Camera")
	float ProbeRadius = 12.f;

private:
	// 서버 쪽 뷰 타깃만 대상 폰으로 — 그 폰이 이 클라에 관련성 있게 된다. 클라 화면은 로컬 카메라가 정한다.
	UFUNCTION(Server, Reliable)
	void Server_SetSpectateTarget(APlayerState* NewTarget);

	APlayerController* GetOwnerController() const;
	AExplorationGameState* GetExplorationGameState() const;
	bool IsOwnPawnDead() const;
	bool IsRoundResolved() const;
	bool IsPlayerAlive(const APlayerState* PlayerState) const;
	APawn* FindPawn(const APlayerState* PlayerState) const;

	void SetPhase(ESpectatePhase NewPhase);
	void BeginDying();
	void BeginConnecting();
	void Cut(int32 Direction);	// 현재 대상으로 카메라 즉시 이동 + 컷 연출 트리거
	APlayerState* PickNextAlive(const APlayerState* From, int32 Direction) const;
	void SetTarget(APlayerState* NewTarget);

	void SpawnCamera();
	void UpdateCorpseCamera();
	void UpdateOrbitCamera(float DeltaTime, bool bSnap);
	void BindInput();
	void UnbindInput();
	void ShowScreen();
	void HideScreen();
	void HidePersonalHUD();
	void RestorePersonalHUD();
	void RefreshView();

	void HandleNext() { Step(1); }
	void HandlePrev() { Step(-1); }
	void HandleTalkStarted();
	void HandleTalkCompleted();

	UFUNCTION()
	void HandleSettlementReady(const FSettlementResult& Result);

	FSpectateView View;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> SpectateInput;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Screen;

	// 숨긴 위젯과 원래 가시성
	TArray<TPair<TWeakObjectPtr<UUserWidget>, ESlateVisibility>> HiddenHUD;

	TWeakObjectPtr<APlayerState> Target;
	TWeakObjectPtr<APawn> WatchedPawn;	// 사망을 지켜보는 내 폰(바뀌면 리셋)
	TWeakObjectPtr<AExplorationGameState> BoundGameState;

	FVector CorpseStartLocation = FVector::ZeroVector;
	FRotator CorpseStartRotation = FRotator::ZeroRotator;

	FVector Pivot = FVector::ZeroVector;
	FRotator Orbit = FRotator::ZeroRotator;

	bool bSettled = false;	// 정산/실패 화면이 떴다 — 관전 화면은 다시 띄우지 않는다
};
