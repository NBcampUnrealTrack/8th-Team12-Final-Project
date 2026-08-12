// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/BaseNpcCharacter.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ABaseNpcCharacter::ABaseNpcCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.f, 540.f, 0.f);
	}

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void ABaseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	SetMovementState(ENpcMovementState::Idle);
}

void ABaseNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABaseNpcCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ABaseNpcCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AAIController* AIController = Cast<AAIController>(NewController))
	{
		if (BehaviorTreeAsset)
		{
			AIController->RunBehaviorTree(BehaviorTreeAsset);
		}
		PushMovementStateToBlackboard(CurrentMovementState);
	}
}

// ---------------------------------------------------------------------------
// IInteractable
// ---------------------------------------------------------------------------

bool ABaseNpcCharacter::CanInteract_Implementation(AActor* Interactor) const
{
	return CurrentMovementState != ENpcMovementState::Interacting;
}

void ABaseNpcCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	OnNpcInteracted.Broadcast(Interactor);
	
	OnInteracted(Interactor);
}

FText ABaseNpcCharacter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("Npc", "InteractPrompt", "상호작용");
}

// ---------------------------------------------------------------------------
// 공통 이동
// ---------------------------------------------------------------------------

void ABaseNpcCharacter::SetMovementState(ENpcMovementState NewState)
{
	if (CurrentMovementState == NewState)
	{
		return;
	}

	const ENpcMovementState OldState = CurrentMovementState;
	CurrentMovementState = NewState;

	PushMovementStateToBlackboard(NewState);
	OnNpcMovementStateChanged.Broadcast(OldState, NewState);
}

void ABaseNpcCharacter::MoveToLocation(const FVector& Destination, float AcceptanceRadius)
{
	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->MoveToLocation(Destination, AcceptanceRadius);
	}
	SetMovementState(ENpcMovementState::Walk);
}

void ABaseNpcCharacter::MoveToTargetActor(AActor* TargetActor, float AcceptanceRadius)
{
	if (!TargetActor)
	{
		return;
	}

	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->MoveToActor(TargetActor, AcceptanceRadius);
	}
	SetMovementState(ENpcMovementState::Walk);
}

void ABaseNpcCharacter::StopMovementAndIdle()
{
	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->StopMovement();
	}
	SetMovementState(ENpcMovementState::Idle);
}

AAIController* ABaseNpcCharacter::GetNpcAIController() const
{
	return Cast<AAIController>(GetController());
}

void ABaseNpcCharacter::PushMovementStateToBlackboard(ENpcMovementState State) const
{
	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			Blackboard->SetValueAsEnum(MovementStateBlackboardKey, static_cast<uint8>(State));
		}
	}
}