// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "NPC/MainNpc/MainNpcCharacter.h"
#include "NPC/MainNpc/NpcRoute.h"

const FName AMainNpcAIController::BBKey_AIState(TEXT("AIState"));
const FName AMainNpcAIController::BBKey_Route(TEXT("Route"));
const FName AMainNpcAIController::BBKey_RouteIndex(TEXT("RouteIndex"));
const FName AMainNpcAIController::BBKey_RouteTargetLocation(TEXT("RouteTargetLocation"));
const FName AMainNpcAIController::BBKey_RouteTargetRotator(TEXT("RouteTargetRotator"));

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

	// 여기서 무조건 Idle로 덮어쓰면 Blackboard 애셋에 지정해둔 AIState 기본값이 매번 날아가서
	// 테스트가 불가능해진다. 그래서 이미 들어있는 값을 읽어 CurrentAIState만 동기화한다.
	// (AMainNpcCharacter::BeginPlay의 자동 시작은 이 뒤에 실행되므로 그쪽이 우선한다.)
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

bool AMainNpcAIController::SetupRoute(ANpcRoute* Route)
{
	if (!Route || Route->GetNumPoints() <= 0)
	{
		return false;
	}

	UBlackboardComponent* BBComp = GetBlackboardComponent();
	if (!BBComp)
	{
		return false;
	}

	BBComp->SetValueAsObject(BBKey_Route, Route);
	BBComp->SetValueAsInt(BBKey_RouteIndex, 0);
	BBComp->SetValueAsVector(BBKey_RouteTargetLocation, Route->GetPointLocation(0));
	BBComp->SetValueAsRotator(BBKey_RouteTargetRotator, Route->GetPointRotation(0));
	return true;
}

void AMainNpcAIController::StartPatrol(ANpcRoute* Route)
{
	if (SetupRoute(Route))
	{
		if (ACharacter* NowCharacter = Cast<ACharacter>(GetPawn()))
		{
			if (UCharacterMovementComponent* Movement = NowCharacter->GetCharacterMovement())
			{
				Movement->bOrientRotationToMovement = true;
			}
		}

		SetAIState(ENpcAIState::Patrol);
	}
}

void AMainNpcAIController::StartGuide(ANpcRoute* Route)
{
	if (SetupRoute(Route))
	{
		if (ACharacter* NowCharacter = Cast<ACharacter>(GetPawn()))
		{
			if (UCharacterMovementComponent* Movement = NowCharacter->GetCharacterMovement())
			{
				Movement->bOrientRotationToMovement = true;
			}
		}
		const bool bOk = SetupRoute(Route);

		if (bOk)
		{
			SetAIState(ENpcAIState::Guide);
		}
	}
}

void AMainNpcAIController::StopRouteMovement()
{
	if (UBlackboardComponent* BBComp = GetBlackboardComponent())
	{
		BBComp->ClearValue(BBKey_Route);
	}
	StopMovement();
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

bool AMainNpcAIController::HasReachedRouteTarget(float AcceptanceRadius) const
{
	const APawn* MyPawn = GetPawn();
	const UBlackboardComponent* BBComp = GetBlackboardComponent();
	if (!MyPawn || !BBComp)
	{
		return false;
	}

	// 경로가 아예 없으면 "도착"을 논할 수 없다.
	if (!BBComp->GetValueAsObject(BBKey_Route))
	{
		return false;
	}

	const FVector Target = BBComp->GetValueAsVector(BBKey_RouteTargetLocation);
	// 높이 차이(계단, 캡슐 오프셋)는 무시하고 수평 거리로만 판정한다.
	return FVector::DistSquared2D(MyPawn->GetActorLocation(), Target) <= FMath::Square(AcceptanceRadius);
}