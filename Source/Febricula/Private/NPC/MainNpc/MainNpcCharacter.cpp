// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcCharacter.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/NpcPatrolRoute.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"

AMainNpcCharacter::AMainNpcCharacter()
{
	AIControllerClass = AMainNpcAIController::StaticClass();
}

void AMainNpcCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	if (!bAutoStartPatrol || !DefaultPatrolRoute)
	{
		return;
	}

	if (AMainNpcAIController* NpcAIController = Cast<AMainNpcAIController>(GetController()))
	{
		NpcAIController->StartPatrol(DefaultPatrolRoute);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: AMainNpcAIController를 찾지 못해 자동 순찰을 시작하지 못했습니다. "
				 "AI Controller Class가 MainNpcAIController인지, Auto Possess AI 설정이 켜져 있는지 확인하세요."),
			*GetNameSafe(this));
	}
}

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

	// 동행은 게임플레이가 직접 지시하는 이동이므로, Idle/Patrol/Chase BT가 끼어들어
	// 목적지를 덮어쓰지 못하도록 잠깐 멈춘다.
	SetBehaviorTreePaused(true);

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
	SetBehaviorTreePaused(false);
}

// ---------------------------------------------------------------------------
// 대화
// ---------------------------------------------------------------------------

void AMainNpcCharacter::StartTalk(AActor* Talker)
{
	StopMovementAndIdle();
	SetBehaviorTreePaused(true);
	CurrentTalker = Talker;
	SetMovementState(ENpcMovementState::Talk);
	OnNpcTalkStateChanged.Broadcast(true);

	// 대화 상대를 바라보도록 회전 (스냅. 부드럽게 돌리고 싶으면 여기 대신 Tick/타임라인에서 보간)
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
	SetBehaviorTreePaused(false);
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

// ---------------------------------------------------------------------------
// Behavior Tree 일시정지/재개 (Talk/Follow 스크립트 구간 보호용)
// ---------------------------------------------------------------------------

void AMainNpcCharacter::SetBehaviorTreePaused(bool bPaused) const
{
	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBrainComponent* Brain = AIController->GetBrainComponent())
		{
			if (bPaused)
			{
				Brain->PauseLogic(TEXT("NPC scripted event (Talk/Follow)"));
			}
			else
			{
				Brain->ResumeLogic(TEXT("NPC scripted event (Talk/Follow)"));
			}
		}
	}
}