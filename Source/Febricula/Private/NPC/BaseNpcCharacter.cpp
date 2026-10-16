// [skydodo] 2026-08-10 작성 (최종 수정 2026-08-21)
// 요약: NPC 공통 베이스 구현 - AI 컨트롤러·BT 기동, 이동 상태, 상호작용, 애니메이션 재생
// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/BaseNpcCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/CharacterAnimationComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dialogue/DialogueSubsystem.h"
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
	CharacterAnimationComponent = CreateDefaultSubobject<UCharacterAnimationComponent>("CharacterAnimationComponent");
}

// Called when the game starts or when spawned
void ABaseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 시작 상태를 명시적으로 한 번 밀어준다 (애니메이션 BP / Blackboard 초기값 동기화)
	SetMovementState(ENpcMovementState::Idle);
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->AddCharacterByName(NpcId, this);
	}

}

void ABaseNpcCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDialogueSubsystem* Dialogue = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		Dialogue->RemoveCharacterByName(NpcId, this);
	}
	Super::EndPlay(EndPlayReason);

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

bool ABaseNpcCharacter::SetNpcId(FName Id)
{
	if (NpcId.IsNone())
	{
		NpcId = Id;
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("NpcId 덮어쓰기 시도 차단됨"));
	return false;
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

void ABaseNpcCharacter::FaceTowards(const AActor* TargetActor)
{
	if (!TargetActor)
	{
		return;
	}

	const FVector ToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	// 위아래로 기울어지면 안 되므로 Yaw만 반영한다.
	if (!ToTarget.IsNearlyZero())
	{
		

		GetCharacterMovement()->bOrientRotationToMovement = false;
		
		SetActorRotation(FRotator(.0f, .0f, .0f));
		SetActorRotation(FRotator(0.f, ToTarget.Rotation().Yaw, 0.f));
		
		//UE_LOG(LogTemp, Warning, TEXT("후Yaw=%.1f"), GetActorRotation().Yaw);

	}
}

// ---------------------------------------------------------------------------
// 애니메이션
// ---------------------------------------------------------------------------

bool ABaseNpcCharacter::PlayNpcAnim(FName AnimId, bool bLoop)
{
	return CharacterAnimationComponent->PlayAnim(AnimId, bLoop);
	// const TObjectPtr<UAnimSequenceBase>* Found = AnimLibrary.Find(AnimId);
	// if (!Found || !*Found)
	// {
	// 	UE_LOG(LogTemp, Warning, TEXT("%s: AnimLibrary에 '%s' 항목이 없습니다."),
	// 		*GetNameSafe(this), *AnimId.ToString());
	// 	return false;
	// }
	//
	// USkeletalMeshComponent* MeshComp = GetMesh();
	// if (!MeshComp)
	// {
	// 	return false;
	// }
	//
	// UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	//
	// // 애님 블루프린트가 붙어 있고 단일 노드 모드를 강제하지 않았으면 슬롯 재생을 쓴다.
	// // 슬롯 위에서 재생되므로 그 아래 로코모션(걷기/대기)이 그대로 유지된다.
	// if (AnimInstance && !bPreferSingleNodeAnim)
	// {
	// 	// AnimSequence를 런타임에 몽타주로 감싸 슬롯에 얹는다. 몽타주 애셋이 없어도 된다.
	// 	// LoopCount는 반복 횟수라서 무한 루프가 없다. 지속 포즈는 큰 값으로 깔아두고
	// 	// StopNpcAnim으로 끊는 방식이 안전하다.
	// 	const int32 LoopCount = bLoop ? 1000 : 1;
	//
	// 	UAnimMontage* Played = AnimInstance->PlaySlotAnimationAsDynamicMontage(
	// 		*Found, AnimSlotName, AnimBlendTime, AnimBlendTime, /*InPlayRate=*/1.f, LoopCount);
	//
	// 	if (Played)
	// 	{
	// 		return true;
	// 	}
	//
	// 	UE_LOG(LogTemp, Warning,
	// 		TEXT("%s: 슬롯 '%s' 재생에 실패했습니다. 애님 블루프린트 그래프에 같은 이름의 Slot 노드가 있는지 확인하세요. 단일 노드 모드로 대체합니다."),
	// 		*GetNameSafe(this), *AnimSlotName.ToString());
	// }
	//
	// // 폴백: 애님 블루프린트를 거치지 않고 시퀀스를 직접 재생한다.
	// // 애님 BP가 아직 없는 NPC(T포즈 상태)에서도 애니메이션 확인이 가능하다.
	// // 재생 중에는 로코모션이 나오지 않지만, 애초에 애님 BP가 없으면 로코모션도 없으므로 손해가 없다.
	// MeshComp->PlayAnimation(*Found, bLoop);
	// return true;
}

void ABaseNpcCharacter::StopNpcAnim(float BlendOutTime, bool bFromLogic)
{
	// USkeletalMeshComponent* MeshComp = GetMesh();
	// if (!MeshComp)
	// {
	// 	return;
	// }
	//
	// if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
	// {
	// 	AnimInstance->StopSlotAnimation(BlendOutTime, AnimSlotName);
	// 	return;
	// }
	//
	// // 단일 노드 모드로 재생 중이던 경우 (애님 BP가 없는 NPC)
	// MeshComp->Stop();
	CharacterAnimationComponent->StopAnim(BlendOutTime, bFromLogic);
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