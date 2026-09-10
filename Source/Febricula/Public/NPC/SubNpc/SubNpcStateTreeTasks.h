// Fill out your copyright notice in the Description page of Project Settings.
//
// 서브 NPC용 StateTree 태스크와 조건.
//
// [왜 StateTree인가 — 그리고 왜 지금까지는 enum이었나]
//   서브 NPC의 상태는 넷(이동/스팟/반응/대기)뿐이고 전이 조건도 셋뿐이라,
//   트리를 얹어도 얻는 것 없이 개체당 오버헤드만 늘었다. 그래서 enum + switch로 갔다.
//
//   바뀌는 지점은 "행동 종류가 늘어나서 기획자가 직접 편집해야 할 때"다.
//   자판기 -> 벤치 -> 대화처럼 단계가 이어지고, 중간에 끊겼다 되돌아오는 흐름을
//   코드로 관리하면 금방 한계가 온다. StateTree는 그 전이를 에디터에서 그리게 해준다.
//
//   BehaviorTree 대신 StateTree인 이유는 개체당 비용이다. BT는 매 틱 노드 그래프를
//   순회하고 블랙보드를 조회하지만, StateTree는 상태가 바뀔 때만 전이를 계산한다.
//   그리고 Mass 위에서도 돌 수 있게 설계돼 있어서, 나중에 Mass로 옮길 때
//   이 태스크들을 거의 그대로 재사용할 수 있다.
//
// [사용 방법]
//   ASubNpcCharacter 블루프린트에 StateTreeComponent를 붙이고,
//   StateTree 애셋의 스키마를 "StateTree Component"로 만든 뒤 이 태스크들을 배치한다.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionContext.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "SubNpcStateTreeTasks.generated.h"

class ASubNpcCharacter;
class ANpcActivitySpot;

// ---------------------------------------------------------------------------
// 경로 걷기
// ---------------------------------------------------------------------------

USTRUCT()
struct FEBRICULA_API FSubNpcWalkTaskData
{
	GENERATED_BODY()

	/** 대상 행인. 스키마의 Context Actor에서 연결한다 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASubNpcCharacter> Npc = nullptr;

	/** 이 거리를 걷고 나면 태스크가 성공으로 끝난다. 0이면 무한히 걷는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float WalkDistance = 0.f;

	/** 시작 시점의 진행 거리 (내부용) */
	UPROPERTY()
	float StartDistance = 0.f;
};

/**
 * 경로를 따라 걷는다.
 *
 * 실제 이동 계산은 액터가 계속 하고 있고, 이 태스크는 "지금은 걷는 상태다"를
 * 선언하고 종료 조건만 본다. 태스크가 매 틱 위치를 계산하지 않는 게 중요하다 -
 * 계산을 태스크에 넣으면 나중에 Mass로 옮길 때 통째로 다시 써야 한다.
 */
USTRUCT(meta = (DisplayName = "Sub NPC: 경로 걷기"))
struct FEBRICULA_API FSubNpcWalkTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSubNpcWalkTaskData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ---------------------------------------------------------------------------
// 스팟 활동
// ---------------------------------------------------------------------------

USTRUCT()
struct FEBRICULA_API FSubNpcSpotTaskData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASubNpcCharacter> Npc = nullptr;

	/** 들를 스팟. 비워두면 현재 경로에서 가까운 스팟을 스스로 찾는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	TObjectPtr<ANpcActivitySpot> Spot = nullptr;

	/** 자리를 못 잡았을 때 실패로 끝낼지 (false면 그냥 성공 처리하고 지나간다) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bFailIfOccupied = true;
};

/**
 * 활동 스팟에서 행동한다.
 *
 * 단계 배열(ExtraSteps)까지 액터가 진행하므로, 태스크는 끝났는지만 본다.
 * 중간에 플레이어와 부딪히면 StateTree가 반응 상태로 전이하고,
 * 반응이 끝나면 다시 이 상태로 돌아온다 - 그 "되돌아오기"가
 * enum 방식에서 직접 짜야 했던 부분이고 StateTree에서는 전이 하나로 끝난다.
 */
USTRUCT(meta = (DisplayName = "Sub NPC: 스팟 활동"))
struct FEBRICULA_API FSubNpcSpotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSubNpcSpotTaskData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// ---------------------------------------------------------------------------
// 부딪힘 반응
// ---------------------------------------------------------------------------

USTRUCT()
struct FEBRICULA_API FSubNpcReactTaskData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASubNpcCharacter> Npc = nullptr;
};

/** 반응이 끝날 때까지 대기한다. 반응 종류와 애니메이션 선택은 액터가 이미 했다 */
USTRUCT(meta = (DisplayName = "Sub NPC: 부딪힘 반응"))
struct FEBRICULA_API FSubNpcReactTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSubNpcReactTaskData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ---------------------------------------------------------------------------
// 조건
// ---------------------------------------------------------------------------

USTRUCT()
struct FEBRICULA_API FSubNpcActivityConditionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ASubNpcCharacter> Npc = nullptr;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	ESubNpcActivity Activity = ESubNpcActivity::Walking;
};

/** 지금 이 상태인가. 반응 -> 원래 하던 일로 되돌아가는 전이를 그릴 때 쓴다 */
USTRUCT(meta = (DisplayName = "Sub NPC: 현재 상태가"))
struct FEBRICULA_API FSubNpcActivityCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FSubNpcActivityConditionData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
