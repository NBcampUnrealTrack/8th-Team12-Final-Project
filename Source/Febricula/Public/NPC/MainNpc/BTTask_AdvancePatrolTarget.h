// Fill out your copyright notice in the Description page of Project Settings.
//
// 순찰 시퀀스에서 "다음 웨이포인트로 넘어가기"만 담당하는 즉시완료(instant) Task.
// PatrolRoute/PatrolIndex를 보고 다음 지점을 계산해 PatrolIndex/PatrolTargetLocation을 갱신한 뒤 바로 끝난다.
// 보통 [Move To] -> [Wait For Player Nearby] -> [Advance Patrol Target] 순서의 마지막 자리에 놓는다.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_AdvancePatrolTarget.generated.h"

UCLASS()
class FEBRICULA_API UBTTask_AdvancePatrolTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_AdvancePatrolTarget();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolRouteKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolIndexKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolTargetLocationKey;
};