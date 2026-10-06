// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인 이동의 Mass 1단계.
//
// [하는 일]
//   걷는 행인의 경로 이동 계산(ASubNpcCharacter::ComputePathStep)을 워커 스레드에서 여러 명 동시에 돌리고,
//   결과 적용(ApplyPathStep - 위치 설정·경로 갈아타기·스팟 판정)만 게임 스레드에서 한다.
//   예전에는 행인마다 자기 액터 틱에서 계산부터 적용까지 게임 스레드로 했다.
//
// [아직 액터에 남아 있는 것]
//   이동 상태(진행도·차선·속도)는 그대로 액터 멤버에 있다. 엔티티는 액터를 가리키기만 한다.
//   스팟·반응·대화·일행·회피(Director)도 지금 코드 그대로다. 단계적으로 옮긴다:
//     1) 이동 계산 병렬화 (이 파일)   2) 회피·겹침 금지   3) StateTree   4) 표현 LOD(인스턴스드 메시)
//
// [켜고 끄기]
//   crowd.Mass 1(기본) / 0 - 0이면 예전처럼 액터 틱이 이동한다. crowd.Bench로 전후를 비교한다.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassProcessor.h"
#include "SubNpcMassProcessors.generated.h"

class ASubNpcCharacter;

/** 이 엔티티가 움직이는 행인 액터 */
USTRUCT()
struct FSubNpcAgentFragment : public FMassFragment
{
	GENERATED_BODY()

	TWeakObjectPtr<ASubNpcCharacter> Actor;

	/**
	 * 마지막 걸음 이후 쌓인 시간 (초).
	 * 액터 틱 주기(LOD: 가까움 매 프레임, 중간 0.1초, 멀리 0.3초)를 그대로 지키려고 쓴다.
	 * 프로세서는 매 프레임 돌지만, 이 값이 액터 틱 주기를 넘을 때만 한 걸음 계산한다.
	 */
	float TimeSinceStep = 0.f;
};

/**
 * 걷는 행인의 경로 이동을 병렬로 계산하고 게임 스레드에서 적용한다.
 *
 * 이 프로세서는 게임 스레드에서 실행되고(bRequiresGameThreadExecution), 안에서 ParallelFor로
 * 워커 스레드에 나눠 준다. 그래서 계산하는 동안 게임 스레드의 다른 코드(액터 틱, Director)가
 * 같은 액터를 동시에 건드릴 일이 없다.
 *
 * ParallelForEachEntityChunk를 쓰지 않는 이유: 프래그먼트가 작아 행인 100명이 청크 하나에
 * 다 들어가서, 청크 단위 병렬로는 일이 한 스레드에 몰린다. 엔티티 단위로 나눠야 실제로 퍼진다.
 */
UCLASS()
class FEBRICULA_API UMassSubNpcPathFollowProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassSubNpcPathFollowProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
