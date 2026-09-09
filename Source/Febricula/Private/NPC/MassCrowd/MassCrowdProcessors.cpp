// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MassCrowd/MassCrowdProcessors.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "MassCommonTypes.h"
#include "NPC/MassCrowd/MassCrowdFragments.h"
#include "NPC/MassCrowd/MassCrowdPathSubsystem.h"

// ===========================================================================
// 경로 추종
// ===========================================================================

UMassCrowdPathFollowProcessor::UMassCrowdPathFollowProcessor()
	: WalkQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);

	// 이동은 Behavior 그룹에서 돈다. 표현(Representation) 갱신보다 앞서야
	// 이번 프레임의 위치가 그대로 그려진다.
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Behavior;
	ExecutionOrder.ExecuteAfter.Add(TEXT("MassCrowdAvoidanceProcessor"));
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
void UMassCrowdPathFollowProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#else
void UMassCrowdPathFollowProcessor::ConfigureQueries()
#endif
{
	// 읽고 쓸 조각을 미리 선언한다.
	// Mass는 이 선언을 보고 "서로 겹치지 않는 프로세서"를 병렬로 돌린다.
	// 선언하지 않은 데이터를 만지면 그 순간 병렬 실행의 안전 보장이 깨진다.
	WalkQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	WalkQuery.AddRequirement<FMassCrowdPathFragment>(EMassFragmentAccess::ReadWrite);
	WalkQuery.AddRequirement<FMassCrowdAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	WalkQuery.AddConstSharedRequirement<FMassCrowdSharedConfig>();

	// 걷는 중인 엔티티만 대상. 스팟에 있거나 반응 중인 개체는 아예 순회하지 않는다.
	WalkQuery.AddTagRequirement<FMassCrowdWalkingTag>(EMassFragmentPresence::All);
	WalkQuery.AddTagRequirement<FMassCrowdPendingDespawnTag>(EMassFragmentPresence::None);

	// 경로 캐시는 읽기 전용으로만 쓴다. 여러 스레드가 동시에 읽어도 안전하다.
	WalkQuery.AddSubsystemRequirement<UMassCrowdPathSubsystem>(EMassFragmentAccess::ReadOnly);
}

void UMassCrowdPathFollowProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// ParallelForEachEntityChunk - 청크 단위로 워커 스레드에 뿌린다.
	// 스레드에 배분되는 단위가 "개체"가 아니라 "청크"라는 점이 중요하다.
	// 개체 수가 늘어도 스레드 수와 동기화 지점은 그대로다.
	WalkQuery.ParallelForEachEntityChunk(Context,
		[](FMassExecutionContext& Ctx)
		{
			const UMassCrowdPathSubsystem& PathSys = Ctx.GetSubsystemChecked<UMassCrowdPathSubsystem>();
			const FMassCrowdSharedConfig& Config = Ctx.GetConstSharedFragment<FMassCrowdSharedConfig>();

			const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
			const TArrayView<FMassCrowdPathFragment> Paths = Ctx.GetMutableFragmentView<FMassCrowdPathFragment>();
			const TArrayView<FMassCrowdAvoidanceFragment> Avoids = Ctx.GetMutableFragmentView<FMassCrowdAvoidanceFragment>();

			const float DeltaTime = Ctx.GetDeltaTimeSeconds();
			const int32 Num = Ctx.GetNumEntities();

			for (int32 i = 0; i < Num; ++i)
			{
				FMassCrowdPathFragment& Path = Paths[i];
				if (Path.PathIndex == INDEX_NONE || !PathSys.IsValidPath(Path.PathIndex))
				{
					continue;
				}

				// 1) 회피가 밀어둔 값을 반영하고 지운다.
				//    쓰는 쪽(회피 프로세서)과 읽는 쪽(여기)이 다른 단계라 레이스가 없다.
				FMassCrowdAvoidanceFragment& Avoid = Avoids[i];
				if (!FMath::IsNearlyZero(Avoid.PendingNudge))
				{
					const float HalfWidth = PathSys.GetLaneHalfWidth(Path.PathIndex);
					const float Nudge = FMath::Clamp(Avoid.PendingNudge, -Config.MaxAvoidOffset, Config.MaxAvoidOffset);
					Path.LaneOffset = FMath::Clamp(Path.LaneOffset + Nudge * DeltaTime * 3.f, -HalfWidth, HalfWidth);
					Avoid.PendingNudge = 0.f;
				}

				// 2) 전진. 이동은 float 하나를 더하는 것으로 끝난다.
				Path.Distance += Path.Speed * DeltaTime * (Path.bReversed ? -1.f : 1.f);

				const float Length = PathSys.GetPathLength(Path.PathIndex);
				const bool bArrived = (!Path.bReversed && Path.Distance >= Length) ||
					(Path.bReversed && Path.Distance <= 0.f);

				if (bArrived)
				{
					// 3) 다음 경로 고르기. 엔티티마다 자기 시드를 써서 전역 난수를 피한다.
					//    전역 FMath::Rand는 내부 상태를 공유하므로 병렬에서 쓰면 안 된다.
					const FMassEntityHandle Entity = Ctx.GetEntity(i);
					FRandomStream Stream(GetTypeHash(Entity) ^ static_cast<int32>(Ctx.GetWorld()->TimeSeconds * 1000.f));

					int32 NextPath = INDEX_NONE;
					bool bNextReversed = false;

					if (PathSys.PickNextPath(Path.PathIndex, Stream, NextPath, bNextReversed))
					{
						Path.PathIndex = NextPath;
						Path.bReversed = bNextReversed;
						Path.Distance = bNextReversed ? PathSys.GetPathLength(NextPath) : 0.f;

						const float HalfWidth = PathSys.GetLaneHalfWidth(NextPath);
						Path.LaneOffset = FMath::Clamp(
							Path.LaneOffset + Stream.FRandRange(-30.f, 30.f), -HalfWidth, HalfWidth);
					}
					else if (PathSys.IsDespawnAtEnd(Path.PathIndex))
					{
						// 퇴장구. 여기서 바로 지우지 않고 태그만 붙인다.
						// 순회 도중의 구조 변경은 지연 명령으로만 허용된다.
						Ctx.Defer().AddTag<FMassCrowdPendingDespawnTag>(Entity);
						continue;
					}
					else
					{
						// 막다른 길이면 방향을 돌려 왔던 길로 되돌아간다.
						Path.bReversed = !Path.bReversed;
						Path.Distance = FMath::Clamp(Path.Distance, 0.f, Length);
						Path.Distance += Path.bReversed ? -20.f : 20.f;
					}
				}

				// 4) 위치·회전 갱신.
				const FVector Location = PathSys.GetLocationAt(Path.PathIndex, Path.Distance, Path.LaneOffset);
				const FVector Direction = PathSys.GetDirectionAt(Path.PathIndex, Path.Distance) *
					(Path.bReversed ? -1.f : 1.f);

				FTransform& Xf = Transforms[i].GetMutableTransform();
				Xf.SetLocation(Location);
				Xf.SetRotation(Direction.ToOrientationQuat());
			}
		});
}

