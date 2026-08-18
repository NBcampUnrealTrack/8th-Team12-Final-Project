// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcCharacter.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"
#include "NPC/MainNpc/NpcRoute.h"

AMainNpcCharacter::AMainNpcCharacter()
{
	// Idle/Patrol/Chase Behavior Tree를 돌릴 전용 컨트롤러를 붙인다.
	AIControllerClass = AMainNpcAIController::StaticClass();
}

void AMainNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 레벨 디테일 패널에서 DefaultRoute와 AutoStartMode만 지정해두면 여기서 알아서 시작한다.
	// (AIController 빙의와 RunBehaviorTree는 PostInitializeComponents 단계에서 이미 끝나 있으므로
	//  BeginPlay 시점엔 Blackboard가 준비된 상태다.)
	if (AutoStartMode == ENpcAutoStartMode::None || !DefaultRoute)
	{
		return;
	}

	AMainNpcAIController* NpcAIController = Cast<AMainNpcAIController>(GetController());
	if (!NpcAIController)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: AMainNpcAIController를 찾지 못해 자동 시작에 실패했습니다. "
				 "AI Controller Class가 MainNpcAIController인지, Auto Possess AI 설정이 켜져 있는지 확인하세요."),
			*GetNameSafe(this));
		return;
	}

	switch (AutoStartMode)
	{
	case ENpcAutoStartMode::Patrol:
		NpcAIController->StartPatrol(DefaultRoute);
		break;

	case ENpcAutoStartMode::Guide:
		NpcAIController->StartGuide(DefaultRoute);
		break;

	default:
		break;
	}
}

FText AMainNpcCharacter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("Npc", "TalkPrompt", "E - 대화하기");
}

void AMainNpcCharacter::Interact_Implementation(AActor* Interactor)
{
	// 부모가 OnNpcInteracted 브로드캐스트 + OnInteracted 블루프린트 훅까지 처리한다.
	Super::Interact_Implementation(Interactor);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
	if (!Dialogue || Dialogue->IsDialogueActive())
	{
		return;
	}

	// 대화 시작 전에 먼저 멈춰 세우고 플레이어를 바라보게 한다.
	// (BT 일시정지도 StartTalk 안에서 처리되므로 대화 도중 순찰로 걸어가 버리지 않는다)
	StartTalk(Interactor);

	// 조건을 만족하는 이벤트를 찾아 시작. 없으면 기본 잡담으로 폴백.
	bool bStarted = Dialogue->TryStartEventForNpc(GetNpcId(), this);
	if (!bStarted && !DefaultIdleNodeId.IsNone())
	{
		bStarted = Dialogue->StartDialogueAtNode(DefaultIdleNodeId, this);
	}

	if (!bStarted)
	{
		// 할 말이 없으면 Talk 상태를 바로 풀어준다 (안 그러면 계속 멈춰 서 있게 된다).
		EndTalk();
	}
}

// ---------------------------------------------------------------------------
// 추적/동행
// ---------------------------------------------------------------------------

void AMainNpcCharacter::StartFollowTarget(AActor* TargetToFollow)
{
	if (!TargetToFollow)
	{
		return;
	}

	CurrentFollowTarget = TargetToFollow;

	// 동행은 게임플레이가 직접 지시하는 이동이므로, Idle/Patrol/Chase BT가 끼어들어
	// 목적지를 덮어쓰지 못하도록 잠깐 멈춘다.
	SetBehaviorTreePaused(true);

	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			// 매 틱 재계산은 BT 쪽 "Move To (Blackboard Object)" 노드가 담당한다.
			// 여긴 대상만 밀어주고, 실제 추적 갱신 로직은 BT 애셋 책임으로 둔다.
			Blackboard->SetValueAsObject(FollowTargetBlackboardKey, TargetToFollow);
		}
	}

	SetMovementState(ENpcMovementState::Follow);
}

void AMainNpcCharacter::StopFollowTarget()
{
	CurrentFollowTarget = nullptr;

	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent())
		{
			Blackboard->ClearValue(FollowTargetBlackboardKey);
		}
		AIController->StopMovement();
	}

	SetMovementState(ENpcMovementState::Idle);
	SetBehaviorTreePaused(false);
}

