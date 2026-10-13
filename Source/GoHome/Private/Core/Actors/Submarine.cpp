//

#include "Core/Actors/Submarine.h"

#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Core/DockingDoorComponent.h"
#include "Core/GoHomeGameState.h"
#include "Player/Damageable.h"

ASubmarine::ASubmarine()
{
	PrimaryActorTick.bCanEverTick = false;

	InteriorVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteriorVolume"));
	SetRootComponent(InteriorVolume);

	// 플레이어 폰 존재 여부만 판정하면 되므로 쿼리 전용 + Pawn 채널만 오버랩 반응.
	InteriorVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteriorVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteriorVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// 기본 크기는 자리표시자. 에디터에서 잠수정 내부에 맞춰 조정한다.
	InteriorVolume->SetBoxExtent(FVector(200.f, 150.f, 100.f));
}

void ASubmarine::BeginPlay()
{
	Super::BeginPlay();

	// 문 상태에 따른 처리를 위한 델리게이트 바인딩
	if (AGoHomeGameState* GoHomeGameState = GetWorld()->GetGameState<AGoHomeGameState>())
	{
		if (UDockingDoorComponent* DoorComponent = GoHomeGameState->GetDockingDoorComponent())
		{
			DoorComponent->OnDoorStateChanged.AddDynamic(this, &ASubmarine::HandleDoorStateChanged);
		}
	}
}

bool ASubmarine::IsLocationInsideInterior(const FVector& WorldLocation) const
{
	if (!InteriorVolume)
	{
		return false;
	}

	const FVector LocalLocation = InteriorVolume->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = InteriorVolume->GetUnscaledBoxExtent();

	return FMath::Abs(LocalLocation.X) <= Extent.X
		&& FMath::Abs(LocalLocation.Y) <= Extent.Y
		&& FMath::Abs(LocalLocation.Z) <= Extent.Z;
}

void ASubmarine::HandleDoorStateChanged(bool bOpen)
{
	//  킬 판정은 닫힘 상태일 때, 서버 권위에서 이뤄져야 함
	if (bOpen || !HasAuthority())
	{
		return;
	}
	
	// 잠수정 내부에 있는 Actor(플레이어) 체크
	TArray<AActor*> InteriorActors;
	InteriorVolume->GetOverlappingActors(InteriorActors, APawn::StaticClass());
	
	// 잠수정 외부에 있는 플레이어 체크
	for (TActorIterator<APawn> Iter(GetWorld()); Iter; ++Iter)
	{
		// 잠수정에 있는 플레이어는 스킵
		APawn* Pawn = *Iter;
		if (InteriorActors.Contains(Pawn))
		{
			continue;
		}
		
		// 컨트롤러가 없거나 플레이어 상태가 없는(플레이어가 아닌) 폰은 스킵
		if (Pawn->GetController() == nullptr || Pawn->GetPlayerState() == nullptr)
		{
			continue;
		}
		
		// 매우 큰 대미지를 주어 즉사 처리
		if (UActorComponent* DamageableComponent = Pawn->FindComponentByInterface(UDamageable::StaticClass()))
		{
			IDamageable::Execute_ApplyDamage(DamageableComponent, TNumericLimits<float>::Max(), this, TEXT("SubmarineLeftBehind"));
		}
		
	}
}
