


#include "Player/GoHomeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/HealthComponent.h"
#include "Player/DeathNotifier.h"
#include "Interaction/CoopCarryObjectBase.h"
#include "Camera/CameraComponent.h"
#include "Core/GoHomeGameUserSettings.h"
#include "InputAction.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Interaction/ElectricSwitchboardActor.h"
#include "Net/UnrealNetwork.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/OxygenComponent.h"
#include "Item/ItemActorBase.h"
#include "Interaction/InventoryComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerState.h"
#include "UI/Nameplate/PlayerNameplateWidget.h"
#include "UObject/ConstructorHelpers.h"

AGoHomeCharacter::AGoHomeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetMesh(), "Spine_03"); // 카메라는 임시로 몸통(Spine_03)에 부착
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 70.f));
	Camera->bUsePawnControlRotation = true;

	NameplateWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("NameplateWidget"));
	NameplateWidgetComponent->SetupAttachment(GetCapsuleComponent());
	NameplateWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 122.f));
	NameplateWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	NameplateWidgetComponent->SetDrawSize(FVector2D(320.f, 48.f));
	NameplateWidgetComponent->SetPivot(FVector2D(0.5f, 1.f));
	NameplateWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameplateWidgetComponent->SetOwnerNoSee(false);
	static ConstructorHelpers::FClassFinder<UPlayerNameplateWidget> NameplateWidgetClass(TEXT("/Game/GoHome/UI/Nameplate/WBP_PlayerNameplate"));
	if (NameplateWidgetClass.Class)
	{
		// Store the widget class on the component template so the cooker follows this asset reference.
		NameplateWidgetComponent->SetWidgetClass(NameplateWidgetClass.Class);
	}

	//GetMesh()->SetOwnerNoSee(true); // 소유자가 자신을 보지 못하도록 하는 코드(일단 주석처리)

	GetMesh()->SetCastShadow(false); // 그림자 끄기

	bUseControllerRotationYaw = true; // 몸통도 시선 Yaw를 따라가게 함

	AddTickPrerequisiteComponent(GetMesh());

	// FirstPersonArmsMesh 부분
	FirstPersonArmsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArmsMesh"));
	FirstPersonArmsMesh->SetupAttachment(GetCapsuleComponent()); // GetMesh()랑 같은 부모에 붙여서 트랜스폼 맞춤
	FirstPersonArmsMesh->SetRelativeLocation(GetMesh()->GetRelativeLocation());
	FirstPersonArmsMesh->SetRelativeRotation(GetMesh()->GetRelativeRotation());
	FirstPersonArmsMesh->SetOnlyOwnerSee(true);
	FirstPersonArmsMesh->SetCastShadow(false); // 본인 시야에 이상한 팔 그림자 안 생기게

	GetMesh()->SetOwnerNoSee(true); // 본인한테는 전신 메시 안 보이게

	FlashlightSpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("FlashlightSpotLight"));
	FlashlightSpotLight->SetupAttachment(GetMesh(), "Spine_03");
	FlashlightSpotLight->SetVisibility(false);
}

void AGoHomeCharacter::BeginPlay()
{
	// UHealthComponent::BeginPlay()가 Super::BeginPlay() 안에서 곧바로 초기 HP 브로드캐스트를 쏘기 때문에,
	// 구독을 그 전에 먼저 걸어야 첫 브로드캐스트를 놓치지 않는다(안 그러면 LastKnownHP가 -1로 남아
	// 첫 데미지를 초기화 호출로 오인해서 그때만 강제 운반 해제가 안 걸림).
	if (UHealthComponent* Health = FindComponentByClass<UHealthComponent>())
	{
		Health->OnHPChanged.AddDynamic(this, &AGoHomeCharacter::HandleHPChanged);
	}

	Super::BeginPlay();

	FirstPersonArmsMesh->SetLeaderPoseComponent(GetMesh());

	// 캐릭터 수영 모드 강제 진입
	GetCharacterMovement()->SetMovementMode(MOVE_Swimming);
	GetCharacterMovement()->Buoyancy = 1.0f;

	DefaultMaxSwimSpeed = GetCharacterMovement()->MaxSwimSpeed;
	CachedOxygenComponent = FindComponentByClass<UOxygenComponent>();
	CachedInventoryComponent = FindComponentByClass<UInventoryComponent>();

	NameplateWidgetComponent->InitWidget();
	RefreshNameplate();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}

		if (PC->IsLocalController())
		{
			// 이전 맵(정산 등 UI 화면)에서 남은 눌림 상태가 새 폰을 움직이지 않게 게임 입력으로 돌아오는 시점에 비운다.
			PC->FlushPressedKeys();
			PC->SetInputMode(FInputModeGameOnly());
			PC->bShowMouseCursor = false;
		}
	}

	if (IDeathNotifier* DeathNotifier = FindComponentByInterface<IDeathNotifier>())
	{
		DeathNotifier->GetOnDeathDelegate().AddUObject(this, &AGoHomeCharacter::HandleForcedCarryRelease);
	}
}

void AGoHomeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RefreshNameplate();

	// BP가 되돌린 직후, 스프린트 중이면 다시 덮어씌운다
	if (bIsSprinting)
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->MaxSwimSpeed = DefaultMaxSwimSpeed * SprintSpeedMultiplier;
		}
	}

	if (CurrentCarryObject && (IsLocallyControlled() || HasAuthority()))
	{
		// 운반 중 몸통을 손잡이 방향으로 돌림. 서버와 소유 클라가 같은 목표로 각자 돌려야 서로 어긋나지 않음.
		// (bUseControllerRotationYaw / bOrientRotationToMovement 둘 다 꺼져 있어서 무브먼트가 덮어쓰지 않음)
		const FRotator TargetRotation(0.f, CarryFacingYaw, 0.f);
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, CarryFacingInterpSpeed));
	}


	if (IsLocallyControlled())
	{
		if (CurrentCarryObject)
		{
			// 서버가 계산한 이 캐릭터용 운반 입력(공유 이동 + 손잡이 보정)을 "내 로컬 폰"에 직접 적용 -> 표준 예측/ServerMove 흐름 그대로.
			// 운반 중 이동과 손잡이 따라가기가 전부 이 한 줄에 달려 있음(지우면 컴파일은 되지만 운반 중 캐릭터가 멈춤).
			AddMovementInput(CombinedCarryInput);
		}

		// 두 명 다 잡은 동안만 공용 카메라. 놓기/정산/강제 해제 등 모든 해제 경로가 CurrentCarryObject를 비우므로 여기서 자동 복구됨.
		// 여러 액터의 OnRep 도착 순서에 의존하지 않도록 매 틱 상태를 비교.
		const bool bWantCarryView = CurrentCarryObject && CurrentCarryObject->IsFullyCarried();
		if (bWantCarryView && !bCarryViewActive)
		{
			EnterCarryView();
		}
		else if (!bWantCarryView && bCarryViewActive)
		{
			ExitCarryView();
		}

		if (bCarryViewActive && CurrentCarryObject)
		{
			LastCarryViewYaw = CurrentCarryObject->GetHeadingRotation().Yaw;
		}
	}


	if (IsLocallyControlled() || HasAuthority())
	{
		// 본인 클라이언트거나 서버가 그 캐릭터를 볼 때
		FRotator ControlRot = GetControlRotation();
		FRotator ActorRot = GetActorRotation();
		FRotator DeltaRot = (ControlRot - ActorRot).GetNormalized();

		CurrentPitch = DeltaRot.Pitch;

		if (HasAuthority())
		{
			// 호스트(서버+로컬조종) 자기 자신인 경우 -> 바로 리플리케이트 변수에 반영
			ReplicatedPitch = CurrentPitch;
		}
		else
		{
			// 순수 원격 클라이언트인 경우 -> 서버에 전송
			ServerUpdatePitch(CurrentPitch);
		}
	}

	if (bIsFlashlightOn && FlashlightSpotLight)
	{
		// 모든 클라이언트에서 각자 로컬로 계산 - CurrentPitch는 원격 클라도 ReplicatedPitch를 통해 갱신됨.
		const FRotator ViewRotation(CurrentPitch, GetActorRotation().Yaw, 0.f);
		FlashlightSpotLight->SetWorldRotation(ViewRotation);
	}

	if (HasAuthority())
	{
		const bool bIsSwimming = GetVelocity().Size() > 50.f;

		if (bIsSwimming)
		{
			TimeSinceLastSwimNoise += DeltaTime;
			if (TimeSinceLastSwimNoise >= SwimNoiseInterval)
			{
				UGoHomeNoiseLibrary::GenerateNoise(this, GetActorLocation(), SwimNoiseRadius, SwimNoiseType, this);
				TimeSinceLastSwimNoise = 0.f;
			}
		}
		else
		{
			TimeSinceLastSwimNoise = 0.f; // 멈추면 타이머 리셋 -> 멈췄다 바로 움직였을 때 즉시 안 쏘게
		}
	}
}

void AGoHomeCharacter::RefreshNameplate()
{
	APlayerState* OwningPlayerState = GetPlayerState();
	const FString PlayerName = OwningPlayerState ? OwningPlayerState->GetPlayerName() : FString();
	if (!NameplateWidgetComponent)
	{
		return;
	}

	if (!NameplateWidgetComponent->GetUserWidgetObject())
	{
		NameplateWidgetComponent->InitWidget();
	}

	UPlayerNameplateWidget* Nameplate = Cast<UPlayerNameplateWidget>(NameplateWidgetComponent->GetUserWidgetObject());
	if (!Nameplate || (bHasAppliedPlayerName && PlayerName == LastDisplayedPlayerName))
	{
		return;
	}

	if (Nameplate->SetPlayerName(PlayerName))
	{
		LastDisplayedPlayerName = PlayerName;
		bHasAppliedPlayerName = true;
	}
}

void AGoHomeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
		EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &ThisClass::StopCarryInput);
		EIC->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ThisClass::StopCarryInput);
		EIC->BindAction(MoveUpDownAction, ETriggerEvent::Triggered, this, &ThisClass::MoveUpDown);
		EIC->BindAction(MoveUpDownAction, ETriggerEvent::Completed, this, & ThisClass::StopCarryVerticalInput);
		EIC->BindAction(MoveUpDownAction, ETriggerEvent::Canceled, this, &ThisClass::StopCarryVerticalInput);
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
		EIC->BindAction(MoveAction, ETriggerEvent::Started, this, &ThisClass::HandleFocusMoveStarted);
		EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::StartSprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::StopSprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Canceled, this, &ThisClass::StopSprint);
		EIC->BindAction(PushToTalkAction, ETriggerEvent::Started, this, &ThisClass::StartTalking);
		EIC->BindAction(PushToTalkAction, ETriggerEvent::Completed, this, &ThisClass::StopTalking);
	}
}

void AGoHomeCharacter::Move(const FInputActionValue& Value)
{
	if (bIsStunned) return;
	if (FocusedSwitchboard) return;

	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (CurrentCarryObject)
	{
		// 운반 중엔 직업 이동하지 않고 원본 입력만 서버로 보고함.
		// 같은 W 라도 이동 역할이면 전진, 회전 역할이면 위로 조향이라서 해석은 서버(CoopCarryObjectBase)가 역할에 따라 함.
		if (HasAuthority())
		{
			LastCarryMoveInput = MovementVector;
		}
		else
		{
			Server_UpdateCarryInput(MovementVector);
		}
		return;
	}

	const FRotator FullRotation = GetControlRotation();

	const FVector ForwardDirection = FRotationMatrix(FullRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(FullRotation).GetUnitAxis(EAxis::Y);
	
	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);	
}

void AGoHomeCharacter::StopCarryInput()
{
	if (!CurrentCarryObject) return;

	if (HasAuthority())
	{
		LastCarryMoveInput = FVector2D::ZeroVector;
	}
	else
	{
		Server_UpdateCarryInput(FVector2D::ZeroVector);
	}
}

void AGoHomeCharacter::StopCarryVerticalInput()
{
	if (!CurrentCarryObject) return;

	if (HasAuthority())
	{
		LastCarryVerticalInput = 0.f;
	}
	else
	{
		Server_UpdateCarryVerticalInput(0.f);
	}
}

void AGoHomeCharacter::MoveUpDown(const FInputActionValue& Value)
{
	if (bIsStunned) return;

	const float UpDownValue = Value.Get<float>();

	if (CurrentCarryObject)
	{
		// 운반 중엔 직접 적용하지 않고 서버로 보고만 함.
		// 이동 역할이면 그룹 상하 이동, 회전 역할이면 무시됨.
		if (HasAuthority())
		{
			LastCarryVerticalInput = UpDownValue;
		}
		else
		{
			Server_UpdateCarryVerticalInput(UpDownValue);
		}
		return;
	}
	AddMovementInput(FVector::UpVector, UpDownValue);
}

void AGoHomeCharacter::StartSprint()
{
	if (FocusedSwitchboard)
	{
		// 배전반 포커스 중엔 Sprint 입력을 '취소'로 재사용 - 힌트 재생 중이거나 스턴 중이어도 탈출 가능해야 함.
		FocusedSwitchboard->ServerCancelFocus(this);
		return;
	}

	if (bIsStunned) return;

	if(CurrentCarryObject)
	{
		// 운반 중엔 Shift가 개인 스프린트가 아니라 공유 이동 부스트 요청으로 재해석됨.
		// 실제로 반영되는지는 CoopCarryObjectBase::Tick에서 "지금 회전 역할인지" 여부로 결정 -> 이동 역할이 눌러도 무시됨.
		if(HasAuthority())
		{ 
			bIsCarryBoosting = true;
		}
		else
		{
			ServerSetCarryBoosting(true);
		}
		return;
	}

	ApplySprintState(true);

	if (!HasAuthority())
	{
		ServerSetSprinting(true);
	}
}

void AGoHomeCharacter::StopSprint()
{
	if (CurrentCarryObject)
	{
		if (HasAuthority())
		{
			bIsCarryBoosting = false;
		}
		else
		{
			ServerSetCarryBoosting(false);
		}
		return;
	}

	ApplySprintState(false);

	if (!HasAuthority())
	{
		ServerSetSprinting(false);
	}
}

void AGoHomeCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	// 협동 운반 중엔 서버도 스프린트 요청 무시.
	if (bNewSprinting && CurrentCarryObject) return;

	ApplySprintState(bNewSprinting);
}

void AGoHomeCharacter::ApplySprintState(bool bNewSprinting)
{
	if (bIsSprinting == bNewSprinting)
	{
		return;
	}

	bIsSprinting = bNewSprinting;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxSwimSpeed = bIsSprinting ? DefaultMaxSwimSpeed * SprintSpeedMultiplier : DefaultMaxSwimSpeed;
	}

	if (HasAuthority() && CachedOxygenComponent)
	{
		CachedOxygenComponent->SetSprintDrainMultiplier(bIsSprinting ? SprintOxygenDrainMultiplier : 1.f);
	}
}

