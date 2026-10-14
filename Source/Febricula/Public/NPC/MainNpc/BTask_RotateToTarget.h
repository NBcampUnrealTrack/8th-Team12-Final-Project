// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTask_RotateToTarget.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API UBTask_RotateToTarget : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTask_RotateToTarget();
	
	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector RouteTargetRotatorKey;
	
	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector bAutoRotateKey;
	
protected:
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

};