// ===========================================================================
// 소프트 회피
// ===========================================================================

UMassCrowdAvoidanceProcessor::UMassCrowdAvoidanceProcessor()
	: AvoidQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Behavior;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
void UMassCrowdAvoidanceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#else
void UMassCrowdAvoidanceProcessor::ConfigureQueries()
#endif
{
	AvoidQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AvoidQuery.AddRequirement<FMassCrowdPathFragment>(EMassFragmentAccess::ReadOnly);
	AvoidQuery.AddRequirement<FMassCrowdAvoidanceFragment>(EMassFragmentAccess::ReadWrite);
	AvoidQuery.AddConstSharedRequirement<FMassCrowdSharedConfig>();
	AvoidQuery.AddTagRequirement<FMassCrowdWalkingTag>(EMassFragmentPresence::All);
}

void UMassCrowdAvoidanceProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 청크 안에서만 서로 비교한다.
	//
	// 청크 경계를 넘는 이웃은 놓치지만, 그 대가로 다른 청크의 데이터를 건드리지 않아
	// 병렬 실행이 그대로 성립한다. 겹쳐 보이는 것만 줄이면 되는 목적에는 충분하고,
	// 정확한 이웃 탐색이 필요해지면 Mass의 공간 해시(MassNavigation)로 교체하면 된다.
	AvoidQuery.ParallelForEachEntityChunk(Context,
		[](FMassExecutionContext& Ctx)
		{
			const FMassCrowdSharedConfig& Config = Ctx.GetConstSharedFragment<FMassCrowdSharedConfig>();

			const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();
			const TConstArrayView<FMassCrowdPathFragment> Paths = Ctx.GetFragmentView<FMassCrowdPathFragment>();
			const TArrayView<FMassCrowdAvoidanceFragment> Avoids = Ctx.GetMutableFragmentView<FMassCrowdAvoidanceFragment>();

			const int32 Num = Ctx.GetNumEntities();
			const float AvoidSq = FMath::Square(Config.AvoidDistance);

			for (int32 i = 0; i < Num; ++i)
			{
				const FVector LocA = Transforms[i].GetTransform().GetLocation();

				for (int32 j = i + 1; j < Num; ++j)
				{
					// 같은 경로 위에 있어야 좌우로 밀어서 벌릴 수 있다.
					if (Paths[i].PathIndex != Paths[j].PathIndex)
					{
						continue;
					}

					const float DistSq = FVector::DistSquared(LocA, Transforms[j].GetTransform().GetLocation());
					if (DistSq >= AvoidSq || DistSq < KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const float Overlap = 1.f - (FMath::Sqrt(DistSq) / Config.AvoidDistance);
					const float Push = Overlap * Config.AvoidDistance * Config.AvoidStrength;

					// 오프셋이 작은 쪽을 더 왼쪽으로, 큰 쪽을 더 오른쪽으로.
					const float Sign = (Paths[i].LaneOffset <= Paths[j].LaneOffset) ? -1.f : 1.f;

					Avoids[i].PendingNudge += Sign * Push;
					Avoids[j].PendingNudge -= Sign * Push;
				}
			}
		});
}

// ===========================================================================
// 퇴장
// ===========================================================================

UMassCrowdDespawnProcessor::UMassCrowdDespawnProcessor()
	: DespawnQuery(*this)
{
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::AllNetModes);
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Behavior;
	ExecutionOrder.ExecuteAfter.Add(TEXT("MassCrowdPathFollowProcessor"));
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4)
void UMassCrowdDespawnProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
#else
void UMassCrowdDespawnProcessor::ConfigureQueries()
#endif
{
	DespawnQuery.AddTagRequirement<FMassCrowdPendingDespawnTag>(EMassFragmentPresence::All);
}

void UMassCrowdDespawnProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// 여기는 병렬로 돌리지 않는다. 구조를 바꾸는 명령을 쌓는 단계라
	// 순차로 처리하는 편이 안전하고, 대상 수도 적다.
	DespawnQuery.ForEachEntityChunk(Context,
		[](FMassExecutionContext& Ctx)
		{
			const int32 Num = Ctx.GetNumEntities();
			for (int32 i = 0; i < Num; ++i)
			{
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		});
}