void AGoHomeCharacter::Client_ForceStopSprint_Implementation()
{
	ApplySprintState(false);
}

void AGoHomeCharacter::Look(const FInputActionValue& Value)
{
	if (bIsStunned) { return; }
	if (FocusedSwitchboard) { return; }
	if (bCarryViewActive) { return; } // 운반 중 공용 화면 - 두 사람이 항상 같은 화면을 보도록 둘러보기 없음.

	const UGoHomeGameUserSettings* Settings = UGoHomeGameUserSettings::Get();
	const float Sensitivity = Settings ? Settings->GetMouseSensitivity() : 1.f;
	const FVector2D LookVector = Value.Get<FVector2D>() * Sensitivity;
	AddControllerYawInput(LookVector.X);
	AddControllerPitchInput(LookVector.Y);
}

void AGoHomeCharacter::StartTalking()
{
	if (APlayerController* PlayerController = GetController<APlayerController>())
	{
		PlayerController->ToggleSpeaking(true);
	}
}

void AGoHomeCharacter::StopTalking()
{
	if (APlayerController* PlayerController = GetController<APlayerController>())
	{
		PlayerController->ToggleSpeaking(false);
	}
}

void AGoHomeCharacter::AttachItemToRightHand(UStaticMeshComponent* ItemMeshComponent)
{
	if (!ItemMeshComponent) return;

	ItemMeshComponent->AttachToComponent(
		GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		RightHandSocketName);

	// bIsHoldingItem = true; 는
	// UInventoryComponent(SetActiveSlot/RemoveItem)가 단일 소스로 관리함 -> 여기선 안건드림.
}

void AGoHomeCharacter::SetHoldingItem(bool bHolding)
{
	if (HasAuthority())
	{
		bIsHoldingItem = bHolding;
	}
}

void AGoHomeCharacter::DetachItemFromRightHand()
{
	// bIsHoldingItem은 UInventoryComponent(SetActiveSlot/RemoveItem)가 단일 소스로 관리함 - 여기선 안 건드림.
	// 실제 Detach(월드에 다시 떨어뜨리는 것)는 ItemActorBase 쪽에서
	// 자기 자신을 Detach + 위치 지정하는 게 자연스러움 (소유권 문제라).
}

void AGoHomeCharacter::OnRep_IsHoldingItem()
{
	// 필요하면 여기서 사운드/이펙트 등 클라 전용 후처리
}

void AGoHomeCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGoHomeCharacter, bIsHoldingItem);
	DOREPLIFETIME_CONDITION(AGoHomeCharacter, ReplicatedPitch, COND_SkipOwner);
	DOREPLIFETIME(AGoHomeCharacter, CurrentCarryObject);
	DOREPLIFETIME_CONDITION(AGoHomeCharacter, CombinedCarryInput, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AGoHomeCharacter, CarryFacingYaw, COND_OwnerOnly);
	DOREPLIFETIME(AGoHomeCharacter, bIsStunned);
	DOREPLIFETIME(AGoHomeCharacter, bIsFlashlightOn);
}

void AGoHomeCharacter::OnRep_ReplicatedPitch()
{
	CurrentPitch = ReplicatedPitch;
}

void AGoHomeCharacter::ServerUpdatePitch_Implementation(float NewPitch)
{
	ReplicatedPitch = NewPitch;
}

void AGoHomeCharacter::Server_UpdateCarryInput_Implementation(FVector2D MoveInput)
{
	LastCarryMoveInput = MoveInput;
}

void AGoHomeCharacter::Server_UpdateCarryVerticalInput_Implementation(float VerticalInput)
{
	LastCarryVerticalInput = VerticalInput;
}

void AGoHomeCharacter::ServerSetCarryBoosting_Implementation(bool bNewBoosting)
{
	bIsCarryBoosting = bNewBoosting;
}

void AGoHomeCharacter::SetCoopCarryObject(ACoopCarryObjectBase* NewCarryObject)
{
	CurrentCarryObject = NewCarryObject;
	
	// 잡을 때든 놓을 때든 잔여 입력값 리셋 -> 이전 세션 값이 새 세션에 넘어가지 않게.
	// (놓은 직후) 늦게 도착한 Unreliable RPC, 운반 끝난 뒤 키를 떼서 Stop*이 무시된 경우 모두 여기서 정리.

	LastCarryMoveInput = FVector2D::ZeroVector;
	LastCarryVerticalInput = 0.f;
	bIsCarryBoosting = false;
	CombinedCarryInput = FVector::ZeroVector;

	if (NewCarryObject)
	{
		// 목표 몸통 방향을 현재 방향으로 초기화 -> 이전 운반의 값이 남아 잡는 순간 휙 도는 것 방지.
		CarryFacingYaw = GetActorRotation().Yaw;

		// 운반 시작 지점에 스프린트 중이었을 수 있으니 강제로 끔.
		// 서버 + 원격 클라 둘다.
		ApplySprintState(false);
		if (!IsLocallyControlled())
		{
			Client_ForceStopSprint();
		}
	}

	OnRep_CurrentCarryObject(); // 서버 자신에게는 RepNotify가 안 뜨므로 직접 호출 -> 호스트 로컬도 즉시 반영.
}

