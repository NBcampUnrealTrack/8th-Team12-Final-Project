// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "NPC/NpcTypes.h"
#include "MainNpcAIController.generated.h"

class ANpcPatrolRoute;
class UBlackboardComponent;

/**
 * 메인 NPC 전용 AIController. Idle / Patrol / Chase 세 가지 자율 행동만 다룬다.
 *
 * 상태 전환은 이 컨트롤러가 스스로 판단하지 않는다 - 게임플레이 코드(랜덤 인카운터 트리거,
 * 이벤트 트리 등)가 SetAIState / StartPatrol / StartChase를 호출해서 일으킨다.
 * (시야/감지로 자동 전환하는 퍼셉션 시스템은 지금 범위 밖. 필요해지면 여기에 얹으면 된다.)
 *
 * ---- BehaviorTree 쪽 구성 (에디터에서 직접 만들어야 함) ----
 * Root
 *  Selector
 *   [Blackboard: AIState == Chase]  -> Move To (Target Key = ChaseTarget)
 *   [Blackboard: AIState == Patrol] -> Sequence
 *                                       - Move To (Target Key = PatrolTargetLocation)   ; A지점까지 이동
 *                                       - BTTask_WaitForPlayerNearby                    ; 플레이어가 다시 다가올 때까지 대기
 *                                       - BTTask_AdvancePatrolTarget                    ; 다음 지점(B) 계산 후 갱신
 *   [Default]                        -> Wait (원하는 Idle 대기시간)
 * (Selector는 Sequence가 끝나면 매 틱 다시 위에서부터 평가하므로, 위 3단 Sequence가 끝나면
 *  자동으로 처음(Move To)부터 다시 돌아 다음 지점으로 이동한다 - 별도 Loop 데코레이터 불필요)
 *
 * ---- Blackboard 키 (이름이 아래 static FName들과 정확히 같아야 함) ----
 *   AIState (Enum, ENpcAIState) / PatrolRoute (Object, ANpcPatrolRoute)
 *   PatrolIndex (Int) / PatrolTargetLocation (Vector) / ChaseTarget (Object, Actor)
 */
UCLASS()
class FEBRICULA_API AMainNpcAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMainNpcAIController();

	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(BlueprintPure, Category = "NPC|AI")
	ENpcAIState GetAIState() const { return CurrentAIState; }

	/** 최상위 AI 상태만 갈아끼우고 싶을 때 (Patrol/Chase 대상 세팅 없이) */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void SetAIState(ENpcAIState NewState);

	/** Patrol 상태로 전환하고 순찰 경로를 지정 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StartPatrol(ANpcPatrolRoute* Route);

	/** Idle로 복귀 (순찰 중단) */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StopPatrol();

	/** Chase 상태로 전환하고 추적 대상 지정 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StartChase(AActor* TargetToChase);

	/** Idle로 복귀 (추적 중단) */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StopChase();

	// ---- Blackboard 키 이름 (Blackboard 애셋에서 만드는 키 이름과 반드시 일치해야 함) ----
	static const FName BBKey_AIState;
	static const FName BBKey_PatrolRoute;
	static const FName BBKey_PatrolIndex;
	static const FName BBKey_PatrolTargetLocation;
	static const FName BBKey_ChaseTarget;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "NPC|AI")
	ENpcAIState CurrentAIState = ENpcAIState::Idle;
};
