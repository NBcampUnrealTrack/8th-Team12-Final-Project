// [skydodo] 2026-10-07 작성
// 요약: 행인 상태 결정 Mass StateTree (Mass 3단계) - 프래그먼트와 태스크 정의
// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인 상태 결정 — Mass StateTree (Mass 3단계).
//
// [하는 일]
//   "걷다가 언제 스팟에 들를지", "반응·스팟이 끝나면 무엇을 할지" 같은 상태 전이를 StateTree 에셋에서
//   그린다. 실제 동작(경로 이동, 스팟 행동, 부딪힘 반응 애니메이션)은 지금처럼 액터가 한다.
//   태스크는 "지금은 이 상태다"를 선언하고 끝났는지만 본다.
//
// [왜 Mass StateTree인가]
//   액터마다 StateTree 컴포넌트를 붙이면 행인 수만큼 틱이 돈다. Mass StateTree는 신호가 올 때만
//   그 엔티티의 트리를 실행한다. UMassSubNpcBehaviorSignalProcessor가 행인 상태가 바뀌었을 때와
//   걷는 중 일정 간격으로만 신호를 보내므로 평소 비용이 거의 없다.
//
// [에셋 만드는 법] Docs/CrowdStateTree.md 참고. 스키마는 "Mass Behavior", 스포너의 Crowd State Tree에 지정.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassStateTreeTypes.h"
#include "NPC/SubNpc/SubNpcMassProcessors.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "SubNpcMassStateTree.generated.h"


class UMassSignalSubsystem;

/** 신호를 보낼지 정하려고 기억하는 직전 상태 */
USTRUCT()
struct FSubNpcBehaviorFragment : public FMassFragment
{
	GENERATED_BODY()

	ESubNpcActivity LastActivity = ESubNpcActivity::Pooled;

	/** 걷는 중 마지막으로 신호를 보낸 뒤 지난 시간 (초) */
	float TimeSinceSignal = 0.f;
};

// ---------------------------------------------------------------------------
// 걷기
// ---------------------------------------------------------------------------

USTRUCT()
struct FMassSubNpcWalkTaskData
{
	GENERATED_BODY()

	/** 이만큼(cm) 걸을 때마다 근처 스팟에 들를지 판정을 요청한다. 0이면 스팟에 들르지 않는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0.0"))
	float CheckDistance = 150.f;

	UPROPERTY()
	FVector LastLocation = FVector::ZeroVector;

	UPROPERTY()
	float Travelled = 0.f;
};

/**
 * 경로를 따라 걷는다 (이동은 액터·이동 프로세서가 한다).
 *  - CheckDistance만큼 걸을 때마다 스팟 들르기 판정을 요청한다 (판정은 액터가 게임 스레드에서)
 *  - 걷는 상태가 아니게 되면(스팟에 들어감, 부딪힘 반응) Failed → 루트에서 다시 고르게 한다
 */
USTRUCT(meta = (DisplayName = "행인: 걷기"))
struct FMassSubNpcWalkTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMassSubNpcWalkTaskData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	TStateTreeExternalDataHandle<FSubNpcAgentFragment> AgentHandle;
};

// ---------------------------------------------------------------------------
// 상태가 유지되는 동안 대기
// ---------------------------------------------------------------------------

USTRUCT()
struct FMassSubNpcWaitActivityTaskData
{
	GENERATED_BODY()

	/** 이 상태인 동안 Running. 상태가 바뀌면 Succeeded (스팟 행동·부딪힘 반응이 끝났을 때) */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	ESubNpcActivity Activity = ESubNpcActivity::AtSpot;
};

USTRUCT(meta = (DisplayName = "행인: 상태가 끝날 때까지 대기"))
struct FMassSubNpcWaitActivityTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMassSubNpcWaitActivityTaskData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	TStateTreeExternalDataHandle<FSubNpcAgentFragment> AgentHandle;
};

// ---------------------------------------------------------------------------
// 조건
// ---------------------------------------------------------------------------

USTRUCT()
struct FMassSubNpcActivityConditionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Parameter")
	ESubNpcActivity Activity = ESubNpcActivity::Walking;
};

/** 행인이 지금 이 상태인가. 루트에서 상태를 고르는 진입 조건으로 쓴다 */
USTRUCT(meta = (DisplayName = "행인: 현재 상태가"))
struct FMassSubNpcActivityCondition : public FMassStateTreeConditionBase
{
	GENERATED_BODY()

	using FInstanceDataType = FMassSubNpcActivityConditionData;

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	TStateTreeExternalDataHandle<FSubNpcAgentFragment> AgentHandle;
};

// ---------------------------------------------------------------------------
// 신호
// ---------------------------------------------------------------------------

/**
 * 행인 StateTree를 깨운다. Mass StateTree는 신호를 받은 엔티티만 실행한다.
 *  - 행인 상태(걷기/스팟/반응)가 바뀌었을 때
 *  - 걷는 중이면 일정 간격마다 (걷기 태스크가 걸은 거리를 확인하도록)
 */
UCLASS()
class FEBRICULA_API UMassSubNpcBehaviorSignalProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSubNpcBehaviorSignalProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
