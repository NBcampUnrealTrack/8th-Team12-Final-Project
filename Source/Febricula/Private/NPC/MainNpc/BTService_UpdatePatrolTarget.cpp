#include "NPC/MainNpc/BTService_UpdatePatrolTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "NPC/MainNpc/NpcPatrolRoute.h"

UBTService_UpdatePatrolTarget::UBTService_UpdatePatrolTarget()
{
	NodeName = TEXT("Update Patrol Target");
	Interval = 0.25f;
	RandomDeviation = 0.05f;

	// Blackboard 키 선택기에 타입 필터를 걸어둔다 (에디터 드롭다운에서 해당 타입 키만 고를 수 있게).
	PatrolRouteKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdatePatrolTarget, PatrolRouteKey), ANpcPatrolRoute::StaticClass());
	PatrolIndexKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdatePatrolTarget, PatrolIndexKey));
	PatrolTargetLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdatePatrolTarget, PatrolTargetLocationKey));
}

void UBTService_UpdatePatrolTarget::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!Blackboard || !AIController || !AIController->GetPawn())
	{
		return;
	}

	ANpcPatrolRoute* Route = Cast<ANpcPatrolRoute>(Blackboard->GetValueAsObject(PatrolRouteKey.SelectedKeyName));
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return;
	}

	const int32 CurrentIndex = Blackboard->GetValueAsInt(PatrolIndexKey.SelectedKeyName);
	const FVector CurrentTarget = Blackboard->GetValueAsVector(PatrolTargetLocationKey.SelectedKeyName);
	const FVector PawnLocation = AIController->GetPawn()->GetActorLocation();

	if (FVector::DistSquared(PawnLocation, CurrentTarget) <= FMath::Square(ArrivalRadius))
	{
		const int32 NextIndex = Route->GetNextIndex(CurrentIndex);
		Blackboard->SetValueAsInt(PatrolIndexKey.SelectedKeyName, NextIndex);
		Blackboard->SetValueAsVector(PatrolTargetLocationKey.SelectedKeyName, Route->GetPointLocation(NextIndex));
	}
}