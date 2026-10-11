// [skydodo] 2026-08-12 작성 (최종 수정 2026-08-19)
// 요약: 안내 BT 태스크 구현 - 플레이어가 다가올 때까지 대기
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
		UE_LOG(LogTemp, Warning, TEXT("[Wait] Npc=%s Player=%s"),
			*GetNameSafe(NpcPawn), *GetNameSafe(PlayerPawn));
		return false;
	}

	const float DistSq = FVector::DistSquared2D(NpcPawn->GetActorLocation(), PlayerPawn->GetActorLocation());
	const bool bNear = DistSq <= FMath::Square(CatchUpRadius);

	UE_LOG(LogTemp, Warning, TEXT("[Wait] 거리=%.1f 기준=%.1f 통과=%d"),
		FMath::Sqrt(DistSq), CatchUpRadius, bNear);
	
	return DistSq <= FMath::Square(CatchUpRadius);
}
