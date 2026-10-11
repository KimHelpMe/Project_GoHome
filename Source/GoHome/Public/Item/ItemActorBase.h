

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "Interaction/WeightProvider.h"
#include "ItemActorBase.generated.h"

class UItemDataAsset;
class UStaticMeshComponent;
class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * 파손형 충돌 감지, 소음 유발형 누적 타이머는 아이템 자신이 갖는다
 * (인벤토리는 무게/개수만 알면 되고 아이템별 상태 로직을 몰라도 되게 하기 위함).
 */
UCLASS()
class GOHOME_API AItemActorBase : public AActor, public IInteractable, public IWeightProvider
{
	GENERATED_BODY()

public:
	AItemActorBase();

	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<UAudioComponent> NoiseAudioComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemData, Category = "Item")
	TObjectPtr<UItemDataAsset> ItemData;

	virtual bool CanInteract(APawn* InstigatorPawn) const override;
	virtual void OnInteract(APawn* InstigatorPawn) override;
	virtual float GetTotalWeight() const override;

	// 서버 전용: 서브클래스 OnInteract 재정의(예: 회복 아이템의 즉시 사용)를 거치지 않고 기본 픽업으로 Pawn 핫바에 넣는다.
	// 공유 보관함 꺼내기가 사용한다. 들어갔으면 true.
	bool ServerGrantToPawn(APawn* TargetPawn);

	// 잠수정 공유 보관함에서 꺼낸 상품인지(서버 전용 표시). 납품 정산 대상에서 빠진다.
	void SetSharedLockerItem(bool bInSharedLockerItem) { bSharedLockerItem = bInSharedLockerItem; }

	bool IsSharedLockerItem() const { return bSharedLockerItem; }

	// 파손 반영된 현재 가치. 납품 정산 시 ItemData->Value대신 이 값을 사용해야 함.
	UFUNCTION(BlueprintCallable, Category = "Item")
	float GetCurrentValue() const;

	// 납품 정산 대상인지 여부. 기본은 true(일반 아이템)
	// 손전등 처럼 슬롯은 차지하지만 정산되면서 파괴되면 안되는 장비류는 false로 오버라이드.
	UFUNCTION(BlueprintPure, Category = "Item")
	virtual bool IsDeliverable() const { return true; }

	// 부력 계산 전용 무게. 기본은 ItemData->Weight(인벤토리/운반 무게) 그대로 사용.
	// 손전등처럼 인벤토리 무게는 0(운반 부담 없음)이어도 실제로는 가라앉아야 하는 장비류는
	// 오버라이드해서 물리적 무게를 따로 지정함.
	UFUNCTION(BlueprintPure, Category = "Item")
	virtual float GetBuoyancyWeight() const;

	// 인벤토리에 담길 때 Interaction이 호출한다(소음 유발형이면 반경 증가 타이머 시작).
	void NotifyPickedUp();

	// 인벤토리에서 빠질 때 Interaction이 호출한다 (타이머 정지 + 반경 리셋).
	void NotifyDropped();

	// 드롭 시 앞으로 던져지는 초기 속도. 값 낮추면 툭 떨어지고, 높이면 멀리 날아간다.
	UPROPERTY(EditAnywhere, Category = "Item")
	float DropThrowSpeed = 500.f;

	// 서버 권위 : 부착 해제 + 물리/콜리전 복원 + 인벤토리에서 제거.
	// InventoryComponent::Server_RequestDrop이 소유권 검증 후 호출한다. 서버에서만 호출할 것.
	virtual void ServerDrop();

	// 보유 중 특수 동작( 예 : 손전등 온/오프).
	// 기본은 아무 것도 안함 -> 필요한 아이템만 오버라이드.
	// InventoryComponent가 슬롯 내용물 타입을 몰라도 호출할 수 있게 하기 위한 확장 지점.
	virtual void ServerUseSpecialAction() {}

	// 지금 손에 나와있는(활성 슬롯) 아이템인지 여부. false면 인벤토리에 있지만 숨겨진 상태.
	UPROPERTY(ReplicatedUsing = OnRep_IsActiveHeld)
	bool bIsActiveHeld = false;

	// 근접 힌트 제외용 조회. 한 번이라도 집힌 적 있으면 true(드롭해도 안 풀림) - 이미 발견된 아이템이라 힌트 불필요.
	UFUNCTION(BlueprintPure, Category = "Item")
	bool HasBeenPickedUp() const { return bHasBeenPickedUp; }

	UFUNCTION()
	void OnRep_IsActiveHeld();

	// 서버 전용: InventoryComponent가 활성 슬롯 전환 시 호출한다.
	void SetActiveHeld(bool bNewActive);

	// 원거리 회수 등 외부 시스템이 "확보됨" 상태를 걸고 풀 때 사용. 서버 전용.
	UFUNCTION(BlueprintCallable, Category = "Item")
	void SetBeingClaimed(bool bNewClaimed);

	// 원거리 회수 등 외부 시스템이 위치를 직접 제어하는 동안 물리를 꺼두고,
	// 끝나면 "막 드롭된" 상태로 복귀 시킨다. 서버 전용.
	UFUNCTION(BlueprintCallable, Category = "Item")
	void SetExternallyPositioned(bool bExternallyPositioned);

	// BreakCount 변화(NotifyHit 직후, 클라이언트 리플리케이션 수신, 또는 조준/근접힌트 강조가 꺼져서
	// 원래 상태로 복원해야 할 때)에 맞춰 파손 표시를 갱신.
	// InteractionComponent도 호출함.
	void UpdateDamageVisual();


	// 서버 전용. 주어인 충격 속도가 파손 임계값을 넘으면 BreakCount를 올리고 시각 갱신.
	// 성공 시 true.
	// NotifyHit(드롭 상태 물리 충돌)과 AGoHomeCharacter의 인벤토리 파손 판정이 이 함수를 공유한다.
	bool TryApplyBreakFromImpact(float ImpactSpeed);

	virtual FText GetInteractionPromptText_Implementation() const override;