void AGoHomeCharacter::OnRep_CurrentCarryObject()
{
	const bool bIsCarrying = (CurrentCarryObject != nullptr);

	// 운반 중엔 시야는 자유롭게, 몸통 Yaw는 고정(잡은 모습 유지).
	// 운반 아니면 원래대로 시야를 따라감.
	bUseControllerRotationYaw = !bIsCarrying;

	// 1인칭 팔은 몸통(캡슐) 기준 Yaw를 따라가는데, 운반 중엔 몸통 Yaw가 고정되고 카메라만 돌아서
	// 팔이 시야랑 어긋나 이상하게 늘어져 보임 -> 운반 중엔 숨김(잡는 자세 애니메이션은 아직 없음).
	if (FirstPersonArmsMesh)
	{
		FirstPersonArmsMesh->SetVisibility(!bIsCarrying);
	}

	// 운반 중엔 캡슐이 운반 오브젝트를 무시 -> 오브젝트가 회전하며 캡슐을 밀어내는 것 방지.
	// IgnoreActorWhenMoving은 리플리케이트되지 않으므로, 서버/클라 각자 여기서 적용 및 해제함.
	if (CapsuleIgnoredCarryObject.IsValid())
	{
		GetCapsuleComponent()->IgnoreActorWhenMoving(CapsuleIgnoredCarryObject.Get(), false);
	}

	CapsuleIgnoredCarryObject = CurrentCarryObject;

	if (CurrentCarryObject)
	{
		GetCapsuleComponent()->IgnoreActorWhenMoving(CurrentCarryObject, true);
	}

	// 운반 중엔 캐리어의 충돌 컴포넌트가 Camera 채널을 무시 -> 공용 카메라 스프링암이 캐리어(특히 카메라 쪽에 선 이동 역할)에
	// 걸려 코앞으로 당겨졌다 풀렸다 하는 깜빡임 방지. (스프링암은 소유 액터=운반 오브젝트만 무시하고, Pawn/CharacterMesh 프로필은 Camera를 막음)
	// 스프링암 검사는 머신마다 로컬이라 각 머신에서 두 캐리어 모두 무시해야 함 -> 모든 머신에서 캐리어마다 실행되는 이 OnRep에서 처리.
	SetCarryCameraCollisionIgnored(bIsCarrying);
}

void AGoHomeCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && CurrentCarryObject)
	{
		CurrentCarryObject->ReleaseCarriers();
	}

	Super::EndPlay(EndPlayReason);
}

void AGoHomeCharacter::HandleHPChanged(float CurrentHP, float MaxHP)
{
	if (LastKnownHP >= 0.f && CurrentHP < LastKnownHP)
	{
		HandleForcedCarryRelease();
	}
	LastKnownHP = CurrentHP;
}

void AGoHomeCharacter::HandleForcedCarryRelease()
{
	if (HasAuthority() && CurrentCarryObject)
	{
		CurrentCarryObject->ReleaseCarriers();
	}
}


void AGoHomeCharacter::NotifyHit(UPrimitiveComponent* MyComp, 
	                             AActor* Other, 
	                             UPrimitiveComponent* OtherComp, 
	                             bool bSelfMoved, 
	                             FVector HitLocation, 
	                             FVector HitNormal, 
	                             FVector NormalImpulse, 
	                             const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	HandleInventoryBreakOnHit(Other);
}

void AGoHomeCharacter::HandleInventoryBreakOnHit(AActor* OtherActor)
{
	if (!HasAuthority() || !CachedInventoryComponent) return;

	const float Now = GetWorld()->GetTimeSeconds();
	
	if (Now < NextItemBreakEligibleTime) return; // 쿨다운 중 속도 계산 조차 하지 않고 건너뜀.

	const FVector OtherVelocity = OtherActor ? OtherActor->GetVelocity() : FVector::ZeroVector;
	const float ImpactSpeed = (GetVelocity() - OtherVelocity).Size(); // 상대 속도.

	bool bAnyBroke = false;

	for (int32 SlotIndex = 0; SlotIndex < CachedInventoryComponent->GetInventorySlotCount(); ++SlotIndex)
	{
		if (AItemActorBase* Item = CachedInventoryComponent->GetItemInSlot(SlotIndex))
		{
			if (Item->TryApplyBreakFromImpact(ImpactSpeed))
			{
				bAnyBroke = true;
			}
		}
	}

	if (bAnyBroke)
	{
		NextItemBreakEligibleTime = Now + ItemBreakCooldownSeconds; // 실제로 깨졌을 때만 쿨다운 시작.
	}
}



