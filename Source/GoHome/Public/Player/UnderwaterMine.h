

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UnderwaterMine.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class ACharacter;

UENUM(BlueprintType)
enum class EMineState : uint8
{
    Idle,
    Warning,
    Detonated
};

UCLASS()
class GOHOME_API AUnderwaterMine : public AActor
{
    GENERATED_BODY()

public:
    AUnderwaterMine();

protected:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

    void ArmMine();
    void Detonate();

    UFUNCTION()
    void OnRep_State();

    void HandleStateChanged();

    UPROPERTY(VisibleAnywhere, Category = "Mine")
    TObjectPtr<USphereComponent> TriggerArea;

    UPROPERTY(VisibleAnywhere, Category = "Mine")
    TObjectPtr<UStaticMeshComponent> MineMesh;

    UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Mine")
    EMineState State = EMineState::Idle;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine", meta = (ClampMin = "0.0"))
    float WarningDuration = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine", meta = (ClampMin = "0.0"))
    float ExplosionRadius = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine", meta = (ClampMin = "0.0"))
    float ExplosionDamage = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine")
    FName DamageTypeName = FName(TEXT("UnderwaterMine"));

    UPROPERTY(EditAnywhere, Category = "Mine")
    TObjectPtr<UNiagaraSystem> ExplosionVFXAsset;

    bool bHasTriggered = false;

    FTimerHandle DetonationTimerHandle;

    // Players told about this mine when it armed (server) - cleared on detonation
    TArray<TWeakObjectPtr<APawn>> WarnedPawns;

    float WarningStartTime = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Mine")
    float WarningProgress = 0.f;

public:
    UFUNCTION(BlueprintImplementableEvent, Category = "Mine")
    void OnMineArmed();

    UFUNCTION(BlueprintImplementableEvent, Category = "Mine")
    void OnMineDetonated();
};
