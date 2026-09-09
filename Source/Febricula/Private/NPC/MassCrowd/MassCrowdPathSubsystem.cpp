// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MassCrowd/MassCrowdPathSubsystem.h"

#include "Components/SplineComponent.h"
#include "EngineUtils.h"
#include "NPC/SubNpc/NpcActivitySpot.h"
#include "NPC/SubNpc/NpcCrowdPath.h"

void UMassCrowdPathSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 레벨의 액터가 전부 자리를 잡은 뒤에 구워야 한다.
	// BeginPlay보다 이른 시점(Initialize)에 하면 스트리밍으로 늦게 들어온 경로를 놓친다.
	RebuildPathCache();
}

void UMassCrowdPathSubsystem::RebuildPathCache()
{
	check(IsInGameThread());

	BakedPaths.Reset();
	BakedSpots.Reset();
	SpawnEntryPaths.Reset();
	PathLookup.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1차 순회 - 경로를 굽고 인덱스를 부여한다.
	// 출구 연결은 모든 경로가 인덱스를 받은 뒤에야 풀 수 있으므로 두 번 돈다.
	TArray<const ANpcCrowdPath*> PathActors;
	for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
	{
		const ANpcCrowdPath* Path = *It;
		if (!Path)
		{
			continue;
		}

		const int32 Index = BakedPaths.Num();
		PathLookup.Add(Path, Index);
		PathActors.Add(Path);

		FMassCrowdBakedPath Baked;
		BakeOne(*Path, Baked);
		BakedPaths.Add(MoveTemp(Baked));
	}

	// 2차 순회 - 출구 연결과 스팟을 인덱스로 바꾼다.
	for (int32 i = 0; i < PathActors.Num(); ++i)
	{
		const ANpcCrowdPath* Path = PathActors[i];
		FMassCrowdBakedPath& Baked = BakedPaths[i];

		for (const FNpcPathExit& Exit : Path->GetExits())
		{
			const int32* Found = PathLookup.Find(Exit.NextPath.Get());
			if (Found && Exit.Weight > 0.f)
			{
				Baked.ExitPathIndices.Add(*Found);
				Baked.ExitWeights.Add(Exit.Weight);
				Baked.ExitReversed.Add(Exit.bEnterReversed ? 1 : 0);
			}
		}

		for (ANpcActivitySpot* Spot : Path->GetActivitySpots())
		{
			if (!Spot)
			{
				continue;
			}

			FMassCrowdBakedSpot BakedSpot;
			BakedSpot.StandLocation = Spot->GetStandLocation();
			BakedSpot.LookAtLocation = Spot->GetActorLocation();   // 기본값. 행동별 오프셋은 게임 스레드에서 처리
			BakedSpot.EngageChance = Spot->GetEngageChance();
			BakedSpot.MaxOccupants = Spot->GetMaxOccupants();
			BakedSpot.SourceActor = Spot;

			Baked.SpotIndices.Add(BakedSpots.Num());
			BakedSpots.Add(BakedSpot);
		}

		if (Baked.bSpawnEntry)
		{
			SpawnEntryPaths.Add(i);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MassCrowd] 경로 %d개 / 스팟 %d개를 구웠습니다 (등장구 %d개)."),
		BakedPaths.Num(), BakedSpots.Num(), SpawnEntryPaths.Num());
}

void UMassCrowdPathSubsystem::BakeOne(const ANpcCrowdPath& Path, FMassCrowdBakedPath& Out) const
{
	const USplineComponent* Spline = Path.GetSpline();
	if (!Spline)
	{
		return;
	}

	Out.Length = Spline->GetSplineLength();
	Out.SampleStep = FMath::Max(10.f, SampleStep);
	Out.LaneHalfWidth = Path.GetLaneHalfWidth();
	Out.bSpawnEntry = Path.IsSpawnEntry();
	Out.bDespawnAtEnd = Path.IsDespawnAtEnd();

	// 끝점을 포함해야 하므로 +1. 길이가 0이면 점 하나만 둔다.
	const int32 NumSamples = FMath::Max(2, FMath::CeilToInt(Out.Length / Out.SampleStep) + 1);

	Out.Points.Reserve(NumSamples);
	Out.Directions.Reserve(NumSamples);
	Out.RightVectors.Reserve(NumSamples);

	for (int32 i = 0; i < NumSamples; ++i)
	{
		const float Dist = FMath::Min(i * Out.SampleStep, Out.Length);

		Out.Points.Add(Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World));
		Out.Directions.Add(Spline->GetDirectionAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World));
		Out.RightVectors.Add(Spline->GetRightVectorAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World));
	}

	// 마지막 샘플이 정확히 끝점을 가리키도록 보정한다.
	// 반올림 때문에 조금 못 미치면 도착 판정이 계속 어긋난다.
	if (Out.Points.Num() > 0)
	{
		Out.Points.Last() = Spline->GetLocationAtDistanceAlongSpline(Out.Length, ESplineCoordinateSpace::World);
	}
}

// ---------------------------------------------------------------------------
// 조회 (워커 스레드 안전 - 액터도 컴포넌트도 만지지 않는다)
// ---------------------------------------------------------------------------

