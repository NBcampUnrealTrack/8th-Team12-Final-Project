// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MassCrowd/SubNpcStateTreeTasks.h"

#include "NPC/SubNpc/NpcActivitySpot.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcCharacter.h"

// ---------------------------------------------------------------------------
// 경로 걷기
// ---------------------------------------------------------------------------

EStateTreeRunStatus FSubNpcWalkTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.StartDistance = Data.Npc->GetPathDistance();
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSubNpcWalkTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 걷는 동안 부딪히면 액터가 상태를 Reacting으로 바꾼다.
	// 태스크는 그걸 감지해 종료하고, 전이는 StateTree가 결정한다.
	if (Data.Npc->GetActivity() != ESubNpcActivity::Walking)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (Data.WalkDistance <= 0.f)
	{
		return EStateTreeRunStatus::Running;   // 무한히 걷는다
	}

	const float Travelled = FMath::Abs(Data.Npc->GetPathDistance() - Data.StartDistance);
	return (Travelled >= Data.WalkDistance) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}

// ---------------------------------------------------------------------------
// 스팟 활동
// ---------------------------------------------------------------------------

EStateTreeRunStatus FSubNpcSpotTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 스팟을 지정하지 않았으면 액터가 알아서 고른다.
	// 후보 선정·확률 판정·점유는 이미 액터에 구현돼 있으므로 여기서 중복 구현하지 않는다.
	if (!Data.Spot)
	{
		Data.Npc->TryEnterNearbySpot();
	}
	else
	{
		Data.Npc->TryEnterSpot(Data.Spot);
	}

	if (Data.Npc->GetActivity() != ESubNpcActivity::AtSpot)
	{
		return Data.bFailIfOccupied ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSubNpcSpotTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return EStateTreeRunStatus::Failed;
	}

	switch (Data.Npc->GetActivity())
	{
	case ESubNpcActivity::AtSpot:
		return EStateTreeRunStatus::Running;

	case ESubNpcActivity::Reacting:
		// 부딪혔다. 태스크는 끝내고 반응 상태로 전이시킨다.
		// 액터가 점유를 유지하고 있으므로 반응이 끝나면 이 상태로 되돌아올 수 있다.
		return EStateTreeRunStatus::Succeeded;

	default:
		return EStateTreeRunStatus::Succeeded;
	}
}

void FSubNpcSpotTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return;
	}

	// [중요] 반응으로 빠지는 경우에는 점유를 풀면 안 된다.
	// 풀어버리면 그 사이에 다른 개체가 자리를 차지해서 되돌아올 곳이 없어진다.
	if (Data.Npc->GetActivity() == ESubNpcActivity::Reacting)
	{
		return;
	}

	Data.Npc->LeaveSpotIfAny();
}

// ---------------------------------------------------------------------------
// 부딪힘 반응
// ---------------------------------------------------------------------------

EStateTreeRunStatus FSubNpcReactTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Npc)
	{
		return EStateTreeRunStatus::Failed;
	}

	return (Data.Npc->GetActivity() == ESubNpcActivity::Reacting)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Succeeded;
}

// ---------------------------------------------------------------------------
// 조건
// ---------------------------------------------------------------------------

bool FSubNpcActivityCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	return Data.Npc && Data.Npc->GetActivity() == Data.Activity;
}
