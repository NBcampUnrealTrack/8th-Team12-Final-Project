// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/BTTask_AdvanceRouteTarget.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "NPC/MainNpc/NpcRoute.h"

UBTTask_AdvanceRouteTarget::UBTTask_AdvanceRouteTarget()
{
	NodeName = TEXT("Advance Route Target");

	RouteKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvanceRouteTarget, RouteKey), ANpcRoute::StaticClass());
	RouteIndexKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvanceRouteTarget, RouteIndexKey));
	RouteTargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_AdvanceRouteTarget, RouteTargetLocationKey));
}

EBTNodeResult::Type UBTTask_AdvanceRouteTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	UBlackboardComponent* BBComp = OwnerComp.GetBlackboardComponent();
	if (!BBComp)
	{
		return EBTNodeResult::Failed;
	}

	ANpcRoute* Route = Cast<ANpcRoute>(BBComp->GetValueAsObject(RouteKey.SelectedKeyName));
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return EBTNodeResult::Failed;
	}

	const int32 CurrentIndex = BBComp->GetValueAsInt(RouteIndexKey.SelectedKeyName);
	const int32 NextIndex = Route->GetNextIndex(CurrentIndex);

	BBComp->SetValueAsInt(RouteIndexKey.SelectedKeyName, NextIndex);
	BBComp->SetValueAsVector(RouteTargetLocationKey.SelectedKeyName, Route->GetPointLocation(NextIndex));

	return EBTNodeResult::Succeeded;
}
