#include "Interaction/BreakableWallActor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Navigation/NavLinkProxy.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "AI/NoiseType.h"

ABreakableWallActor::ABreakableWallActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	IntactMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IntactMesh"));
	RootComponent = IntactMesh;
	IntactMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	IntactMesh->SetCollisionObjectType(ECC_WorldStatic);
}

void ABreakableWallActor::BeginPlay()
{
	Super::BeginPlay();

	// 통로 링크는 부서지기 전엔 비활성 - 에디터에서 잘못 켜져있어도 방어됨.
	if (HasAuthority() && PassageNavLink)
	{
		PassageNavLink->SetSmartLinkEnabled(false);
	}
}

void ABreakableWallActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABreakableWallActor, HitCount);
}

void ABreakableWallActor::ServerHit(AActor* InInstigator)
{
	if (!HasAuthority() || HitCount >= HitsToBreak) return;

	++HitCount;
	ApplyDamageState(true); // 서버 자신은 OnRep이 안 뜨므로 수동 호출.

	const bool bJustBroke = (HitCount >= HitsToBreak);

	if (bJustBroke)
	{
		// 통로 열기 (AI 경로탐색은 서버에서만 돎 -> 서버 전용으로 충분).
		if (PassageNavLink)
		{
			PassageNavLink->SetSmartLinkEnabled(true);
		}
		UGoHomeNoiseLibrary::GenerateNoise(this, GetActorLocation(), BreakNoiseRadius, BreakNoiseType,
			InInstigator ? InInstigator : this);
	}
	else
	{
		UGoHomeNoiseLibrary::GenerateNoise(this, GetActorLocation(), HitNoiseRadius, HitNoiseType,
			InInstigator ? InInstigator : this);
	}
}

void ABreakableWallActor::OnRep_HitCount()
{
	// 이미 begun play면 런타임 타격 -> 연출 재생. 아니면 늦게 접속한 클라의 상태 동기화 -> 스왑만.
	ApplyDamageState(HasActorBegunPlay());
}

void ABreakableWallActor::ApplyDamageState(bool bPlayEffects)
{
	const int32 MeshIndex = HitCount - 1;
	if (DamageStageMeshes.IsValidIndex(MeshIndex) && DamageStageMeshes[MeshIndex])
	{
		IntactMesh->SetStaticMesh(DamageStageMeshes[MeshIndex]);
	}

	const bool bBroken = HitCount >= HitsToBreak;
	if (bBroken)
	{
		IntactMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		IntactMesh->SetCanEverAffectNavigation(false);
	}

	if (!bPlayEffects) return;

	if (bBroken)
	{
		if (BreakEffect)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BreakEffect, GetActorLocation(), GetActorRotation());
		}
		if (BreakSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, BreakSound, GetActorLocation(), 1.f, 1.f, 0.f, BreakSoundAttenuation);
		}
	}
	else
	{
		if (HitEffect)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitEffect, GetActorLocation(), GetActorRotation());
		}
		if (HitSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation(), 1.f, FMath::FRandRange(0.9f, 1.1f), 0.f, HitSoundAttenuation);
		}
	}
}