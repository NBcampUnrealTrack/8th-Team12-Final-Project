// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "NPC/MainNpc/NpcPatrolRoute.h"

const FName AMainNpcAIController::BBKey_AIState(TEXT("AIState"));
const FName AMainNpcAIController::BBKey_PatrolRoute(TEXT("PatrolRoute"));
const FName AMainNpcAIController::BBKey_PatrolIndex(TEXT("PatrolIndex"));
const FName AMainNpcAIController::BBKey_PatrolTargetLocation(TEXT("PatrolTargetLocation"));
const FName AMainNpcAIController::BBKey_ChaseTarget(TEXT("ChaseTarget"));

AMainNpcAIController::AMainNpcAIController()
{
}

void AMainNpcAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		CurrentAIState = static_cast<ENpcAIState>(BBComp->GetValueAsEnum(BBKey_AIState));
	}
}

void AMainNpcAIController::SetAIState(ENpcAIState NewState)
{
	CurrentAIState = NewState;

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsEnum(BBKey_AIState, static_cast<uint8>(NewState));
	}
}

void AMainNpcAIController::StartPatrol(ANpcPatrolRoute* Route)
{
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return;
	}

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsObject(BBKey_PatrolRoute, Route);
		BBComp->SetValueAsInt(BBKey_PatrolIndex, 0);
		BBComp->SetValueAsVector(BBKey_PatrolTargetLocation, Route->GetPointLocation(0));
	}

	SetAIState(ENpcAIState::Patrol);
}

void AMainNpcAIController::StopPatrol()
{
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->ClearValue(BBKey_PatrolRoute);
	}
	SetAIState(ENpcAIState::Idle);
}

void AMainNpcAIController::StartChase(AActor* TargetToChase)
{
	if (!TargetToChase)
	{
		return;
	}

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsObject(BBKey_ChaseTarget, TargetToChase);
	}

	SetAIState(ENpcAIState::Chase);
}

void AMainNpcAIController::StopChase()
{
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->ClearValue(BBKey_ChaseTarget);
	}
	SetAIState(ENpcAIState::Idle);
}