FVector UMassCrowdPathSubsystem::GetLocationAt(int32 PathIndex, float Distance, float LaneOffset) const
{
	if (!BakedPaths.IsValidIndex(PathIndex))
	{
		return FVector::ZeroVector;
	}

	const FMassCrowdBakedPath& P = BakedPaths[PathIndex];
	if (P.Points.Num() == 0)
	{
		return FVector::ZeroVector;
	}

	const float Clamped = FMath::Clamp(Distance, 0.f, P.Length);
	const float Exact = Clamped / P.SampleStep;
	const int32 i0 = FMath::Clamp(FMath::FloorToInt(Exact), 0, P.Points.Num() - 1);
	const int32 i1 = FMath::Min(i0 + 1, P.Points.Num() - 1);
	const float Alpha = Exact - i0;

	const FVector Base = FMath::Lerp(P.Points[i0], P.Points[i1], Alpha);
	const FVector Right = FMath::Lerp(P.RightVectors[i0], P.RightVectors[i1], Alpha);

	return Base + Right * LaneOffset;
}

FVector UMassCrowdPathSubsystem::GetDirectionAt(int32 PathIndex, float Distance) const
{
	if (!BakedPaths.IsValidIndex(PathIndex))
	{
		return FVector::ForwardVector;
	}

	const FMassCrowdBakedPath& P = BakedPaths[PathIndex];
	if (P.Directions.Num() == 0)
	{
		return FVector::ForwardVector;
	}

	const float Clamped = FMath::Clamp(Distance, 0.f, P.Length);
	const float Exact = Clamped / P.SampleStep;
	const int32 i0 = FMath::Clamp(FMath::FloorToInt(Exact), 0, P.Directions.Num() - 1);
	const int32 i1 = FMath::Min(i0 + 1, P.Directions.Num() - 1);
	const float Alpha = Exact - i0;

	return FMath::Lerp(P.Directions[i0], P.Directions[i1], Alpha).GetSafeNormal();
}

float UMassCrowdPathSubsystem::GetPathLength(int32 PathIndex) const
{
	return BakedPaths.IsValidIndex(PathIndex) ? BakedPaths[PathIndex].Length : 0.f;
}

float UMassCrowdPathSubsystem::GetLaneHalfWidth(int32 PathIndex) const
{
	return BakedPaths.IsValidIndex(PathIndex) ? BakedPaths[PathIndex].LaneHalfWidth : 0.f;
}

bool UMassCrowdPathSubsystem::IsDespawnAtEnd(int32 PathIndex) const
{
	return BakedPaths.IsValidIndex(PathIndex) && BakedPaths[PathIndex].bDespawnAtEnd;
}

bool UMassCrowdPathSubsystem::PickNextPath(int32 PathIndex, FRandomStream& Stream, int32& OutNextPath, bool& bOutReversed) const
{
	OutNextPath = INDEX_NONE;
	bOutReversed = false;

	if (!BakedPaths.IsValidIndex(PathIndex))
	{
		return false;
	}

	const FMassCrowdBakedPath& P = BakedPaths[PathIndex];
	if (P.ExitPathIndices.Num() == 0)
	{
		return false;
	}

	float Total = 0.f;
	for (const float W : P.ExitWeights)
	{
		Total += W;
	}
	if (Total <= 0.f)
	{
		return false;
	}

	float Roll = Stream.FRandRange(0.f, Total);
	for (int32 i = 0; i < P.ExitPathIndices.Num(); ++i)
	{
		Roll -= P.ExitWeights[i];
		if (Roll <= 0.f)
		{
			OutNextPath = P.ExitPathIndices[i];
			bOutReversed = P.ExitReversed[i] != 0;
			return true;
		}
	}

	OutNextPath = P.ExitPathIndices.Last();
	bOutReversed = P.ExitReversed.Last() != 0;
	return true;
}

// ---------------------------------------------------------------------------
// 스팟 점유 - 원자적으로 처리해서 병렬에서도 정원을 넘지 않게 한다
// ---------------------------------------------------------------------------

bool UMassCrowdPathSubsystem::TryOccupySpot(int32 SpotIndex) const
{
	if (!BakedSpots.IsValidIndex(SpotIndex))
	{
		return false;
	}

	const FMassCrowdBakedSpot& Spot = BakedSpots[SpotIndex];

	// [핵심] "빈자리 확인"과 "차지하기"가 갈라지면 안 된다.
	// 두 단계로 나누면 병렬에서 둘 다 확인을 통과한 뒤 둘 다 차지해버린다.
	// compare_exchange로 한 번에 처리하면 하나만 성공한다.
	int32 Current = Spot.Occupancy.load(std::memory_order_relaxed);
	while (Current < Spot.MaxOccupants)
	{
		if (Spot.Occupancy.compare_exchange_weak(Current, Current + 1, std::memory_order_acq_rel))
		{
			return true;
		}
		// 실패하면 Current에 최신값이 들어오므로 다시 판정한다.
	}
	return false;
}

void UMassCrowdPathSubsystem::ReleaseSpot(int32 SpotIndex) const
{
	if (!BakedSpots.IsValidIndex(SpotIndex))
	{
		return;
	}

	const FMassCrowdBakedSpot& Spot = BakedSpots[SpotIndex];
	int32 Current = Spot.Occupancy.load(std::memory_order_relaxed);
	while (Current > 0)
	{
		if (Spot.Occupancy.compare_exchange_weak(Current, Current - 1, std::memory_order_acq_rel))
		{
			return;
		}
	}
}
