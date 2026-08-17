// Fill out your copyright notice in the Description page of Project Settings.
//
// 경로의 "다음 지점으로 넘어가기"만 담당하는 즉시완료(instant) Task.
// Route/RouteIndex를 보고 다음 지점을 계산해 RouteIndex/RouteTargetLocation을 갱신한 뒤 바로 끝난다.
//
// 순찰(Patrol)과 안내(Guide) 양쪽 브랜치에서 공통으로 쓴다:
//   Patrol : [Move To] -> [Wait] -> [Advance Route Target]
//   Guide  : [Move To] -> [Wait For Player Nearby] -> [Advance Route Target]

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_AdvanceRouteTarget.generated.h"

UCLASS()
class FEBRICULA_API UBTTask_AdvanceRouteTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_AdvanceRouteTarget();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector RouteKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector RouteIndexKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector RouteTargetLocationKey;
};