void AGoHomeCharacter::SetCombinedCarryInput(const FVector& NewInput)
{
	CombinedCarryInput = NewInput; 
}


void AGoHomeCharacter::ApplyStun_Implementation(float Duration, FVector KnockbackImpulse, AActor* InInstigator)
{
	if (!HasAuthority()) return;

	bIsStunned = true;
	LaunchCharacter(KnockbackImpulse, true, true);

	if (IsLocallyControlled())
	{
		OnRep_IsStunned(); // 서버 자신은 RepNotify 안 뜸.
	}
	else
	{
		Client_ApplyStun(Duration, KnockbackImpulse);
	}

	GetWorldTimerManager().SetTimer(StunTimerHandle, this, &AGoHomeCharacter::EndStun, Duration, false);
}

void AGoHomeCharacter::Client_ApplyStun_Implementation(float Duration, FVector KnockbackImpulse)
{
	// 로컬 예측을 이 프레임에 바로 멈추기 위해 직접 반영.
	bIsStunned = true;
	LaunchCharacter(KnockbackImpulse, true, true);
}

void AGoHomeCharacter::EndStun()
{
	if (!HasAuthority()) return;

	bIsStunned = false;

	if (IsLocallyControlled())
	{
		OnRep_IsStunned();
	}
}

void AGoHomeCharacter::OnRep_IsStunned()
{
	// 필요한 경우 여기서 스턴 사운드/이펙트 등 클라 전용 후처리.
}

void AGoHomeCharacter::EnterSwitchboardFocus(AElectricSwitchboardActor* Switchboard)
{
	FocusedSwitchboard = Switchboard;
	HighlightedWireIndex = 0;
	EnteredPasswordDigits.Reset();

	UpdateWireHighlight(-1, HighlightedWireIndex);

	// 추가: FocusCamera로 컷 하면 SetOwnerNoSee가 무력화돼 내 몸이 보이므로 직접 숨김.
	GetMesh()->SetVisibility(false, true); 

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetViewTargetWithBlend(Switchboard, 0.3f);
	}

	StartHintPlayback();
}

void AGoHomeCharacter::ExitSwitchboardFocus()
{
	GetWorld()->GetTimerManager().ClearTimer(HintPlaybackTimerHandle);
	bPlayingHint = false;
	HintPlaybackStep = -1;

	UpdateWireHighlight(HighlightedWireIndex, -1);

	GetMesh()->SetVisibility(true, true); // 메쉬 복원.

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetViewTargetWithBlend(this, 0.3f);
	}

	FocusedSwitchboard = nullptr;
	HighlightedWireIndex = -1;

}

void AGoHomeCharacter::Client_EnterSwitchboardFocus_Implementation(AElectricSwitchboardActor* Switchboard)
{
	EnterSwitchboardFocus(Switchboard);
}

void AGoHomeCharacter::Client_ExitSwitchboardFocus_Implementation()
{
	ExitSwitchboardFocus();
}

void AGoHomeCharacter::HandleFocusMoveStarted(const FInputActionValue& Value)
{
	if (!FocusedSwitchboard || bPlayingHint) return;

	const FVector2D MovementVector = Value.Get<FVector2D>();

	int32 RowDelta = 0;
	int32 ColDelta = 0;

	if (MovementVector.X > 0.5f) ColDelta = 1;
	else if (MovementVector.X < -0.5f) ColDelta = -1;

	if (MovementVector.Y > 0.5f) RowDelta = -1; // W = 위.
	else if (MovementVector.Y < -0.5f) RowDelta = 1; // S = 아래.

	if (RowDelta != 0 || ColDelta != 0)
	{
		MoveHighlightedKey(RowDelta, ColDelta);
	}
}

static void IndexToRowCol(int32 Index, int32& OutRow, int32& OutCol)
{
	if (Index == 10) { OutRow = 0; OutCol = 0; return; } // Back
	if (Index == 11) { OutRow = 0; OutCol = 4; return; } // Enter
	if (Index >= 5) { OutRow = 1; OutCol = Index - 5; return; } // Wire5~9 = 윗줄
	OutRow = 2; OutCol = Index; // Wire0~4 = 아랫줄
}

static int32 RowColToIndex(int32 Row, int32 Col)
{
	if (Row <= 0)
	{
		return (Col <= 2) ? 10 : 11; // 왼쪽 절반 = Back, 오른쪽 절반 = Enter
	}

	const int32 ClampedCol = FMath::Clamp(Col, 0, 4);

	if (Row == 1)
	{
		return 5 + ClampedCol; // Wire5~9 (윗줄)
	}

	return ClampedCol; // Wire0~4 (아랫줄)
}

