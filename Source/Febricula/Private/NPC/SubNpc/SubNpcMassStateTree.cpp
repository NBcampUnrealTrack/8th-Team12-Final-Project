// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcMassStateTree.h"

#include "MassExecutionContext.h"
#include "MassSignalSubsystem.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeFragments.h"
#include "NPC/SubNpc/SubNpcCharacter.h"
#include "StateTreeLinker.h"

namespace SubNpcBehavior
{
	/** 걷는 중 StateTree를 깨우는 간격 (초). 걷기 태스크가 걸은 거리를 확인한다 */
	constexpr float WalkSignalInterval = 0.25f;

	static ASubNpcCharacter* GetNpc(FStateTreeExecutionContext& Context, const TStateTreeExternalDataHandle<FSubNpcAgentFragment>& Handle)
	{
		return Context.GetExternalData(Handle).Actor.Get();
	}
}

// ---------------------------------------------------------------------------
// 걷기
// ---------------------------------------------------------------------------

bool FMassSubNpcWalkTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(AgentHandle);
	return true;
}

void FMassSubNpcWalkTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(AgentHandle);
}

EStateTreeRunStatus FMassSubNpcWalkTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const ASubNpcCharacter* Npc = SubNpcBehavior::GetNpc(Context, AgentHandle);
	if (!Npc || Npc->GetActivity() != ESubNpcActivity::Walking)
	{
		return EStateTreeRunStatus::Failed;
	}
	Data.LastLocation = Npc->GetActorLocation();
	Data.Travelled = 0.f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMassSubNpcWalkTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const ASubNpcCharacter* Npc = SubNpcBehavior::GetNpc(Context, AgentHandle);
	if (!Npc || Npc->GetActivity() != ESubNpcActivity::Walking)
	{
		return EStateTreeRunStatus::Failed;   // 반응·스팟 등으로 바뀌었다. 루트에서 다시 고른다
	}

	// 경로를 갈아타도 끊기지 않게 경로 진행도가 아니라 실제로 움직인 거리로 잰다.
	const FVector Location = Npc->GetActorLocation();
	Data.Travelled += FVector::Dist2D(Location, Data.LastLocation);
	Data.LastLocation = Location;

	// 일정 거리마다 스팟에 들를지 굴려 보라고 요청한다. 실제 판정(확률·빈자리·줄 서기)은 액터가
	// 게임 스레드에서 한다 - 이 태스크는 게임 스레드가 아닌 곳에서 돌 수 있다.
	// 들르게 되면 행인 상태가 AtSpot으로 바뀌고, 다음 실행에서 이 태스크가 Failed로 끝나 루트가 스팟 상태를 고른다.
	if (Data.CheckDistance > 0.f && Data.Travelled >= Data.CheckDistance)
	{
		Data.Travelled = 0.f;
		if (ASubNpcCharacter* MutableNpc = Context.GetExternalData(AgentHandle).Actor.Get())
		{
			MutableNpc->RequestSpotRoll();
		}
	}
	return EStateTreeRunStatus::Running;
}

// ---------------------------------------------------------------------------
// 상태가 끝날 때까지 대기
// ---------------------------------------------------------------------------

bool FMassSubNpcWaitActivityTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(AgentHandle);
	return true;
}

void FMassSubNpcWaitActivityTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(AgentHandle);
}

EStateTreeRunStatus FMassSubNpcWaitActivityTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	return Tick(Context, 0.f);
}

EStateTreeRunStatus FMassSubNpcWaitActivityTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const ASubNpcCharacter* Npc = SubNpcBehavior::GetNpc(Context, AgentHandle);
	if (!Npc)
	{
		return EStateTreeRunStatus::Failed;
	}
	return (Npc->GetActivity() == Data.Activity) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

// ---------------------------------------------------------------------------
// 조건
// ---------------------------------------------------------------------------

bool FMassSubNpcActivityCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(AgentHandle);
	return true;
}

void FMassSubNpcActivityCondition::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly(AgentHandle);
}

bool FMassSubNpcActivityCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	const ASubNpcCharacter* Npc = SubNpcBehavior::GetNpc(Context, AgentHandle);
	return Npc && Npc->GetActivity() == Data.Activity;
}

// ---------------------------------------------------------------------------
// 신호
// ---------------------------------------------------------------------------

UMassSubNpcBehaviorSignalProcessor::UMassSubNpcBehaviorSignalProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ProcessingPhase = EMassProcessingPhase::PrePhysics;
	bRequiresGameThreadExecution = true;   // 액터 상태를 읽는다
	ExecutionOrder.ExecuteAfter.Add(UMassSubNpcPathFollowProcessor::StaticClass()->GetFName());
}

void UMassSubNpcBehaviorSignalProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FSubNpcAgentFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FSubNpcBehaviorFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadOnly);
	ProcessorRequirements.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UMassSubNpcBehaviorSignalProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UMassSignalSubsystem* Signals = Context.GetMutableSubsystem<UMassSignalSubsystem>();
	if (!Signals)
	{
		return;
	}

	const float DeltaTime = Context.GetDeltaTimeSeconds();
	TArray<FMassEntityHandle> ToWake;

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& ChunkContext)
	{
		const TConstArrayView<FSubNpcAgentFragment> Agents = ChunkContext.GetFragmentView<FSubNpcAgentFragment>();
		const TArrayView<FSubNpcBehaviorFragment> Behaviors = ChunkContext.GetMutableFragmentView<FSubNpcBehaviorFragment>();

		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			const ASubNpcCharacter* Npc = Agents[It].Actor.Get();
			FSubNpcBehaviorFragment& Behavior = Behaviors[It];
			const ESubNpcActivity Activity = IsValid(Npc) ? Npc->GetActivity() : ESubNpcActivity::Pooled;

			bool bWake = (Activity != Behavior.LastActivity);
			Behavior.LastActivity = Activity;

			if (Activity == ESubNpcActivity::Walking)
			{
				Behavior.TimeSinceSignal += DeltaTime;
				bWake |= (Behavior.TimeSinceSignal >= SubNpcBehavior::WalkSignalInterval);
			}

			if (bWake && Activity != ESubNpcActivity::Pooled)
			{
				Behavior.TimeSinceSignal = 0.f;
				ToWake.Add(ChunkContext.GetEntity(It));
			}
		}
	});

	if (ToWake.Num() > 0)
	{
		Signals->SignalEntities(UE::Mass::Signals::NewStateTreeTaskRequired, ToWake);
	}
}
