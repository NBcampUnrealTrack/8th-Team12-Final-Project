// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interaction/Interactable.h"
#include "NPC/NpcTypes.h"
#include "BaseNpcCharacter.generated.h"

class UCharacterAnimationComponent;
class UBehaviorTree;
class AAIController;
class UAnimSequenceBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNpcMovementStateChanged, ENpcMovementState, OldState, ENpcMovementState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNpcInteracted, AActor*, Interactor);

/**
 * 모든 NPC(메인/서브)의 베이스 캐릭터.
 * 여긴 정말로 모든 NPC가 공유하는 것만 둔다:
 *   - 식별(NpcId), 기본 스폰 방식 참조
 *   - AIController + BehaviorTree 연동
 *   - Idle/Walk 이동 상태머신과 걷기 이동 요청
 *   - 상호작용 진입점(IInteractable)
 * 추적/동행(Follow), 대화(Talk), 호감도 조회 같은 메인 NPC 전용 기능은
 * AMainNpcCharacter(ABaseNpcCharacter 상속)에 있다. 교장/학생부장 같은 서브 NPC는
 * ASubNpcCharacter를 상속해서 이 클래스 그대로 쓰면 된다.
 */
UCLASS(Abstract)
class FEBRICULA_API ABaseNpcCharacter : public ACharacter, public IInteractable
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ABaseNpcCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// AIController가 붙는 순간(스폰/레벨 배치) BehaviorTree를 기동한다
	virtual void PossessedBy(AController* NewController) override;

	//~ Begin IInteractable
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	//~ End IInteractable

	/**
	 * 이 NPC가 메인 NPC(스토리+호감도 보유)인지.
	 * 필드가 아니라 virtual 함수로 두는 이유: Main/Sub는 클래스 자체로 나뉘므로(상속),
	 * "클래스는 Sub인데 필드는 Main으로 잘못 세팅"되는 사고를 원천적으로 막기 위함이다.
	 * 베이스는 항상 false, AMainNpcCharacter가 true로 오버라이드한다.
	 */
	UFUNCTION(BlueprintPure, Category = "NPC")
	virtual bool IsMainNpc() const { return false; }

	/** 이벤트 트리·세이브 데이터·호감도 매니저 조회용 고유 ID (예: "NPC_HanSeoYun") */
	UFUNCTION(BlueprintPure, Category = "NPC")
	FName GetNpcId() const { return NpcId; }
	
	UFUNCTION(BlueprintCallable, Category = "NPC")
	bool SetNpcId(FName Id);
	
	UFUNCTION(BlueprintPure, Category = "NPC")
	ENpcSpawnMode GetDefaultSpawnMode() const { return DefaultSpawnMode; }

	UFUNCTION(BlueprintPure, Category = "NPC|Movement")
	ENpcMovementState GetMovementState() const { return CurrentMovementState; }

	/**
	 * 이동/행동 상태 전환. 애니메이션 블루프린트는 OnNpcMovementStateChanged를 구독해서
	 * Idle/Walk 등 몽타주·블렌드스페이스를 갈아탄다.
	 * BehaviorTreeAsset이 돌고 있다면 Blackboard의 MovementStateBlackboardKey에도 값을 반영한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void SetMovementState(ENpcMovementState NewState);

	/** 지정 위치로 걸어서 이동 (서브 NPC의 순찰, 메인 NPC의 자유 이동 등에 공통 사용) */
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void MoveToLocation(const FVector& Destination, float AcceptanceRadius = 50.f);

	/** 특정 액터 근처로 이동 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void MoveToTargetActor(AActor* TargetActor, float AcceptanceRadius = 100.f);

	/** 진행 중인 이동 요청을 취소하고 Idle로 복귀 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	virtual void StopMovementAndIdle();

	/** 지정한 액터 쪽을 바라보도록 즉시 회전 (Yaw만, 수평) */
	UFUNCTION(BlueprintCallable, Category = "NPC|Movement")
	void FaceTowards(const AActor* TargetActor);

	// ---- 애니메이션 -------------------------------------------------------

	/**
	 * AnimLibrary에 등록된 AnimId를 슬롯 애니메이션으로 재생한다.
	 *
	 * AnimMontage 애셋이 없어도 AnimSequence만으로 동작한다.
	 * PlaySlotAnimationAsDynamicMontage가 시퀀스를 런타임에 몽타주로 감싸서 슬롯에 얹어주기 때문에,
	 * 애니메이터가 몽타주를 따로 만들어주지 않아도 된다.
	 *
	 * 전제 조건: 이 NPC의 애님 블루프린트 그래프에 Slot 노드(기본 이름 "DefaultSlot")가 있어야 한다.
	 * 슬롯이 없으면 재생은 되지만 화면에 반영되지 않는다.
	 *
	 * @param AnimId  AnimLibrary의 키
	 * @param bLoop   true면 계속 반복 (앉아있기 같은 지속 포즈), false면 한 번만
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Animation")
	bool PlayNpcAnim(FName AnimId, bool bLoop = false);

	/** 재생 중인 슬롯 애니메이션을 중단하고 원래 상태로 블렌드 아웃 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Animation")
	void StopNpcAnim(float BlendOutTime = 0.25f, bool bFromLogic = false);
	
	UFUNCTION(BlueprintCallable, Category = "NPC|LookAt")
	const AActor* GetLookAtActor() const { return LookAtTarget.Get(); }
	

	UPROPERTY(BlueprintAssignable, Category = "NPC|Movement")
	FOnNpcMovementStateChanged OnNpcMovementStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Interaction")
	FOnNpcInteracted OnNpcInteracted;

protected:
	/**
	 * Interact_Implementation이 상태 체크 이후 호출하는 블루프린트 훅.
	 * 실제 이벤트 트리 진입(전제조건/발생조건 체크, 선택지 위젯 오픈 등)은
	 * 여기서 EventTreeManager/DialogueSubsystem으로 넘기도록 서브클래스/블루프린트에서 구현한다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Interaction")
	void OnInteracted(AActor* Interactor);

	AAIController* GetNpcAIController() const;

	/** BehaviorTree/Blackboard가 있다면 현재 이동 상태를 Blackboard 값으로도 밀어준다 */
	void PushMovementStateToBlackboard(ENpcMovementState State) const;

	/** 이벤트 트리·세이브 데이터·호감도 매니저 조회용 고유 ID. 서브클래스/블루프린트에서 고정값으로 세팅 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC", meta = (DisplayPriority = 0))
	FName NpcId;

	/** 레벨 고정배치 / 랜덤 인카운터 여부 (참조용, 실제 스폰은 스포너가 담당) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC")
	ENpcSpawnMode DefaultSpawnMode = ENpcSpawnMode::FixedInLevel;

	/** AIController가 기동할 기본 행동트리 (순찰/추적/자유이동 분기는 BT/BB 쪽 책임) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	/** Blackboard의 이동 상태 키 이름 (Enum 키로 구성해서 BT에서 상태별 분기) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	FName MovementStateBlackboardKey = TEXT("MovementState");
	
	UPROPERTY(BlueprintReadOnly, Category = "NPC|Movement")
	ENpcMovementState CurrentMovementState = ENpcMovementState::Idle;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC | Component")
	TObjectPtr<UCharacterAnimationComponent> CharacterAnimationComponent;
	
private:
	TWeakObjectPtr<const AActor> LookAtTarget; 
};