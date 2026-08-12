#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdatePatrolTarget.generated.h"

UCLASS()
class FEBRICULA_API UBTService_UpdatePatrolTarget : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdatePatrolTarget();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	// playable character 도착 판정 기준 거리
	UPROPERTY(EditAnywhere, Category = "NPC|Patrol")
	float ArrivalRadius = 80.f;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolRouteKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolIndexKey;

	UPROPERTY(EditAnywhere, Category = "NPC|Blackboard")
	FBlackboardKeySelector PatrolTargetLocationKey;
};