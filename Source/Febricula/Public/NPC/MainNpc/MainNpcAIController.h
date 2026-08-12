// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "NPC/NpcTypes.h"
#include "MainNpcAIController.generated.h"

class ANpcPatrolRoute;
class UBlackboardComponent;

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

	/** Idle로 복귀 */
	UFUNCTION(BlueprintCallable, Category = "NPC|AI")
	void StopChase();

	// ---- Blackboard 키 이름 ----
	static const FName BBKey_AIState;
	static const FName BBKey_PatrolRoute;
	static const FName BBKey_PatrolIndex;
	static const FName BBKey_PatrolTargetLocation;
	static const FName BBKey_ChaseTarget;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "NPC|AI")
	ENpcAIState CurrentAIState = ENpcAIState::Idle;
};