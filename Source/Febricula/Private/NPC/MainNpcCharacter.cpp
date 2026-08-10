// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpcCharacter.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NPC/NpcRelationshipSubsystem.h"

FText AMainNpcCharacter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("Npc", "TalkPrompt", "대화하기");
}

// ---------------------------------------------------------------------------
// 추적/동행
// ---------------------------------------------------------------------------

void AMainNpcCharacter::StartFollowTarget(AActor* TargetToFollow)
{
	if (!TargetToFollow)
	{
		return;
	}

	CurrentFollowTarget = TargetToFollow;

	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			Blackboard->SetValueAsObject(FollowTargetBlackboardKey, TargetToFollow);
		}
	}

	SetMovementState(ENpcMovementState::Follow);
}

void AMainNpcCharacter::StopFollowTarget()
{
	CurrentFollowTarget = nullptr;

	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			Blackboard->ClearValue(FollowTargetBlackboardKey);
		}
		AIController->StopMovement();
	}

	SetMovementState(ENpcMovementState::Idle);
}

// ---------------------------------------------------------------------------
// 대화
// ---------------------------------------------------------------------------

void AMainNpcCharacter::StartTalk(AActor* Talker)
{
	StopMovementAndIdle();
	CurrentTalker = Talker;
	SetMovementState(ENpcMovementState::Talk);
	OnNpcTalkStateChanged.Broadcast(true);

	if (Talker)
	{
		const FVector ToTalker = (Talker->GetActorLocation() - GetActorLocation());
		if (!ToTalker.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, ToTalker.Rotation().Yaw, 0.f));
		}
	}

	OnTalkStarted(Talker);
}

void AMainNpcCharacter::EndTalk()
{
	CurrentTalker = nullptr;
	SetMovementState(ENpcMovementState::Idle);
	OnNpcTalkStateChanged.Broadcast(false);
	OnTalkEnded();
}

// ---------------------------------------------------------------------------
// 호감도 조회
// ---------------------------------------------------------------------------

bool AMainNpcCharacter::IsRelationshipAtLeast(ENpcRelationshipLevel MinimumLevel) const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->IsRelationshipAtLeast(GetNpcId(), MinimumLevel);
	}
	return false;
}

ENpcRelationshipLevel AMainNpcCharacter::GetRelationshipLevel() const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->GetRelationshipLevel(GetNpcId());
	}
	return ENpcRelationshipLevel::Neutral;
}

float AMainNpcCharacter::GetAffinityValue() const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->GetAffinityValue(GetNpcId());
	}
	return 0.f;
}

UNpcRelationshipSubsystem* AMainNpcCharacter::GetRelationshipSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UNpcRelationshipSubsystem>();
		}
	}
	return nullptr;
}
