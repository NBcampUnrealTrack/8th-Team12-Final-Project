// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcMassProcessors.h"

#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"
#include "MassExecutionContext.h"
#include "NPC/SubNpc/SubNpcCharacter.h"
#include "NPC/SubNpc/SubNpcProfiling.h"

int32 GSubNpcUseMass = 1;
static FAutoConsoleVariableRef CVarSubNpcUseMass(
	TEXT("crowd.Mass"),
	GSubNpcUseMass,
	TEXT("1이면 걷는 행인의 이동을 Mass 프로세서가 워커 스레드에서 병렬로 계산한다. 0이면 예전처럼 액터 틱에서 한 명씩 계산한다."),
	ECVF_Cheat);

namespace SubNpcMassTuning
{
	/**
	 * 워커 스레드 하나가 한 번에 맡는 행인 수.
	 * 한 명 계산이 수 µs라 너무 잘게 나누면 작업 분배 비용이 계산보다 커진다.
	 */
	constexpr int32 MinBatchSize = 4;
}

UMassSubNpcPathFollowProcessor::UMassSubNpcPathFollowProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ProcessingPhase = EMassProcessingPhase::PrePhysics;

	// 게임 스레드에서 실행하고 안에서 ParallelFor로 나눈다 (헤더 설명 참고).
	// 계산이 끝날 때까지 게임 스레드가 기다리므로, 액터 틱·Director와 같은 행인을 동시에 건드리지 않는다.
	bRequiresGameThreadExecution = true;
}

void UMassSubNpcPathFollowProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FSubNpcAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassSubNpcPathFollowProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	if (GSubNpcUseMass == 0)
	{
		return;
	}

	SUBNPC_PROFILE_SCOPE(MassPathFollow);

	struct FJob
	{
		ASubNpcCharacter* Npc = nullptr;
		float DeltaTime = 0.f;
		FSubNpcPathStep Step;
	};
	TArray<FJob> Jobs;

	// 1) 이번 프레임에 한 걸음 걸을 행인을 고른다 (게임 스레드).
	const float FrameDelta = Context.GetDeltaTimeSeconds();
	EntityQuery.ForEachEntityChunk(Context, [&Jobs, FrameDelta](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FSubNpcAgentFragment> Agents = ChunkContext.GetMutableFragmentView<FSubNpcAgentFragment>();
		for (FMassExecutionContext::FEntityIterator It = ChunkContext.CreateEntityIterator(); It; ++It)
		{
			FSubNpcAgentFragment& Agent = Agents[It];
			ASubNpcCharacter* Npc = Agent.Actor.Get();
			if (!Npc || !Npc->CanMassStep())
			{
				Agent.TimeSinceStep = 0.f;
				continue;
			}

			// 액터 틱 주기(LOD)를 지킨다. 멀리 있는 사람까지 매 프레임 계산하면 예전보다 오히려 비싸진다.
			Agent.TimeSinceStep += FrameDelta;
			if (Agent.TimeSinceStep < Npc->GetActorTickInterval())
			{
				continue;
			}

			FJob& Job = Jobs.AddDefaulted_GetRef();
			Job.Npc = Npc;
			Job.DeltaTime = Agent.TimeSinceStep;
			Agent.TimeSinceStep = 0.f;
		}
	});

	if (Jobs.Num() == 0)
	{
		return;
	}

	// 2) 계산 - 워커 스레드에서 병렬로. 각 행인은 자기 멤버만 바꾼다 (ComputePathStep 설명 참고).
	ParallelFor(TEXT("SubNpcPathCompute"), Jobs.Num(), SubNpcMassTuning::MinBatchSize, [&Jobs](int32 Index)
	{
		FJob& Job = Jobs[Index];
		Job.Npc->ComputePathStep(Job.DeltaTime, Job.Step);
	});

	// 3) 적용 - 게임 스레드에서 차례로 (위치 설정, 경로 갈아타기, 스팟 판정).
	for (FJob& Job : Jobs)
	{
		if (IsValid(Job.Npc))
		{
			Job.Npc->ApplyPathStep(Job.Step);
		}
	}
}
