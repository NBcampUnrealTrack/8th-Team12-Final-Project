// Fill out your copyright notice in the Description page of Project Settings.
//
// 대화 / 이벤트 트리 데이터의 공용 타입 모음.
//
// 설계 핵심: "조건(Condition)"과 "액션(Action)"을 데이터로 표현해서,
// 이벤트 트리 문서에 적힌 [전제 조건] / [분기] / [친밀도 상승] / [Tag 획득] 같은 항목을
// C++ 코드를 새로 짜지 않고 DataTable만 고쳐서 늘릴 수 있게 한다.
//
// 데이터는 DataTable 2개로 나뉜다:
//   - 이벤트 테이블 (FNpcEventRow)     : "언제 어떤 대화를 시작할지"
//   - 대화 테이블   (FDialogueNodeRow) : "그 대화가 어떻게 흘러가는지"

#pragma once

// 파일명 주의: 엔진에 이미 Sound/DialogueTypes.h가 있어서 이름이 겹치면 UHT가 빌드를 거부한다.
// 그래서 DialogueTypes.h가 아니라 DialogueDataTypes.h로 둔다.

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DialogueDataTypes.generated.h"

/**
 * 조건 종류. 이벤트 전제조건, 대화 자동분기, 선택지 노출 조건에 공통으로 쓴다.
 * 각 항목 옆 주석은 FGameCondition의 Key / Value를 어떻게 채우는지를 뜻한다.
 */
UENUM(BlueprintType)
enum class EGameConditionType : uint8
{
	None					UMETA(DisplayName = "없음(항상 참)"),

	/** Value일차 이상 진행됐는가 (예: 14일차 이상) */
	DayAtLeast				UMETA(DisplayName = "N일차 이상"),

	/** Key 이벤트를 이미 완료했는가 */
	EventCompleted			UMETA(DisplayName = "이벤트 완료함"),

	/** Key 이벤트를 아직 완료하지 않았는가 */
	EventNotCompleted		UMETA(DisplayName = "이벤트 미완료"),

	/** Key(NpcId)와의 관계가 Value(ENpcRelationshipLevel) 이상인가 */
	RelationshipAtLeast		UMETA(DisplayName = "관계 O 이상"),

	/** Key 태그를 보유했는가 (예: 일름보, 철두철미) */
	HasTag					UMETA(DisplayName = "태그 보유"),

	/** Key 태그가 없는가 */
	NotHasTag				UMETA(DisplayName = "태그 미보유"),

	/** Key 카운터가 Value 이상인가 (예: Violation >= 3) */
	CounterAtLeast			UMETA(DisplayName = "카운터 N 이상"),

	/** Key 카운터가 Value 이하인가 */
	CounterAtMost			UMETA(DisplayName = "카운터 N 이하"),

	/** Key 능력치가 Value 이상인가 (예: Charm >= 5) - 매력/담력 선택지 조건용 */
	StatAtLeast				UMETA(DisplayName = "능력치 N 이상"),

	/** 주사위 판정. Key 능력치를 보정으로 쓰고 Value를 목표치로 삼는다 */
	RollCheck				UMETA(DisplayName = "판정(주사위)"),
};

/**
 * 액션 종류. 대화 노드 진입 시 / 선택지 선택 시 / 분기 확정 시 실행된다.
 */
UENUM(BlueprintType)
enum class EGameActionType : uint8
{
	None					UMETA(DisplayName = "없음"),

	/** Key(NpcId) 친밀도를 Value만큼 증감 (음수 가능) */
	AddAffinity				UMETA(DisplayName = "친밀도 증감"),

	/** Key 태그 부여 */
	GrantTag				UMETA(DisplayName = "태그 획득"),

	/** Key 태그 제거 */
	RemoveTag				UMETA(DisplayName = "태그 제거"),

	/** Key 카운터를 Value만큼 증감 */
	AddCounter				UMETA(DisplayName = "카운터 증감"),

	/** Key 이벤트를 완료 처리 */
	CompleteEvent			UMETA(DisplayName = "이벤트 완료 처리"),

	/** Key 대화 주제 획득 (다른 NPC와 이 주제로 대화 가능해짐) */
	GrantTopic				UMETA(DisplayName = "대화 주제 획득"),

	// ---- AI 연동 ----

	/** Key(NpcId) NPC를 Value(ENpcAIState)로 전환 */
	NpcSetAIState			UMETA(DisplayName = "NPC AI 상태 변경"),

	/** Key(NpcId) NPC가 플레이어를 따라오게 함 */
	NpcFollowPlayer			UMETA(DisplayName = "NPC 플레이어 추적 시작"),

	/** Key(NpcId) NPC 추적 중단 */
	NpcStopFollow			UMETA(DisplayName = "NPC 추적 중단"),

