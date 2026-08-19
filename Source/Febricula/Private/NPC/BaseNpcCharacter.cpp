// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/BaseNpcCharacter.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ABaseNpcCharacter::ABaseNpcCharacter()
{
	// Set this character to call Tick() every frame. NPC는 대부분 상태 변화만으로 충분해서 꺼둔다.
	// 필요해지면 (예: 매 틱 시선 추적) 자식 클래스에서 true로 켜면 된다.
	PrimaryActorTick.bCanEverTick = false;

	// NPC는 기본적으로 이동 방향으로 몸을 돌린다 (플레이어 컨트롤러 회전을 따라가지 않음)
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.f, 540.f, 0.f);
	}

	// 레벨에 배치되거나 스폰되면 자동으로 AIController를 붙인다.
	// 프로젝트에 커스텀 AIController 클래스가 있다면 서브클래스/블루프린트에서 AIControllerClass를 지정할 것.
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

// Called when the game starts or when spawned
void ABaseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 시작 상태를 명시적으로 한 번 밀어준다 (애니메이션 BP / Blackboard 초기값 동기화)
	SetMovementState(ENpcMovementState::Idle);
}

// Called every frame
void ABaseNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
void ABaseNpcCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// NPC는 플레이어 입력을 받지 않으므로 기본적으로 바인딩할 것이 없다.
}

void ABaseNpcCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AAIController* AIController = Cast<AAIController>(NewController))
	{
		if (BehaviorTreeAsset)
		{
			AIController->RunBehaviorTree(BehaviorTreeAsset);
		}
		PushMovementStateToBlackboard(CurrentMovementState);
	}
}

// ---------------------------------------------------------------------------
// IInteractable
// ---------------------------------------------------------------------------

bool ABaseNpcCharacter::CanInteract_Implementation(AActor* Interactor) const
{
	// 상호작용 모션을 재생 중이면 중복 진입을 막는다.
	// 개별 이벤트의 전제조건/발생조건(호감도, 경과일 등)은 EventTreeManager 쪽에서 추가로 검사한다.
	return CurrentMovementState != ENpcMovementState::Interacting;
}

void ABaseNpcCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!CanInteract_Implementation(Interactor))
	{
		return;
	}

	OnNpcInteracted.Broadcast(Interactor);

	// 실제 이벤트 트리 분기는 이 훅을 구현하는 서브클래스/블루프린트, 혹은 여기서 호출하는
	// EventTreeManager 쪽에서 처리한다.
	OnInteracted(Interactor);
}

FText ABaseNpcCharacter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("Npc", "InteractPrompt", "상호작용");
}

// ---------------------------------------------------------------------------
// 공통 이동
// ---------------------------------------------------------------------------

void ABaseNpcCharacter::SetMovementState(ENpcMovementState NewState)
{
	if (CurrentMovementState == NewState)
	{
		return;
	}

	const ENpcMovementState OldState = CurrentMovementState;
	CurrentMovementState = NewState;

	PushMovementStateToBlackboard(NewState);
	OnNpcMovementStateChanged.Broadcast(OldState, NewState);
}

void ABaseNpcCharacter::MoveToLocation(const FVector& Destination, float AcceptanceRadius)
{
	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->MoveToLocation(Destination, AcceptanceRadius);
	}
	SetMovementState(ENpcMovementState::Walk);
}

void ABaseNpcCharacter::MoveToTargetActor(AActor* TargetActor, float AcceptanceRadius)
{
	if (!TargetActor)
	{
		return;
	}

	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->MoveToActor(TargetActor, AcceptanceRadius);
	}
	SetMovementState(ENpcMovementState::Walk);
}

void ABaseNpcCharacter::StopMovementAndIdle()
{
	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->StopMovement();
	}
	SetMovementState(ENpcMovementState::Idle);
}

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

AAIController* ABaseNpcCharacter::GetNpcAIController() const
{
	return Cast<AAIController>(GetController());
}

void ABaseNpcCharacter::PushMovementStateToBlackboard(ENpcMovementState State) const
{
	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			Blackboard->SetValueAsEnum(MovementStateBlackboardKey, static_cast<uint8>(State));
		}
	}
}