protected:

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaTime) override;

	UPROPERTY(ReplicatedUsing = OnRep_HoldingPawn)
	TObjectPtr<APawn> HoldingPawn = nullptr;

	virtual void OnRep_ReplicatedMovement() override;

	UFUNCTION()
	void OnRep_HoldingPawn(APawn* OldHoldingPawn);

	virtual void UpdateAttachment(APawn* OldHoldingPawn = nullptr);

	// 드롭 등으로 물리가 다시 켜질 때 호출 : 부유 사이클 시작(Tick 켜기 + FloatDuration 후 가라앉기 떠오르기 시작 타이머).
	// SetSimulatePhysics(true) 직후에 호출할 것.
	// 자식 클래스(예: 손전등)가 자기만의 UpdateAttachment를 쓰더라도 이 사이클을 재사용할 수 있게 protected로 노출.
	void BeginFloatCycle();

	// 다시 손에 들렸을 때 호출 : 부유 사이클 정지(타이머 취소 + Tick 끄기).
	void CancelFloatCycle();

	// 가라앉다가 바닥 등에 부딪혀 완전히 정지하는 순간 호출됨(뜨는 아이템에는 안 불림).
	// 기본은 아무 것도 안 함 -> 필요한 서브클래스만 오버라이드.
	virtual void OnSettled() {}

	UFUNCTION()
	virtual void OnRep_ItemData();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void NotifyHit(
		UPrimitiveComponent* MyComp,
		AActor* Other,
		UPrimitiveComponent* OtherComp,
		bool bSelfMoved,
		FVector HitLocation,
		FVector HitNormal,
		FVector NormalImpulse,
		const FHitResult& Hit) override;

	// 동시 픽업 레이스 컨디션 방지: 서버 틱 단일 스레드 특성 이용 (동기 함수 호출 안에서 끊김 없이 검사+설정).
	UPROPERTY(Replicated)
	bool bIsBeingClaimed = false;

	// 근접 힌트 제외 플래그. OnInteract에서 최초 픽업 시 true로 세팅되고 이후 계속 유지됨(드롭해도 안 풀림).
	UPROPERTY(Replicated)
	bool bHasBeenPickedUp = false;

	// 파손형 누적 파손 횟수. ItemData->MaxBreakCount에서 멈춘다.
	UPROPERTY(ReplicatedUsing = OnRep_BreakCount)
	int32 BreakCount = 0;

	// 소음 유발형 현재 반경(미보유 시 0).
	UPROPERTY(ReplicatedUsing = OnRep_CurrentNoiseRadius)
	float CurrentNoiseRadius = 0.f;

