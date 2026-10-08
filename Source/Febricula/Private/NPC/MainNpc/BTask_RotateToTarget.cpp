// Fill out your copyright notice in the Description page of Project Settings.


#include "NPC/MainNpc/BTask_RotateToTarget.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTask_RotateToTarget::UBTask_RotateToTarget()
{
	NodeName = TEXT("RotateToTarget");
	RouteTargetRotatorKey.AddRotatorFilter(this, GET_MEMBER_NAME_CHECKED(UBTask_RotateToTarget, RouteTargetRotatorKey));
}

void UBTask_RotateToTarget::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (const UBlackboardData* BlackboardData = GetBlackboardAsset())
	{
		RouteTargetRotatorKey.ResolveSelectedKey(*BlackboardData);
	}
}

EBTNodeResult::Type UBTask_RotateToTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (APawn* NowNpc = OwnerComp.GetAIOwner()->GetPawn())
	{
		if (UBlackboardComponent* BlackboardComponent = OwnerComp.GetBlackboardComponent())
		{
			const float Yaw = BlackboardComponent->GetValueAsRotator(RouteTargetRotatorKey.SelectedKeyName).Yaw;
			//UE_LOG(LogTemp, Warning, TEXT("[RotateToTarget] Key=%s, Yaw=%.1f, 현재 Yaw=%.1f"), *RouteTargetRotatorKey.SelectedKeyName.ToString(), Yaw, NowNpc->GetActorRotation().Yaw);
			NowNpc->SetActorRotation(FRotator(0.f, Yaw, 0.f));
			return EBTNodeResult::Succeeded;
		}
	}
	return EBTNodeResult::Failed;
}