void AGoHomeCharacter::MoveHighlightedKey(int32 RowDelta, int32 ColDelta)
{
	if (!FocusedSwitchboard) return;

	int32 Row, Col;
	IndexToRowCol(HighlightedWireIndex, Row, Col);

	const int32 NewRow = FMath::Clamp(Row + RowDelta, 0, 2);

	int32 NewCol;
	if (NewRow == 0)
	{
		// Back/Enter 두 칸뿐 -> 좌우 입력 한번으로 바로 토글.
		if (ColDelta > 0) NewCol = 4; // Enter.
		else if (ColDelta < 0) NewCol = 0; // Back.
		else NewCol = (Col <= 2) ? 0 : 4; // 위/아래로 넘어온 경우 가까운 쪽 유지.
	}
	else
	{
		NewCol = FMath::Clamp(Col + ColDelta, 0, 4);
	}

	const int32 OldIndex = HighlightedWireIndex;
	HighlightedWireIndex = RowColToIndex(NewRow, NewCol);

	UpdateWireHighlight(OldIndex, HighlightedWireIndex);
}



void AGoHomeCharacter::PressHighlightedKey()
{
	if (!FocusedSwitchboard || bPlayingHint) return;

	const int32 DigitCount = FocusedSwitchboard->GetWireTargetCount(); // 10

	if (HighlightedWireIndex < DigitCount)
	{
		if (EnteredPasswordDigits.Num() < FocusedSwitchboard->GetPasswordLength())
		{
			EnteredPasswordDigits.Add(HighlightedWireIndex);
			FocusedSwitchboard->UpdatePasswordDisplay(EnteredPasswordDigits);
		}
	}
	else if (HighlightedWireIndex == DigitCount) // Back
	{
		if (EnteredPasswordDigits.Num() > 0)
		{
			EnteredPasswordDigits.Pop();
			FocusedSwitchboard->UpdatePasswordDisplay(EnteredPasswordDigits);
		}
	}
	else // Enter
	{
		FocusedSwitchboard->ServerSubmitPassword(EnteredPasswordDigits, this);
	}
}

void AGoHomeCharacter::UpdateWireHighlight(int32 OldIndex, int32 NewIndex)
{
	if (!FocusedSwitchboard) return;

	if (UPrimitiveComponent* OldWire = FocusedSwitchboard->GetKeypadElementMesh(OldIndex))
	{
		OldWire->SetRenderCustomDepth(false);
	}
	if (UPrimitiveComponent* NewWire = FocusedSwitchboard->GetKeypadElementMesh(NewIndex))
	{
		NewWire->SetRenderCustomDepth(true);
		NewWire->SetCustomDepthStencilValue(1);
	}
}

void AGoHomeCharacter::StartHintPlayback()
{
	if (!FocusedSwitchboard) return;

	bPlayingHint = true;
	HintPlaybackStep = 0;
	bHintShowingGap = true;

	SetAllWiresOff();
	GetWorld()->GetTimerManager().SetTimer(HintPlaybackTimerHandle, this, 
		                                   &AGoHomeCharacter::AdvanceHintPlayback, 
		                                   FocusedSwitchboard->GetHintGapDuration(), false);
}

void AGoHomeCharacter::AdvanceHintPlayback()
{
	if (!FocusedSwitchboard) return;

	if (bHintShowingGap)
	{
		// 갭 끝남 -> 이번 라운드 색 패턴 표시.
		bHintShowingGap = false;

		const TArray<FLinearColor>& Palette = FocusedSwitchboard->GetHintColorPalette();
		if (Palette.Num() < 2) return;

		const int32 MajorIdx = FMath::RandRange(0, Palette.Num() - 1);
		int32 MinorIdx;
		do { MinorIdx = FMath::RandRange(0, Palette.Num() - 1); } while (MinorIdx == MajorIdx);

		const int32 CorrectIndex = FocusedSwitchboard->GetCorrectWireIndexForStep(HintPlaybackStep);

		for (int32 i = 0; i < FocusedSwitchboard->GetWireTargetCount(); ++i)
		{
			const FLinearColor& Color = (i == CorrectIndex) ? Palette[MinorIdx] : Palette[MajorIdx];
			SetWireColor(FocusedSwitchboard->GetWireTarget(i), Color);
		}

		GetWorld()->GetTimerManager().SetTimer(HintPlaybackTimerHandle, this,
			&AGoHomeCharacter::AdvanceHintPlayback, FocusedSwitchboard->GetHintRoundDuration(), false);
	}
	else
	{
		// 이번 라운드 표시 끝 -> 다음 라운드로.
		++HintPlaybackStep;

		if (HintPlaybackStep >= FocusedSwitchboard->GetPasswordLength())
		{
			SetAllWiresOff();
			bPlayingHint = false;
			HintPlaybackStep = -1;
			return; // 힌트 재생 끝 - 이제 조작 가능.
		}

		bHintShowingGap = true;
		SetAllWiresOff();
		GetWorld()->GetTimerManager().SetTimer(HintPlaybackTimerHandle, this,
			&AGoHomeCharacter::AdvanceHintPlayback, FocusedSwitchboard->GetHintGapDuration(), false);
	}
}

