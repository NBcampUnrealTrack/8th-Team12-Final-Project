// Fill out your copyright notice in the Description page of Project Settings.
//
// 대화 그래프 실행 + 이벤트 선택 + 액션 실행을 담당하는 런타임 코어.
//
// 흐름:
//   1) 플레이어가 NPC와 상호작용 → TryStartEventForNpc(NpcId)
//   2) 이벤트 테이블에서 해당 NPC의 이벤트 중 전제조건을 만족하는 것을 Priority 순으로 고름
//   3) 그 이벤트의 RootNodeId부터 대화 테이블을 따라가며 진행
//   4) 노드마다 OnDialogueLine / OnDialogueChoices 델리게이트를 쏘고, HUD가 그걸 받아 표시
//   5) 대화가 끝나면 OnDialogueFinished → 이벤트 완료 처리 + NPC를 Talk 상태에서 해제

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Dialogue/DialogueDataTypes.h"
#include "DialogueSubsystem.generated.h"

class ABaseNpcCharacter;
class UGameProgressSubsystem;
class UNpcRelationshipSubsystem;

/** 대사 한 줄이 출력될 때. Speaker가 비어 있으면 나레이션(사물 조사 등) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueLine, const FText&, Speaker, const FText&, Body);

/** 선택지를 띄워야 할 때 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueChoices, const TArray<FDialogueChoiceView>&, Choices);

/** 대화가 완전히 끝났을 때 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogueFinished);

UCLASS()
class FEBRICULA_API UDialogueSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---- 데이터 테이블 주입 ----

	/**
	 * 사용할 DataTable을 등록한다. 보통 게임 시작 시 한 번(플레이어의 UInteractionComponent가 자동 호출).
	 * EventTable  : FNpcEventRow
	 * DialogueTable: FDialogueNodeRow
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void SetDataTables(UDataTable* InEventTable, UDataTable* InDialogueTable);

	// ---- 대화 시작 ----

	/**
	 * NpcId에 해당하는 이벤트 중 지금 발생 가능한 것을 찾아 대화를 시작한다.
	 * 조건을 만족하는 이벤트가 하나도 없으면 false를 반환한다 (이때는 기본 잡담을 띄우면 된다).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool TryStartEventForNpc(FName NpcId, AActor* InInstigator);

	/** 특정 노드부터 직접 대화를 시작한다 (이벤트 테이블을 거치지 않는 경우) */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool StartDialogueAtNode(FName NodeId, AActor* InInstigator);

	/**
	 * 사물 조사처럼 테이블에 없는 즉석 대사를 띄운다.
	 * ("시체인 것 같다." 같은 한 줄짜리 나레이션용)
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	bool StartSimpleLines(const FText& Speaker, const TArray<FText>& Lines, AActor* InInstigator);

	// ---- 진행 ----

	/** 대사창 클릭 등으로 다음으로 넘길 때 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void Advance();

	/** 선택지를 골랐을 때 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void SelectChoice(int32 ChoiceIndex);

	/** 강제 종료 */
	UFUNCTION(BlueprintCallable, Category = "Dialogue")
	void FinishDialogue();

	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsDialogueActive() const { return bDialogueActive; }

	/** 선택지 표시 중인가 (이 상태에선 Advance를 무시해야 한다) */
	UFUNCTION(BlueprintPure, Category = "Dialogue")
	bool IsWaitingForChoice() const { return bWaitingForChoice; }

	// ---- HUD가 구독할 델리게이트 ----

	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueLine OnDialogueLine;

	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueChoices OnDialogueChoices;

	UPROPERTY(BlueprintAssignable, Category = "Dialogue")
	FOnDialogueFinished OnDialogueFinished;

protected:
	/** 노드로 진입해서 OnEnterActions 실행 + 대사 출력까지 */
	void EnterNode(FName NodeId);

	/** 대사를 다 읽은 뒤 다음 목적지(선택지/분기/다음노드)를 결정 */
	void ResolveNodeExit();

	void ExecuteActions(const TArray<FGameAction>& Actions);
	void ExecuteAction(const FGameAction& Action);

	UGameProgressSubsystem* GetProgress() const;
	UNpcRelationshipSubsystem* GetRelationship() const;

	/** 월드에서 NpcId가 일치하는 NPC 액터를 찾는다 (AI 액션 실행용) */
	ABaseNpcCharacter* FindNpcById(FName NpcId) const;

	UPROPERTY()
	TObjectPtr<UDataTable> EventTable;

	UPROPERTY()
	TObjectPtr<UDataTable> DialogueTable;

	/** 지금 대화 중인 상대(NPC 또는 사물) */
	UPROPERTY()
	TObjectPtr<AActor> CurrentInstigator;

	/** 진행 중인 이벤트 행 이름. 끝날 때 완료 처리에 쓴다 */
	FName ActiveEventId;
	bool bCompleteEventOnFinish = false;

	FName CurrentNodeId;
	bool bDialogueActive = false;
	bool bWaitingForChoice = false;

	/** 지금 표시 중인 선택지의 원본 (SelectChoice에서 참조) */
	TArray<FDialogueChoice> PendingChoices;

	/** StartSimpleLines로 띄운 즉석 대사 큐 */
	TArray<FText> SimpleLineQueue;
	FText SimpleLineSpeaker;
	bool bRunningSimpleLines = false;
};
