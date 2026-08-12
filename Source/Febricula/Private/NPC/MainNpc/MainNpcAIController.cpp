// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "NPC/MainNpc/NpcPatrolRoute.h"

const FName AMainNpcAIController::BBKey_AIState(TEXT("AIState"));
const FName AMainNpcAIController::BBKey_PatrolRoute(TEXT("PatrolRoute"));
const FName AMainNpcAIController::BBKey_PatrolIndex(TEXT("PatrolIndex"));
const FName AMainNpcAIController::BBKey_PatrolTargetLocation(TEXT("PatrolTargetLocation"));
const FName AMainNpcAIController::BBKey_ChaseTarget(TEXT("ChaseTarget"));

AMainNpcAIController::AMainNpcAIController()
{
}

void AMainNpcAIController::OnPossess(APawn* InPawn)
{
	// Super::OnPossess 안에서 InPawn->PossessedBy(this)가 먼저 실행된다.
	// ABaseNpcCharacter::PossessedBy가 여기서 RunBehaviorTree까지 이미 끝내놓기 때문에,
	// 이 지점에서는 Blackboard가 이미 준비된 상태다.
	Super::OnPossess(InPawn);

	// 예전엔 여기서 무조건 SetAIState(Idle)로 덮어썼는데, 그러면 Blackboard 애셋에서
	// AIState 기본값을 Patrol로 지정해둔 세팅까지 매번 지워버려서 테스트가 불가능했다.
	// 이제는 Blackboard에 이미 들어있는 값을 읽어 CurrentAIState만 동기화한다.
	// (AMainNpcCharacter::BeginPlay의 자동 순찰은 이 뒤에 실행되므로 그쪽이 우선한다.)
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		CurrentAIState = static_cast<ENpcAIState>(BBComp->GetValueAsEnum(BBKey_AIState));
	}
}

void AMainNpcAIController::SetAIState(ENpcAIState NewState)
{
	CurrentAIState = NewState;

	// AAIController가 이미 protected 멤버 "Blackboard"를 갖고 있어서 지역변수 이름을
	// 그대로 "Blackboard"로 쓰면 섀도잉(C4458)이 나기 때문에 BBComp로 받는다.
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsEnum(BBKey_AIState, static_cast<uint8>(NewState));
	}
}

void AMainNpcAIController::StartPatrol(ANpcPatrolRoute* Route)
{
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return;
	}

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsObject(BBKey_PatrolRoute, Route);
		BBComp->SetValueAsInt(BBKey_PatrolIndex, 0);
		BBComp->SetValueAsVector(BBKey_PatrolTargetLocation, Route->GetPointLocation(0));
	}

	SetAIState(ENpcAIState::Patrol);
}

void AMainNpcAIController::StopPatrol()
{
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->ClearValue(BBKey_PatrolRoute);
	}
	SetAIState(ENpcAIState::Idle);
}

void AMainNpcAIController::StartChase(AActor* TargetToChase)
{
	if (!TargetToChase)
	{
		return;
	}

	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->SetValueAsObject(BBKey_ChaseTarget, TargetToChase);
	}

	SetAIState(ENpcAIState::Chase);
}

void AMainNpcAIController::StopChase()
{
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->ClearValue(BBKey_ChaseTarget);
	}
	SetAIState(ENpcAIState::Idle);
}