// ---------------------------------------------------------------------------
// 대화
// ---------------------------------------------------------------------------

void AMainNpcCharacter::StartTalk(AActor* Talker)
{
	StopMovementAndIdle();
	SetBehaviorTreePaused(true);
	CurrentTalker = Talker;
	SetMovementState(ENpcMovementState::Talk);
	OnNpcTalkStateChanged.Broadcast(true);

	// 대화 상대를 바라보도록 회전 (스냅. 부드럽게 돌리고 싶으면 여기 대신 Tick/타임라인에서 보간)
	if (Talker)
	{
		const FVector ToTalker = (Talker->GetActorLocation() - GetActorLocation());
		if (!ToTalker.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, ToTalker.Rotation().Yaw, 0.f));
		}
	}

	OnTalkStarted(Talker);
}

void AMainNpcCharacter::EndTalk()
{
	CurrentTalker = nullptr;
	SetMovementState(ENpcMovementState::Idle);
	SetBehaviorTreePaused(false);
	OnNpcTalkStateChanged.Broadcast(false);
	OnTalkEnded();
}

void AMainNpcCharacter::ResumeAIDuringTalk()
{
	SetBehaviorTreePaused(false);

	// Talk 상태로 두면 애니메이션이 대화 포즈에 묶여서 걷는 게 어색해진다.
	// 실제 이동은 BT의 Move To가 하므로 여기선 표시 상태만 Walk로 바꿔준다.
	if (GetMovementState() == ENpcMovementState::Talk)
	{
		SetMovementState(ENpcMovementState::Walk);
	}
}

void AMainNpcCharacter::PauseAIDuringTalk()
{
	if (AAIController* AIController = GetNpcAIController())
	{
		AIController->StopMovement();
	}

	SetBehaviorTreePaused(true);
	SetMovementState(ENpcMovementState::Talk);

	// 다시 멈춰 설 때는 대화 상대를 바라보게 한다.
	if (CurrentTalker)
	{
		const FVector ToTalker = (CurrentTalker->GetActorLocation() - GetActorLocation());
		if (!ToTalker.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, ToTalker.Rotation().Yaw, 0.f));
		}
	}
}

// ---------------------------------------------------------------------------
// 호감도 조회
// ---------------------------------------------------------------------------

bool AMainNpcCharacter::IsRelationshipAtLeast(ENpcRelationshipLevel MinimumLevel) const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->IsRelationshipAtLeast(GetNpcId(), MinimumLevel);
	}
	return false;
}

ENpcRelationshipLevel AMainNpcCharacter::GetRelationshipLevel() const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->GetRelationshipLevel(GetNpcId());
	}
	return ENpcRelationshipLevel::Neutral;
}

float AMainNpcCharacter::GetAffinityValue() const
{
	if (const UNpcRelationshipSubsystem* Subsystem = GetRelationshipSubsystem())
	{
		return Subsystem->GetAffinityValue(GetNpcId());
	}
	return 0.f;
}

UNpcRelationshipSubsystem* AMainNpcCharacter::GetRelationshipSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UNpcRelationshipSubsystem>();
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Behavior Tree 일시정지/재개 (Talk/Follow 스크립트 구간 보호용)
// ---------------------------------------------------------------------------

void AMainNpcCharacter::SetBehaviorTreePaused(bool bPaused) const
{
	// GetNpcAIController()가 반환하는 포인터는 원래 비const이므로 그대로 받는다.
	// (직전에 겪었던 "const 지역변수로 받아서 GetXxxComponent()가 const 포인터를 반환하는" 문제를 되풀이하지 않기 위함)
	if (AAIController* AIController = GetNpcAIController())
	{
		if (UBrainComponent* Brain = AIController->GetBrainComponent())
		{
			if (bPaused)
			{
				Brain->PauseLogic(TEXT("NPC scripted event (Talk/Follow)"));
			}
			else
			{
				Brain->ResumeLogic(TEXT("NPC scripted event (Talk/Follow)"));
			}
		}
	}
}
