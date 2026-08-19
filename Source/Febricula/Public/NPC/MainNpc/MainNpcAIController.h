// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "NPC/NpcTypes.h"
#include "MainNpcAIController.generated.h"

class ANpcRoute;
class UBlackboardComponent;

/**
 * 메인 NPC 전용 AIController. Idle / Patrol / Guide / Chase 네 가지 자율 행동을 다룬다.
 *
 * Patrol과 Guide는 같은 ANpcRoute를 쓰지만 성격이 다르다:
 *   - Patrol : 지점 도착 즉시 다음 지점으로. 혼자 계속 돈다.
 *   - Guide  : 지점 도착 후 플레이어가 따라올 때까지 기다렸다가 다음 지점으로. 안내/동행 연출용.
 *
 * 상태 전환은 이 컨트롤러가 스스로 판단하지 않는다 - 게임플레이 코드(이벤트 트리의 액션,
 * 랜덤 인카운터 트리거 등)가 StartPatrol / StartGuide / StartChase를 호출해서 일으킨다.
 * (시야/감지로 자동 전환하는 퍼셉션 시스템은 지금 범위 밖. 필요해지면 여기에 얹으면 된다.)
 *
 * ---- BehaviorTree 쪽 구성 (에디터에서 직접 만들어야 함) ----
 * Root
 *  Selector
 *   [AIState == Chase]  -> Move To (ChaseTarget)
 *   [AIState == Guide]  -> Sequence
 *                            - Move To (RouteTargetLocation)
 *                            - BTTask_WaitForPlayerNearby     ; 플레이어 기다리기
 *                            - BTTask_AdvanceRouteTarget
 *   [AIState == Patrol] -> Sequence
 *                            - Move To (RouteTargetLocation)
 *                            - Wait (1~2초, 선택)             ; 기다리는 대상이 없다
 *                            - BTTask_AdvanceRouteTarget
 *   [Default]           -> Wait                               ; Idle
 *
 * ---- Blackboard 키 (이름이 아래 static FName들과 정확히 같아야 함) ----
 *   AIState (Enum, ENpcAIState) / Route (Object, ANpcRoute)
 *   RouteIndex (Int) / RouteTargetLocation (Vector) / ChaseTarget (Object, Actor)
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

	/** 최상위 AI 상태만 갈아끼운다 (경로/대상 세팅 없이) */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void SetAIState(ENpcAIState NewState);

	/** 혼자 계속 도는 순찰. 지점에 도착하면 바로 다음 지점으로 간다 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StartPatrol(ANpcRoute* Route);

	/** 플레이어를 데리고 다니는 안내. 지점마다 플레이어를 기다린다 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StartGuide(ANpcRoute* Route);

	/** 순찰/안내 공통 중단. Idle로 복귀한다 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StopRouteMovement();

	/** Chase 상태로 전환하고 추적 대상 지정 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StartChase(AActor* TargetToChase);

	/** Idle로 복귀 (추적 중단) */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StopChase();

	/**
	 * 현재 경로 목표 지점에 도착했는지 (수평 거리 기준).
	 * 대화 자동 진행(WhenNpcArrived)에서 "NPC가 다음 지점에 닿았나"를 판정하는 데 쓴다.
	 * 경로가 설정돼 있지 않으면 false.
	 */
	UFUNCTION(BlueprintPure, Category = "NPC|AI")
	bool HasReachedRouteTarget(float AcceptanceRadius = 150.f) const;

	// ---- Blackboard 키 이름 (Blackboard 애셋에서 만드는 키 이름과 반드시 일치해야 함) ----
	static const FName BBKey_AIState;
	static const FName BBKey_Route;
	static const FName BBKey_RouteIndex;
	static const FName BBKey_RouteTargetLocation;
	static const FName BBKey_ChaseTarget;

protected:
	/** StartPatrol/StartGuide가 공유하는 경로 세팅 부분 */
	bool SetupRoute(ANpcRoute* Route);

	UPROPERTY(BlueprintReadOnly, Category = "NPC|AI")
	ENpcAIState CurrentAIState = ENpcAIState::Idle;
};
