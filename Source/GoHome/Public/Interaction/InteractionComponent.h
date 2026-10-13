//THE

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/CoopCarryObjectBase.h"
#include "InteractionComponent.generated.h"

class UCameraComponent;
class ADeliveryPoint;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractableTargetChanged, AActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractRequested, AActor*, Target);

// 카메라 정면 트레이스로 상호작용 대상을 탐지한다(로컬 컨트롤 폰(Pawn)에서만 동작).
// 대상 변경은 OnInteractableTargetChanged로 브로드캐스트(HUD 프롬프트 표시/숨김용).

UCLASS( ClassGroup=(Interaction), meta=(BlueprintSpawnableComponent) )
class GOHOME_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:	

	UInteractionComponent();

	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractableTargetChanged OnInteractableTargetChanged;

	// 로컬에서 상호작용(E)을 서버에 요청한 순간. 서버 승인(복제) 전이라 HUD가 그 대상을 미리 걷는 데 쓴다(조준 정보 UI).
	UPROPERTY(BlueprintAssignable, Category = "Interaction")
	FOnInteractRequested OnInteractRequested;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void TryInteract();

	UFUNCTION(Server, Reliable)
	void Server_RequestInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_RequestDeliverCarry(ADeliveryPoint* DeliveryPoint);

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float TraceDistance = 300.f;


	// 선 트레이스가 빗나갔을 때 한 번 더 쓰는 구체 스윕 반지름(조준 보정). 0이면 보정 끔.
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float AimAssistRadius = 20.f;


	// 서버 측 거리 재검증용 최대 상호작용 거리.
	// TraceDistance보다 살짝 여유를 둠.
	// (카메라-폰 위치 오프셋 + 핑으로 인한 위치 오차 보정).
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float MaxInteractDistance = 500.f;

	// 운반 중 납품 지점 근접 판정 반경(바운딩 박스까지 거리). 서버 재검증 MaxInteractDistance보다 작게 둬서 핑 여유 확보.
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float CarryDeliverRadius = 300.f;

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float TraceInterval = 0.1f;

	// 조준 중 대상 강조용 Custom Depth 스텐실 값.
	UPROPERTY(EditAnywhere, Category = "Interaction")
	int32 AimOutlineStencilValue = 1;

	// 근처 루팅 아이템 힌트용 Custom Depth 스텐실 값(조준 강조와 다른 값 -> 머터리얼에서 다른 효과로 분기).
	UPROPERTY(EditAnywhere, Category = "Interaction")
	int32 NearbyHintStencilValue = 2;

	// 근처 아이템 힌트가 켜지는 반경(cm). 기본 1000 = 10m.
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float NearbyHintRadius = 1500.f;

	// 지금 조준 중인 대상 (없으면 nullptr). HUD/디버깅에서 조회용.
	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	// NewTarget이 IInteractable을 구현하면 그 프롬프트 문구를 그 외에는 기본 문구를 반환함.
	// 블루프린트에서 인터페이스 "메시지" 노드를 직접 찾기 애매할 때 쓰라고 만든 함수.
	UFUNCTION(BlueprintCallable, Category = "Interaction")
	static FText GetInteractionPromptTextFor(AActor* Target);






protected:

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	void PerformTrace();

	// 운반 중 : 반경 안 가장 가까운 납품 지점(없으면 nullptr).
	ADeliveryPoint* FindNearbyDeliveryPoint(const APawn* OwnerPawn) const;

	// 반경 안의 미획득 아이템들을 찾아 근접 힌트(스텐실)를 켜거나 끔. CurrentTarget은 제외(조준 강조가 우선).
	void UpdateNearbyItemHints();

	// Target의 모든 PrimitiveComponent에 Custom Depth 렌더링을 켜거나 끔(메쉬 테두리 강조 표시용).
	void SetOutlineEnabled(AActor* Target, bool bEnabled, int32 StencilValue);

	UPROPERTY()
	TObjectPtr<UCameraComponent> CachedCamera;

	UPROPERTY()
	TObjectPtr<AActor> CurrentTarget;

	// 지금 근접 힌트가 켜져있는 아이템들(매 체크마다 비교용).
	UPROPERTY()
	TSet<TObjectPtr<AActor>> NearbyHintedActors;

	float TimeSinceLastTrace = 0.f;
	
};
