// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcCrowdPath.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "NPC/SubNpc/NpcActivitySpot.h"

int32 NpcWeightedPick::PickIndex(const TArray<float>& Weights)
{
	float Total = 0.f;
	for (const float W : Weights)
	{
		if (W > 0.f)
		{
			Total += W;
		}
	}

	if (Total <= 0.f)
	{
		return INDEX_NONE;
	}

	float Roll = FMath::FRandRange(0.f, Total);
	for (int32 i = 0; i < Weights.Num(); ++i)
	{
		if (Weights[i] <= 0.f)
		{
			continue;
		}
		Roll -= Weights[i];
		if (Roll <= 0.f)
		{
			return i;
		}
	}

	// 부동소수 오차로 끝까지 떨어지지 않는 경우를 위한 폴백
	for (int32 i = Weights.Num() - 1; i >= 0; --i)
	{
		if (Weights[i] > 0.f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

ANpcCrowdPath::ANpcCrowdPath()
{
	// 경로 액터 자체는 아무것도 안 한다. 데이터만 들고 있는다.
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);

	// 행인 경로는 평면 위 곡선이라 닫힌 루프가 기본값이면 곤란하다.
	Spline->SetClosedLoop(false);
}

float ANpcCrowdPath::GetPathLength() const
{
	return Spline ? Spline->GetSplineLength() : 0.f;
}

FVector ANpcCrowdPath::GetLocationAtDistance(float Distance, float LaneOffset) const
{
	if (!Spline)
	{
		return GetActorLocation();
	}

	const float Clamped = FMath::Clamp(Distance, 0.f, Spline->GetSplineLength());
	const FVector Base = Spline->GetLocationAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);

	if (FMath::IsNearlyZero(LaneOffset))
	{
		return Base;
	}

	// 진행 방향의 오른쪽 벡터로 밀어서 차선을 만든다.
	// 스플라인의 Right 벡터를 그대로 쓰면 롤(Roll)이 들어간 구간에서 위아래로 튀므로,
	// 수평 성분만 뽑아 쓴다.
	FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);
	Right.Z = 0.f;
	if (!Right.Normalize())
	{
		return Base;
	}

	return Base + Right * LaneOffset;
}

FVector ANpcCrowdPath::GetDirectionAtDistance(float Distance) const
{
	if (!Spline)
	{
		return GetActorForwardVector();
	}

	const float Clamped = FMath::Clamp(Distance, 0.f, Spline->GetSplineLength());
	FVector Dir = Spline->GetDirectionAtDistanceAlongSpline(Clamped, ESplineCoordinateSpace::World);
	Dir.Z = 0.f;
	if (!Dir.Normalize())
	{
		Dir = GetActorForwardVector();
	}
	return Dir;
}

float ANpcCrowdPath::FindClosestPointOnPath(const FVector& WorldLocation, float& OutDistanceAlongPath) const
{
	OutDistanceAlongPath = 0.f;

	if (!Spline)
	{
		return FVector::DistSquared(WorldLocation, GetActorLocation());
	}

	// 스플라인의 입력 키(구간 보간 파라미터)를 먼저 구한 뒤 거리로 환산한다.
	const float Key = Spline->FindInputKeyClosestToWorldLocation(WorldLocation);
	OutDistanceAlongPath = FMath::Clamp(
		Spline->GetDistanceAlongSplineAtSplineInputKey(Key), 0.f, Spline->GetSplineLength());

	const FVector Closest = Spline->GetLocationAtDistanceAlongSpline(
		OutDistanceAlongPath, ESplineCoordinateSpace::World);

	return FVector::DistSquared(WorldLocation, Closest);
}

ANpcCrowdPath* ANpcCrowdPath::PickNextPath(bool& bOutReversed) const
{
	bOutReversed = false;

	if (Exits.Num() == 0)
	{
		return nullptr;
	}

	// 유효한 출구만 모아서 가중 랜덤. 참조가 끊긴 항목(레벨에서 지운 경로)은 조용히 건너뛴다.
	TArray<float> Weights;
	TArray<int32> ValidIndices;
	Weights.Reserve(Exits.Num());
	ValidIndices.Reserve(Exits.Num());

	for (int32 i = 0; i < Exits.Num(); ++i)
	{
		if (Exits[i].NextPath && Exits[i].Weight > 0.f)
		{
			Weights.Add(Exits[i].Weight);
			ValidIndices.Add(i);
		}
	}

	const int32 Picked = NpcWeightedPick::PickIndex(Weights);
	if (Picked == INDEX_NONE)
	{
		return nullptr;
	}

	const FNpcPathExit& Exit = Exits[ValidIndices[Picked]];
	bOutReversed = Exit.bEnterReversed;
	return Exit.NextPath;
}

#if WITH_EDITOR
void ANpcCrowdPath::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서만: 출구 연결을 화살표로 그려 망 구조를 눈으로 확인한다.
	// (실행 중에는 아무 비용도 없다)
	if (!Spline || !GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}

	const FVector End = Spline->GetLocationAtDistanceAlongSpline(
		Spline->GetSplineLength(), ESplineCoordinateSpace::World);

	for (const FNpcPathExit& Exit : Exits)
	{
		if (!Exit.NextPath || !Exit.NextPath->GetSpline())
		{
			continue;
		}

		const USplineComponent* NextSpline = Exit.NextPath->GetSpline();
		const float TargetDist = Exit.bEnterReversed ? NextSpline->GetSplineLength() : 0.f;
		const FVector Start = NextSpline->GetLocationAtDistanceAlongSpline(TargetDist, ESplineCoordinateSpace::World);

		DrawDebugDirectionalArrow(GetWorld(), End, Start, 60.f, FColor::Green, false, 5.f, 0, 4.f);
	}
}
#endif
