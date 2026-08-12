// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/BTTask_AdvancePatrolTarget.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "NPC/MainNpc/NpcPatrolRoute.h"

UBTTask_AdvancePatrolTarget::UBTTask_AdvancePatrolTarget()
{
	NodeName = TEXT("Advance Patrol Target");

	PatrolRouteKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvancePatrolTarget, PatrolRouteKey), ANpcPatrolRoute::StaticClass());
	PatrolIndexKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvancePatrolTarget, PatrolIndexKey));
	PatrolTargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvancePatrolTarget, PatrolTargetLocationKey));
}

EBTNodeResult::Type UBTTask_AdvancePatrolTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		return EBTNodeResult::Failed;
	}

	ANpcPatrolRoute* Route = Cast<ANpcPatrolRoute>(Blackboard->GetValueAsObject(PatrolRouteKey.SelectedKeyName));
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return EBTNodeResult::Failed;
	}

	const int32 CurrentIndex = Blackboard->GetValueAsInt(PatrolIndexKey.SelectedKeyName);
	const int32 NextIndex = Route->GetNextIndex(CurrentIndex);

	Blackboard->SetValueAsInt(PatrolIndexKey.SelectedKeyName, NextIndex);
	Blackboard->SetValueAsVector(PatrolTargetLocationKey.SelectedKeyName, Route->GetPointLocation(NextIndex));

	return EBTNodeResult::Succeeded;
}