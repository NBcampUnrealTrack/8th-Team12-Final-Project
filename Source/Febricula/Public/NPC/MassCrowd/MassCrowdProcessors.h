// Fill out your copyright notice in the Description page of Project Settings.
//
// 군중 프로세서들.
//
// [프로세서가 액터 Tick과 다른 점]
//   액터 Tick은 "개체 하나가 자기 할 일을 다 한다". 프로세서는 반대로
//   "한 가지 일을 모든 개체에 대해 한꺼번에 한다".
//
//   그래서 순서가 중요해진다. 회피가 값을 쓰고 -> 이동이 그 값을 읽는 식으로
//   단계가 나뉘고, 각 단계 안에서는 개체끼리 서로를 건드리지 않는다.
//   이것이 병렬 실행이 성립하는 조건이다.
//
// [실행 순서]
//   Avoidance(회피 계산)  ->  PathFollow(이동)  ->  Despawn(퇴장 처리)
//   ExecutionOrder로 선언하며, 같은 그룹 안에서는 Mass가 의존성을 보고 정렬한다.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassCrowdProcessors.generated.h"

/**
 * 경로를 따라 전진시키고 위치를 갱신한다.
 *
 * 이 프로세서가 군중 비용의 대부분을 차지하므로 병렬로 돌린다.
 * 안에서 액터를 만지지 않는 것이 병렬 실행의 전제다 - 경로 데이터는
 * UMassCrowdPathSubsystem이 미리 구워둔 배열에서만 읽는다.
 */
UCLASS()
class FEBRICULA_API UMassCrowdPathFollowProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdPathFollowProcessor();

protected:
	// UE 5.4에서 시그니처가 바뀌었다. 5.8은 아래쪽(EntityManager를 받는 형태)을 쓴다.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#else
	virtual void ConfigureQueries() override;
#endif

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery WalkQuery;
};

/**
 * 가까운 개체끼리 좌우로 밀어낸다.
 *
 * 액터 버전에서는 개체가 자기 Tick에서 이웃을 직접 읽어 오프셋을 바꿨다.
 * 그러면 "읽는 도중에 쓰인다"가 되어 병렬화가 불가능하다.
 * 여기서는 이 프로세서가 밀어낼 양만 조각에 적어두고, 실제 반영은
 * 다음 단계인 이동 프로세서가 한다. 쓰는 쪽과 읽는 쪽이 분리된다.
 *
 * 공간 분할 없이 도는 대신 대상을 화면 근처로 한정한다. 정확도보다
 * "겹쳐 보이지 않게" 하는 것이 목적이라 이 정도면 충분하다.
 */
UCLASS()
class FEBRICULA_API UMassCrowdAvoidanceProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdAvoidanceProcessor();

protected:
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#else
	virtual void ConfigureQueries() override;
#endif

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery AvoidQuery;
};

/**
 * 퇴장 표시가 붙은 엔티티를 실제로 제거한다.
 *
 * 제거는 엔티티 배열 구조를 바꾸는 일이라 순회 도중에 하면 안 된다.
 * Mass는 이런 변경을 "지연 명령"으로 모았다가 안전한 시점에 한꺼번에 적용한다.
 * 액터 버전에서 회수를 예약만 하고 Director가 나중에 처리하게 바꾼 것과 같은 원리인데,
 * 여기서는 프레임워크가 그 구조를 강제한다.
 */
UCLASS()
class FEBRICULA_API UMassCrowdDespawnProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UMassCrowdDespawnProcessor();

protected:
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
#else
	virtual void ConfigureQueries() override;
#endif

	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

	FMassEntityQuery DespawnQuery;
};