	/** Key(NpcId) NPC가 자기 DefaultPatrolRoute로 순찰 시작 */
	NpcStartPatrol			UMETA(DisplayName = "NPC 순찰 시작"),

	/** Key(NpcId) NPC 순찰 중단 */
	NpcStopPatrol			UMETA(DisplayName = "NPC 순찰 중단"),
};

/**
 * 데이터로 표현하는 단일 조건.
 * 여러 개를 배열로 두면 전부 만족해야 통과(AND)로 동작한다.
 */
USTRUCT(BlueprintType)
struct FGameCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EGameConditionType Type = EGameConditionType::None;

	/** 대상 식별자. NpcId / 태그 이름 / 카운터 이름 / 능력치 이름 등 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FName Key;

	/** 비교값. 일차, 카운터 수치, 관계 등급(ENpcRelationshipLevel의 정수값) 등 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	int32 Value = 0;
};

/**
 * 데이터로 표현하는 단일 액션.
 */
USTRUCT(BlueprintType)
struct FGameAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	EGameActionType Type = EGameActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FName Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	int32 Value = 0;
};

/**
 * 플레이어 선택지 하나.
 * Conditions를 만족하지 못하면 목록에 보이되 회색으로 비활성화된다
 * ("매력이 부족해서 못 고르는 선택지"가 있다는 걸 플레이어가 알 수 있어야 하므로).
 */
USTRUCT(BlueprintType)
struct FDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	FText Text;

	/** 선택 가능 조건. 비어 있으면 항상 선택 가능 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	TArray<FGameCondition> Conditions;

	/** 이 선택지를 골랐을 때 실행할 액션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	TArray<FGameAction> Actions;

	/** 다음 노드. None이면 대화 종료 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	FName NextNodeId;
};

/**
 * 자동 분기 한 갈래. 선택지 없이 "지금까지의 행적"으로 갈라질 때 쓴다.
 * 배열 위에서부터 검사해 처음으로 조건을 만족하는 갈래를 탄다 (= else if 사슬).
 */
USTRUCT(BlueprintType)
struct FDialogueBranch
{
	GENERATED_BODY()

	/** 비어 있으면 무조건 통과 → 배열 맨 끝에 두면 else 역할 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Branch")
	TArray<FGameCondition> Conditions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Branch")
	TArray<FGameAction> Actions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Branch")
	FName NextNodeId;
};

/**
 * 대화 노드 한 개 = DataTable 한 행.
 *
 * 진행 순서:
 *   1) OnEnterActions 실행
 *   2) SpeakerName / Text 를 HUD에 출력
 *   3) 플레이어가 클릭해서 넘기면:
 *      - Choices가 있으면  → 선택지 표시
 *      - Branches가 있으면 → 위에서부터 조건 검사해 첫 통과 갈래로 이동
 *      - 둘 다 없으면      → NextNodeId로 이동 (None이면 대화 종료)
 */
USTRUCT(BlueprintType)
struct FDialogueNodeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 화면에 표시할 화자 이름. 비우면 나레이션(사물 조사 문구 등)으로 취급 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText SpeakerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FGameAction> OnEnterActions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FDialogueChoice> Choices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FDialogueBranch> Branches;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName NextNodeId;
};

/**
 * NPC 이벤트 한 개 = 이벤트 DataTable 한 행.
 * "이 NPC와 상호작용했을 때 어떤 대화를 시작할지"를 조건과 함께 정의한다.
 */
USTRUCT(BlueprintType)
struct FNpcEventRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 이 이벤트의 주인 NPC (ABaseNpcCharacter의 NpcId와 일치해야 함) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	FName NpcId;

	/** 여러 이벤트가 동시에 조건을 만족하면 이 값이 큰 쪽이 먼저 실행된다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	int32 Priority = 0;

	/** true면 완료 후에도 조건만 맞으면 다시 발생 (4번 이벤트처럼 반복 가능한 것) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	bool bRepeatable = false;

	/** 전제 조건 + 발생 조건. 전부 만족해야 이 이벤트가 후보가 된다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	TArray<FGameCondition> Preconditions;

	/** 시작할 대화 노드 (대화 DataTable의 행 이름) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	FName RootNodeId;

	/** true면 대화가 끝날 때 이 이벤트를 자동으로 완료 처리한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	bool bCompleteOnFinish = true;
};

/**
 * HUD로 넘기는 선택지 표시용 뷰 데이터.
 * bEnabled=false면 "매력 부족" 같은 이유로 못 고르는 선택지 → 회색 처리한다.
 */
USTRUCT(BlueprintType)
struct FDialogueChoiceView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bEnabled = true;
};
