// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/BTTask_WaitForPlayerNearby.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UBTTask_WaitForPlayerNearby::UBTTask_WaitForPlayerNearby()
{
	NodeName = TEXT("Wait For Player Nearby");

	// TickTask가 호출되도록 켜준다 (기본값은 꺼져 있음)
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_WaitForPlayerNearby::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 시작하자마자 이미 플레이어가 근처에 있을 수도 있으니 첫 틱을 기다리지 않고 바로 확인한다.
	return IsPlayerNearby(OwnerComp) ? EBTNodeResult::Succeeded : EBTNodeResult::InProgress;
}

void UBTTask_WaitForPlayerNearby::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (IsPlayerNearby(OwnerComp))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

bool UBTTask_WaitForPlayerNearby::IsPlayerNearby(UBehaviorTreeComponent& OwnerComp) const
{
	const AAIController* AIController = OwnerComp.GetAIOwner();
	const APawn* NpcPawn = AIController ? AIController->GetPawn() : nullptr;
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(OwnerComp.GetWorld(), PlayerIndex);

	if (!NpcPawn || !PlayerPawn)
	{
		return false;
	}

	const float DistSq = FVector::DistSquared2D(NpcPawn->GetActorLocation(), PlayerPawn->GetActorLocation());
	return DistSq <= FMath::Square(CatchUpRadius);
}