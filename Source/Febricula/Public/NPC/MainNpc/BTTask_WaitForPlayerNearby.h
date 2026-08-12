// Fill out your copyright notice in the Description page of Project Settings.
//
// 순찰 지점에 도착한 뒤, 플레이어(플레이어블 캐릭터)가 다시 근처로 다가올 때까지
// NPC를 그 자리에 세워두는 Latent Task. CatchUpRadius 안에 플레이어가 들어오면 성공 처리되어
// 다음 노드(보통 BTTask_AdvancePatrolTarget)로 넘어간다.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_WaitForPlayerNearby.generated.h"

UCLASS()
class FEBRICULA_API UBTTask_WaitForPlayerNearby : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_WaitForPlayerNearby();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	bool IsPlayerNearby(UBehaviorTreeComponent& OwnerComp) const;

	/** 이 거리(수평 기준) 안에 플레이어가 들어오면 "따라잡았다"고 판단하고 대기를 끝낸다 */
	UPROPERTY(EditAnywhere, Category = "NPC|Patrol")
	float CatchUpRadius = 250.f;

	/** 기준으로 삼을 로컬 플레이어 인덱스. 싱글플레이면 0으로 두면 된다 */
	UPROPERTY(EditAnywhere, Category = "NPC|Patrol")
	int32 PlayerIndex = 0;
};