private:

	void GrowNoiseRadius();

	// 우측 알림 피드(본인만) — 납품 정산 대상(장비·보관함 상품 제외)을 들고 있을 때만 보낸다. 서버 전용.
	bool IsNoticeTarget() const { return IsDeliverable() && !IsSharedLockerItem(); }
	void SendHolderNotice(const struct FHudNotice& Notice) const;

	// 소음 단계: 1 = 기본, 2 = Large(LargeNoiseRadius 이상), 마지막 = MaxNoiseRadius 도달. 단계 수는 반경 설정에 따라 2~3
	int32 GetNoiseStage(float Radius) const;
	int32 GetNoiseStageCount() const;

	UFUNCTION()
	void OnRep_CurrentNoiseRadius();

	// 소음 오디오 재생/정지 + 반경 비율에 따른 볼륨·피치 갱신.
	// HoldingPawn이 바뀌거나(UpdateAttachment 경유) CurrentNoiseRadius가 바뀔 때(OnRep/서버 직접 호출) 부른다.
	void UpdateNoiseAudio();

	// Item Data 헬퍼 함수.
	void SyncVisualsFromItemData();

	// 스폰시 바닥 위치.
	void SnapToGround();

	void BeginSinkOrRise();
	FTimerHandle SinkOrRiseTimerHandle;

	// 드롭 후 이 시간(초) 동안은 부유 상태 유지, 이후 무게 기반으로 가라앉거나 떠오름.
	UPROPERTY(EditAnywhere, Category = "Item")
	float FloatDuration = 4.0f;

	// 이 무게보다 무거우면 가라앉고, 가벼우면 떠오름.
	UPROPERTY(EditAnywhere, Category = "Item")
	float NeutralWeight = 1.5f;

	// 가라앉거나 떠오를 경우 수치 조정.
	UPROPERTY(EditAnywhere, Category = "Item")
	float BuoyancyAccelFactor = 180.0f;

	// 부유 단계에서의 흔들림.
	UPROPERTY(EditAnywhere, Category = "Item")
	float DriftForceStrength = 60.0f;

	float DriftPhaseOffset = 0.f;
	bool bIsSinkingOrRising = false;

	// 가라앉는 도중 한 번이라도 SettleVelocityThreshold를 넘긴 적 있는지.
	// (막 가라앉기 시작해서 아직 속도가 안 붙은 상태를 "바닥에 닿아 멈춤"으로 오판하지 않기 위함.)
	bool bHasReachedSinkSpeed = false;

	UPROPERTY(EditAnywhere, Category = "Item")
	float SettleVelocityThreshold = 5.0f;

	FTimerHandle NoiseGrowthTimerHandle;

	// 파손형 관련----------
	UFUNCTION()
	void OnRep_BreakCount();

	// 파손 시각효과(균열)용 공유 오버레이 머티리얼 - 전체 파손형 아이템이 공통으로 씀, 아이템별 설정 불필요.
	UPROPERTY(EditDefaultsOnly, Category = "Item")
	TObjectPtr<UMaterialInterface> CrackOverlayMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CrackOverlayMID;
	//-----------

	// 파손 시각효과용 스텐실 값. 조준(1)/근접힌트(2)와 겹치지 않는 값.
	UPROPERTY(EditAnywhere, Category = "Item")
	int32 DamageStencilValue = 3;

	bool bSharedLockerItem = false;

};