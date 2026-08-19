// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NPC/BaseNpcCharacter.h"
#include "MainNpcCharacter.generated.h"

class ANpcRoute;
class UNpcRelationshipSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNpcTalkStateChanged, bool, bIsTalking);

/**
 * 스토리 + 호감도를 갖는 메인 NPC의 베이스 (한서윤/김도윤/이서아/고시원 주인 등이 이걸 상속).
 * Idle/Walk/Interact 등 공통 기능은 부모(ABaseNpcCharacter)를 그대로 쓰고,
 * 여기서는 메인 NPC만 필요한 것 세 가지만 추가한다:
 *   1) 추적/동행 (Follow) - 순찰 동행, 산책 동행 등
 *   2) 대화 (Talk) - 이동 정지 + 상태 전환 + 훅
 *   3) 호감도 조회 - 실제 값은 UNpcRelationshipSubsystem 소유, 여긴 읽기 전용 래퍼만
 */
UCLASS(Abstract)
class FEBRICULA_API AMainNpcCharacter : public ABaseNpcCharacter
{
	GENERATED_BODY()

public:
	AMainNpcCharacter();

	virtual void BeginPlay() override;

	virtual bool IsMainNpc() const override { return true; }

	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;

	/**
	 * 상호작용 시 Talk 상태로 들어가고 이벤트 트리 대화를 시작한다.
	 * 조건을 만족하는 이벤트가 없으면 DefaultIdleNodeId(있다면)를 재생하고, 그것도 없으면 그냥 지나간다.
	 */
	virtual void Interact_Implementation(AActor* Interactor) override;

	/** 이동 경로 조회 (대화 액션 NpcStartPatrol / NpcStartGuide에서 사용) */
	UFUNCTION(BlueprintPure, Category = "NPC|AI")
	ANpcRoute* GetDefaultRoute() const { return DefaultRoute; }

	// ---- 추적/동행 ----

	/** 대상을 계속 따라 이동한다 (한서윤 순찰 동행, 김도윤 산책 동행 등) */
	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StartFollowTarget(AActor* TargetToFollow);

	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StopFollowTarget();

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	bool IsFollowing() const { return GetMovementState() == ENpcMovementState::Follow; }

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	AActor* GetFollowTarget() const { return CurrentFollowTarget; }

	// ---- 대화 ----

	/**
	 * 대화 진입. 실제 대화 내용/선택지/이벤트 트리 분기는 별도 Dialogue/EventTree 매니저가 처리하고,
	 * 여긴 "이동 정지 + Talk 상태 전환 + 훅 브로드캐스트"까지만 담당한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void StartTalk(AActor* Talker);

	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	virtual void EndTalk();

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	bool IsTalking() const { return GetMovementState() == ENpcMovementState::Talk; }

	UFUNCTION(BlueprintPure, Category = "NPC|Main")
	AActor* GetCurrentTalker() const { return CurrentTalker; }

	/**
	 * 대화 중인데도 AI를 다시 돌린다 ("걸으면서 대화하기" 연출용).
	 * StartTalk은 기본적으로 NPC를 제자리에 세우는데, 학교 안내처럼 이동과 대사가 맞물려야 하는
	 * 장면에서는 대화 데이터가 이 함수를 호출해서 AI를 깨운다.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	void ResumeAIDuringTalk();

	/** 다시 제자리에 세운다 */
	UFUNCTION(BlueprintCallable, Category = "NPC|Main")
	void PauseAIDuringTalk();

	// ---- 호감도 조회 (실제 값의 소유자는 UNpcRelationshipSubsystem) ----

	/** 이벤트 트리 전제조건("호의 이상" 등) 판정에 바로 쓰는 헬퍼 */
	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	bool IsRelationshipAtLeast(ENpcRelationshipLevel MinimumLevel) const;

	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	ENpcRelationshipLevel GetRelationshipLevel() const;

	UFUNCTION(BlueprintPure, Category = "NPC|Relationship")
	float GetAffinityValue() const;

	UPROPERTY(BlueprintAssignable, Category = "NPC|Main")
	FOnNpcTalkStateChanged OnNpcTalkStateChanged;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Main")
	void OnTalkStarted(AActor* Talker);

	UFUNCTION(BlueprintImplementableEvent, Category = "NPC|Main")
	void OnTalkEnded();

	UNpcRelationshipSubsystem* GetRelationshipSubsystem() const;

	/**
	 * Talk/Follow처럼 게임플레이가 직접 이동을 지시하는 스크립트 구간 동안
	 * AMainNpcAIController의 Behavior Tree(Idle/Patrol/Chase)가 끼어들어 목적지를
	 * 덮어쓰지 못하도록 잠깐 멈춘다. true=일시정지, false=재개.
	 */
	void SetBehaviorTreePaused(bool bPaused) const;

	/**
	 * 레벨에 배치한 이동 경로. 순찰(Patrol)과 안내(Guide)가 같은 경로를 공유한다.
	 * EditInstanceOnly라 "레벨에 배치된 NPC를 선택한 상태"의 디테일 패널에서 지정한다
	 * (경로는 레벨마다 다르므로 클래스 디폴트가 아니라 인스턴스 값이 맞다).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPC|AI")
	TObjectPtr<ANpcRoute> DefaultRoute;

	/**
	 * DefaultRoute가 지정되어 있을 때 BeginPlay에서 무엇을 자동으로 시작할지.
	 * 블루프린트 배선 없이 레벨에 놓기만 해도 동작을 확인할 수 있게 하려는 편의 기능이다.
	 * 이벤트 트리로 제어할 NPC라면 None으로 두면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPC|AI")
	ENpcAutoStartMode AutoStartMode = ENpcAutoStartMode::Patrol;

	/**
	 * 발생 가능한 이벤트가 하나도 없을 때 재생할 기본 대화 노드.
	 * ("오늘은 별일 없어." 같은 잡담. 비워두면 상호작용해도 아무 일도 일어나지 않는다)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Dialogue")
	FName DefaultIdleNodeId;

	/** Blackboard의 추적/동행 대상 키 이름 (Object 키) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|AI")
	FName FollowTargetBlackboardKey = TEXT("FollowTarget");

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Main")
	TObjectPtr<AActor> CurrentFollowTarget;

	UPROPERTY(BlueprintReadOnly, Category = "NPC|Main")
	TObjectPtr<AActor> CurrentTalker;
};