void AGoHomeCharacter::SetWireColor(UMeshComponent* Wire, const FLinearColor& Color)
{
	if (!Wire) return;

	if (UMaterialInstanceDynamic* MID = Wire->CreateAndSetMaterialInstanceDynamic(0))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AGoHomeCharacter::SetAllWiresOff()
{
	if (!FocusedSwitchboard) return;

	for (int32 i = 0; i < FocusedSwitchboard->GetWireTargetCount(); ++i)
	{
		SetWireColor(FocusedSwitchboard->GetWireTarget(i), FLinearColor::Black);
	}
}


void AGoHomeCharacter::ToggleFlashlight()
{
	const bool bNewIsOn = !bIsFlashlightOn;

	if (HasAuthority())
	{
		// 서버(호스트 자신 포함) - 권위값 직접 갱신. 서버 자신은 RepNotify가 안 뜨므로 시각 갱신도 직접.
		bIsFlashlightOn = bNewIsOn;
		UpdateFlashlightVisual(bNewIsOn);
	}

	else
	{
		// 원격 클라 - 내 화면엔 즉시 반영(로컬 예측), 서버엔 권위 갱신 요청.
		UpdateFlashlightVisual(bNewIsOn);
		ServerToggleFlashlight();
	}
}

void AGoHomeCharacter::ServerToggleFlashlight_Implementation()
{
	bIsFlashlightOn = !bIsFlashlightOn;
	UpdateFlashlightVisual(bIsFlashlightOn); // 서버(리슨 서버) 자신의 화면에도 반영 - RepNotify가 서버 자신에겐 안 뜸.
}

void AGoHomeCharacter::OnRep_IsFlashlightOn()
{
	UpdateFlashlightVisual(bIsFlashlightOn);
}

void AGoHomeCharacter::UpdateFlashlightVisual(bool bNewIsOn)
{
	if (FlashlightSpotLight)
	{
		FlashlightSpotLight->SetVisibility(bNewIsOn);
	}
}

FVector AGoHomeCharacter::GetCameraWorldLocation() const
{
	return Camera->GetComponentLocation();
}

bool AGoHomeCharacter::IsCarryMover() const
{
	return CurrentCarryObject && CurrentCarryObject->IsMover(this);
}

void AGoHomeCharacter::EnterCarryView()
{
	bCarryViewActive = true;
	CarryViewObject = CurrentCarryObject;
	LastCarryViewYaw = CurrentCarryObject->GetHeadingRotation().Yaw;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// 배전반과 같은 패턴 - 로컬에서만 뷰 타겟 전환(복제 불필요).
		// 뷰 타겟이 폰이 아니게 되면 OwnerNoSee가 풀려 자기 몸이 보이고, 1인칭 팔(OnlyOwnerSee)은 안 보임 -> 3인칭에 필요한 그대로.
		PC->SetViewTargetWithBlend(CurrentCarryObject, CarryViewBlendTime);
	}

	OnCarryViewChanged.Broadcast(true, IsCarryMover());
}

void AGoHomeCharacter::ExitCarryView()
{
	bCarryViewActive = false;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// 다른 시스템(사망 관전 등)이 이미 뷰 타겟을 바꿨으면 덮어쓰지 않음.
		// 정산으로 오브젝트가 파괴돼 뷰 타겟이 무효가 된 경우는 자기 폰으로 복귀.
		AActor* ViewTarget = PC->GetViewTarget();
		if (!IsValid(ViewTarget) || ViewTarget == this || ViewTarget == CarryViewObject.Get())
		{
			// 1인칭이 공용 화면이 보던 수평 방향에서 이어지게 -> 몸도 그 방향을 향한 채 풀림.
			PC->SetControlRotation(FRotator(0.f, LastCarryViewYaw, 0.f));
			PC->SetViewTargetWithBlend(this, CarryViewBlendTime);
		}
	}

	CarryViewObject = nullptr;

	OnCarryViewChanged.Broadcast(false, false);
}


void AGoHomeCharacter::SetCarryCameraCollisionIgnored(bool bIgnore)
{
	if (bIgnore)
	{
		// 이미 적용된 상태면 다시 저장하지 않음(원래 값 대신 Ignore를 저장해버리는 것 방지).
		if (CarryCameraIgnoredComponents.Num() > 0) return;

		TArray<UPrimitiveComponent*> Primitives;
		GetComponents<UPrimitiveComponent>(Primitives);

		for (UPrimitiveComponent* Primitive : Primitives)
		{
			const ECollisionResponse Original = Primitive->GetCollisionResponseToChannel(ECC_Camera);
			if (Original != ECR_Ignore)
			{
				CarryCameraIgnoredComponents.Emplace(Primitive, Original);
				Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			}
		}
	}

	else
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, ECollisionResponse>& Entry : CarryCameraIgnoredComponents)
		{
			if (UPrimitiveComponent* Primitive = Entry.Key.Get())
			{
				Primitive->SetCollisionResponseToChannel(ECC_Camera, Entry.Value);
			}
		}
		CarryCameraIgnoredComponents.Reset();
	}
}
