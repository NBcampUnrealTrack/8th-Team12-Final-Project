// [skydodo] 2026-08-12 작성 (최종 수정 2026-08-19)
// 요약: 안내(Guide) 전용 BT 태스크 - 플레이어가 근처에 올 때까지 대기
// Fill out your copyright notice in the Description page of Project Settings.
//
// 안내(Guide) 브랜치 전용 노드.
// 경로 지점에 도착한 뒤, 플레이어(플레이어블 캐릭터)가 다시 근처로 다가올 때까지
// NPC를 그 자리에 세워두는 Latent Task. CatchUpRadius 안에 플레이어가 들어오면 성공 처리되어
// 다음 노드(보통 BTTask_AdvanceRouteTarget)로 넘어간다.
//
// 순찰(Patrol) 브랜치에는 이 노드를 넣지 않는다. 순찰은 플레이어를 기다리지 않는 게 정의이므로,
// 대신 Wait(1~2초)를 넣거나 아무것도 넣지 않으면 된다.

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
