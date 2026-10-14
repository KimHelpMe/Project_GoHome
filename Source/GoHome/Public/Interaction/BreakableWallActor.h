#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/NoiseType.h"
#include "BreakableWallActor.generated.h"

class UStaticMeshComponent;
class ANavLinkProxy;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

// 해머류 아이템으로 부술 수 있는 벽.
// 상호작용(E) 대상이 아니라 아이템 트레이스가 Cast로 찾는다.
// 권위: HitCount 하나가 "통행 가능 여부"의 유일한 진실 - HitsToBreak에 도달하면 파괴.
// 파손 단계 시각화는 스태틱 메시 교체 방식(Geometry Collection 안 씀).
UCLASS()
class GOHOME_API ABreakableWallActor : public AActor
{
	GENERATED_BODY()

public:
	ABreakableWallActor();

	bool IsBroken() const { return HitCount >= HitsToBreak; }

	// 서버 권위: 해머로 한 대 맞았을 때 호출.
	void ServerHit(AActor* InInstigator);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "BreakableWall")
	TObjectPtr<UStaticMeshComponent> IntactMesh;

	// 부서질 때 통로 통행을 여는 내비 링크. 레벨 디자이너가 통로에 배치 후 여기 연결(기본 비활성).
	UPROPERTY(EditInstanceOnly, Category = "BreakableWall")
	TObjectPtr<ANavLinkProxy> PassageNavLink;

	// 파괴까지 필요한 타격 횟수.
	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	int32 HitsToBreak = 3;

	// 단계별 교체용 메시. 배열 크기 = HitsToBreak
	// (인덱스 0 = 1타 후 ... 마지막 = 파괴/구멍 뚫린 모습).
	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	TArray<TObjectPtr<UStaticMesh>> DamageStageMeshes;

	// 일반 타격(파괴 아닌) 소음.
	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	float HitNoiseRadius = 600.f;

	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	ENoiseType HitNoiseType = ENoiseType::Medium;

	// 최종 파괴 시 소음(더 큼).
	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	float BreakNoiseRadius = 1500.f;

	UPROPERTY(EditAnywhere, Category = "BreakableWall")
	ENoiseType BreakNoiseType = ENoiseType::Large;

	// --- 코스메틱 ---
	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<UNiagaraSystem> HitEffect;

	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<USoundBase> HitSound;

	// 비워두면 사운드 에셋 자체 감쇠를 씀 - 감쇠 없는 에셋이면 맵 전체에 들리니 반드시 지정.
	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<USoundAttenuation> HitSoundAttenuation;

	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<UNiagaraSystem> BreakEffect;

	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(EditAnywhere, Category = "BreakableWall|FX")
	TObjectPtr<USoundAttenuation> BreakSoundAttenuation;

private:
	UPROPERTY(ReplicatedUsing = OnRep_HitCount)
	int32 HitCount = 0;

	UFUNCTION()
	void OnRep_HitCount();

	// bPlayEffects = false면 메시/콜리전 스왑만(늦게 접속한 클라의 상태 동기화용).
	void ApplyDamageState(bool bPlayEffects);
};