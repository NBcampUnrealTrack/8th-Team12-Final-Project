// Copyright Epic Games, Inc. All Rights Reserved.

#include "FebriculaCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Febricula.h"
#include "Animation/CharacterAnimationComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Interaction/InteractableProp.h"
#include "Interaction/InteractionComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Public/Level/HingedActor.h"

void AFebriculaCharacter::OnComponentBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                                  UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UE_LOG(LogTemp, Warning, TEXT("%s Interact Start Overlap!"), *OtherActor->GetName());
	if (AInteractableProp* InteractableActor = Cast<AInteractableProp>(OtherActor))
	{
		OverlappedActors.AddUnique(InteractableActor);
	}
}

void AFebriculaCharacter::OnComponentEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("%s Interact End Overlap!"), *OtherActor->GetName());
	if (AInteractableProp* InteractableActor = Cast<AInteractableProp>(OtherActor))
	{
		OverlappedActors.Remove(InteractableActor);
	}

}

AFebriculaCharacter::AFebriculaCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	WalkSpeed = 500.f;
	SprintSpeed = 1000.f;

	// 버거집은 좁은 실내라 뛰어다닐 곳이 아니다.
	SprintBlockedLevels.Add(TEXT("L_BurgerHouse"));

	// 손님과 같은 속도로 걷게 하기 위한 레벨별 걷기 속도 오버라이드.
	LevelWalkSpeedOverrides.Add(TEXT("L_BurgerHouse"), 350.f);
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	InteractComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractComponent"));
	CharacterAnimationComponent = CreateDefaultSubobject<UCharacterAnimationComponent>(TEXT("CharacterAnimationComponent"));

}

void AFebriculaCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 생성자는 레벨을 모르므로, 레벨별 걷기 속도는 BeginPlay에서 적용한다.
	GetCharacterMovement()->MaxWalkSpeed = GetEffectiveWalkSpeed();
}

void AFebriculaCharacter::FaceTowards(const AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	// 위아래로 기울어지면 안 되므로 Yaw만 반영한다.
	if (!ToTarget.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, ToTarget.Rotation().Yaw, 0.f));
	}
}

void AFebriculaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {

		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFebriculaCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFebriculaCharacter::Look);

		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AFebriculaCharacter::StartSprint);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFebriculaCharacter::EndSprint);

		// Dialogue Fast Forward (Ctrl 홀드로 대사 빠르게 넘기기)
		EnhancedInputComponent->BindAction(FastForwardAction, ETriggerEvent::Started, this, &AFebriculaCharacter::StartFastForward);
		EnhancedInputComponent->BindAction(FastForwardAction, ETriggerEvent::Completed, this, &AFebriculaCharacter::EndFastForward);
		EnhancedInputComponent->BindAction(FastForwardAction, ETriggerEvent::Canceled, this, &AFebriculaCharacter::EndFastForward);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFebriculaCharacter::Look);

		//	EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this , &AFebriculaCharacter::Interact);

	}
	else
	{
		UE_LOG(LogFebricula, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AFebriculaCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AFebriculaCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

float AFebriculaCharacter::GetEffectiveWalkSpeed() const
{
	if (LevelWalkSpeedOverrides.Num() > 0)
	{
		const FString Current = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true);
		if (const float* Found = LevelWalkSpeedOverrides.Find(FName(*Current)))
		{
			return *Found;
		}
	}
	return WalkSpeed;
}

bool AFebriculaCharacter::IsSprintAllowed() const
{
	if (SprintBlockedLevels.Num() == 0)
	{
		return true;
	}

	// PIE 접두사(UEDPIE_0_ 등)를 떼어내야 레벨 이름 비교가 정확하다.
	const FString Current = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true);
	return !SprintBlockedLevels.Contains(FName(*Current));
}

void AFebriculaCharacter::StartSprint(const FInputActionValue& Value)
{
	// 입력 바인딩은 그대로 두고 동작만 막는다.
	if (!IsSprintAllowed())
	{
		return;
	}
	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void AFebriculaCharacter::EndSprint(const FInputActionValue& Value)
{
	// WalkSpeed가 아니라 GetEffectiveWalkSpeed()로 되돌려야
	// 레벨별 오버라이드가 스프린트 종료 후에도 유지된다.
	GetCharacterMovement()->MaxWalkSpeed = GetEffectiveWalkSpeed();
}

void AFebriculaCharacter::StartFastForward(const FInputActionValue& Value)
{
	if (UWorld* World = GetWorld())
	{
		if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
		{
			Dialogue->SetFastForward(true);
		}
	}
}

void AFebriculaCharacter::EndFastForward(const FInputActionValue& Value)
{
	if (UWorld* World = GetWorld())
	{
		if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
		{
			Dialogue->SetFastForward(false);
		}
	}
}

void AFebriculaCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AFebriculaCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AFebriculaCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AFebriculaCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
