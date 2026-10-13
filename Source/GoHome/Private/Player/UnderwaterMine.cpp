


#include "Player/UnderwaterMine.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Player/Damageable.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Core/AnnouncerComponent.h"

AUnderwaterMine::AUnderwaterMine()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true;

    TriggerArea = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerArea"));
    SetRootComponent(TriggerArea);
    TriggerArea->InitSphereRadius(500.f);
    TriggerArea->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
    TriggerArea->SetGenerateOverlapEvents(true);

    MineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MineMesh"));
    MineMesh->SetupAttachment(TriggerArea);
    MineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AUnderwaterMine::BeginPlay()
{
    Super::BeginPlay();
    TriggerArea->OnComponentBeginOverlap.AddDynamic(this, &AUnderwaterMine::OnTriggerBeginOverlap);
}

void AUnderwaterMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AUnderwaterMine, State);
}

void AUnderwaterMine::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (State == EMineState::Warning && WarningDuration > 0.f)
    {
        const float Elapsed = GetWorld()->GetTimeSeconds() - WarningStartTime;
        WarningProgress = FMath::Clamp(Elapsed / WarningDuration, 0.f, 1.f);
    }
}

void AUnderwaterMine::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!HasAuthority() || bHasTriggered)
    {
        return;
    }

    if (!Cast<ACharacter>(OtherActor))
    {
        return;
    }

    bHasTriggered = true;
    ArmMine();
}

void AUnderwaterMine::ArmMine()
{
    State = EMineState::Warning;
    HandleStateChanged();

    // Announce to living players inside the blast radius at arm time (one line per mine)
    TArray<AActor*> IgnoreActors;
    TArray<AActor*> InRange;
    UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), ExplosionRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        ACharacter::StaticClass(), IgnoreActors, InRange);
    for (AActor* Actor : InRange)
    {
        if (APawn* Pawn = Cast<APawn>(Actor); Pawn && Pawn->IsPlayerControlled())
        {
            UAnnouncerComponent::SendToPawnOwner(Pawn, EAnnouncerEvent::MineArmed, GetFName());
            WarnedPawns.Add(Pawn);
        }
    }

    GetWorldTimerManager().SetTimer(DetonationTimerHandle, this, &AUnderwaterMine::Detonate, WarningDuration, false);
}

void AUnderwaterMine::Detonate()
{
    TArray<AActor*> IgnoreActors;
    TArray<AActor*> OverlappingActors;
    UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), ExplosionRadius,
        TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
        ACharacter::StaticClass(), IgnoreActors, OverlappingActors);

    for (AActor* Actor : OverlappingActors)
    {
        if (UActorComponent* DamageableComponent = Actor->FindComponentByInterface(UDamageable::StaticClass()))
        {
            IDamageable::Execute_ApplyDamage(DamageableComponent, ExplosionDamage, this, DamageTypeName);
        }
    }

    State = EMineState::Detonated;
    HandleStateChanged();

    // The warning is moot once it has gone off
    for (const TWeakObjectPtr<APawn>& Pawn : WarnedPawns)
    {
        UAnnouncerComponent::CancelForPawnOwner(Pawn.Get(), GetFName());
    }
    WarnedPawns.Reset();

    SetLifeSpan(5.f);
}

void AUnderwaterMine::OnRep_State()
{
    HandleStateChanged();
}

void AUnderwaterMine::HandleStateChanged()
{
    switch (State)
    {
    case EMineState::Warning:
        WarningStartTime = GetWorld()->GetTimeSeconds();
        WarningProgress = 0.f;
        OnMineArmed();
        break;
    case EMineState::Detonated:
        TriggerArea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MineMesh->SetVisibility(false);

        if (ExplosionVFXAsset)
        {
            const FVector VFXSpawnLocation = GetActorLocation() + FVector(0.f, 0.f, 50.f);
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ExplosionVFXAsset, VFXSpawnLocation);
        }

        OnMineDetonated();
        break;
    default:
        break;
    }
}