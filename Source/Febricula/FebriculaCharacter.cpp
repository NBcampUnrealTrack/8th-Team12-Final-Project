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
#include "Febricula.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Interaction/InteractableProp.h"
#include "Interaction/InteractionComponent.h"
#include "Public/Level/DoorMapActor.h"

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
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
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
	
	InteractionCollider = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollider"));
	InteractionCollider->SetupAttachment(RootComponent);
	
	InteractionCollider->OnComponentBeginOverlap.AddDynamic(this, &AFebriculaCharacter::OnComponentBeginOverlap);
	InteractionCollider->OnComponentEndOverlap.AddDynamic(this, &AFebriculaCharacter::OnComponentEndOverlap);
	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
	
	InteractionAngle = 78.f;
	InteractionCos = .2f;
	
}

void AFebriculaCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	InteractionCos = FMath::Cos(FMath::DegreesToRadians(InteractionAngle));
	
}

#if  WITH_EDITOR
void AFebriculaCharacter::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	InteractionCos = FMath::Cos(FMath::DegreesToRadians(InteractionCos));
}
#endif

AInteractableProp* AFebriculaCharacter::GetBestInteractable()
{
	const FVector PlayerLocation = GetActorLocation();
	const FVector Forward = GetControlRotation().Vector();

	AInteractableProp* BestInteractable = nullptr;
	float BestScore = -1.f;
	UE_LOG(LogTemp, Warning, TEXT("GetBestInteractable()"))
	for (const TObjectPtr<AInteractableProp>& Interactable : OverlappedActors)
	{
		if (!IsValid(Interactable)) continue;
		if (!Interactable->Execute_CanInteract(Interactable, this)) continue;
		FVector Direction = Interactable->GetActorLocation() - PlayerLocation;
		const float Distance = Direction.Size();
		Direction = Direction.GetSafeNormal();
		
		if (Direction.IsNearlyZero()) continue;
		const float Dot = FVector::DotProduct(Forward, Direction);
		if (Dot < InteractionCos) continue;
		
		const float Score = Dot * (1.f - Distance / InteractionCollider->GetScaledSphereRadius());
		if (Score > BestScore)
		{
			BestScore = Score;
			BestInteractable = Interactable;
		}
	}	
	return BestInteractable;
}

void AFebriculaCharacter::Interact(const FInputActionValue& InputActionValue)
{
	
	AInteractableProp* InteractableActor = GetBestInteractable();
	if (!IsValid(InteractableActor)) return;
	InteractableActor->Execute_Interact(InteractableActor, this);
	
	// TArray<FHitResult> Result;
	// FCollisionQueryParams CollisionParams;
	// CollisionParams.AddIgnoredActor(this);
	// GetWorld()->LineTraceMultiByChannel
	// (
	// 	Result,
	// 	GetActorLocation(),
	// 	GetActorLocation() + GetActorForwardVector() * 1000.f,
	// 	ECollisionChannel::ECC_Visibility,
	// 	CollisionParams
	// );
	// DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + GetActorForwardVector() * 100.f, FColor::Green);
	// for (FHitResult HitResult : Result)
	// {
	// 	if (AActor* Hit = HitResult.GetActor())
	// 	{
	// 		UE_LOG(LogTemp, Warning, TEXT("SomethingFind!"));
	//
	// 		if (AMovableMapActor* Openable = Cast<AMovableMapActor>(Hit))
	// 		{
	// 			Openable->StartMove();
	// 			break;
	// 		}
	// 	}
	// }
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

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFebriculaCharacter::Look);
		
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this , &AFebriculaCharacter::Interact);
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
