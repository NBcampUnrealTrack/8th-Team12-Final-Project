// [skydodo] 2026-09-01 작성 (최종 수정 2026-10-08)
// 요약: 행인 경로 구간 구현 - 차선 위치 계산, 갈림길 가중치 선택, 경로 디버그 표시
// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcCrowdPath.h"

#include "Components/SplineComponent.h"
#include "Components/ArrowComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameMode/TimeSubsystem.h"

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
	// 경로 액터는 게임에서는 아무것도 안 한다(데이터만 들고 있는다).
	// 에디터 뷰포트에서만 틱해서 길 폭을 그리고, 게임이 시작되면 첫 틱에 스스로 틱을 끈다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);

	// 행인 경로는 평면 위 곡선이라 닫힌 루프가 기본값이면 곤란하다.
	Spline->SetClosedLoop(false);
}

void ANpcCrowdPath::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 게임에서는 할 일이 없다. 게임 중 경로 표시는 crowd.DrawPaths가 맡는다.
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		SetActorTickEnabled(false);
		return;
	}

#if WITH_EDITORONLY_DATA
	if (bDrawLanesInEditor)
	{
		// 에디터에서는 Director의 추월 여유값을 볼 수 없으므로 같은 값을 쓴다 (SubNpcCrowdCVars::SqueezeOverflow).
		DrawLaneDebug(/*SqueezeOverflow=*/45.f);
	}
#endif
}

// [skydodo] 2026-10-13 리팩토링: 상태를 안 바꾸므로 const (const인 PickNextPath 안에서 부른다).
// 에디터 프리뷰처럼 GameInstance가 없을 때 GetGameInstance() null 역참조하던 것도 막고, 시간을 모르면 열린 것으로 본다
bool ANpcCrowdPath::IsOpen() const
{
	if (ClosedSchedules.IsEmpty())
	{
		return true;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UTimeSubsystem* TimeSubsystem = GameInstance ? GameInstance->GetSubsystem<UTimeSubsystem>() : nullptr;
	if (!TimeSubsystem)
	{
		return true;
	}
	return !ClosedSchedules.HasTagExact(TimeSubsystem->GetNowScheduleTag());
}

void ANpcCrowdPath::DrawLaneDebug(float SqueezeOverflow, float Lifetime) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const float Length = GetPathLength();
	if (!World || !Spline || Length <= 1.f)
	{
		return;
	}

	constexpr float Step = 50.f;            // 선을 이 간격(cm)으로 잘라 그린다
	constexpr float ArrowEvery = 300.f;     // 방향 화살표 간격 (cm)
	const FVector Lift(0.f, 0.f, -80.f);    // 경로 높이는 캡슐 중심이라 발밑 근처로 내려 그린다
	const float SqueezeWidth = LaneHalfWidth + SqueezeOverflow;
	const FColor SqueezeColor(110, 110, 110);

	FVector PrevC, PrevL, PrevR, PrevSL, PrevSR;
	float NextArrow = 0.f;
	for (float D = 0.f; ; D = FMath::Min(D + Step, Length))
	{
		const FVector C = GetLocationAtDistance(D, 0.f) + Lift;
		const FVector L = GetLocationAtDistance(D, -LaneHalfWidth) + Lift;
		const FVector R = GetLocationAtDistance(D, LaneHalfWidth) + Lift;
		const FVector SL = GetLocationAtDistance(D, -SqueezeWidth) + Lift;
		const FVector SR = GetLocationAtDistance(D, SqueezeWidth) + Lift;

		if (D > 0.f)
		{
			DrawDebugLine(World, PrevC, C, FColor::White, false, Lifetime, 0, 3.f);
			DrawDebugLine(World, PrevL, L, FColor::Green, false, Lifetime, 0, 2.f);
			DrawDebugLine(World, PrevR, R, FColor::Green, false, Lifetime, 0, 2.f);
			DrawDebugLine(World, PrevSL, SL, SqueezeColor, false, Lifetime, 0, 1.f);
			DrawDebugLine(World, PrevSR, SR, SqueezeColor, false, Lifetime, 0, 1.f);
		}

		if (D >= NextArrow)
		{
			const FVector Dir = GetDirectionAtDistance(D);
			DrawDebugDirectionalArrow(World, C, C + Dir * 80.f, 40.f,
				bSpawnEntry ? FColor::Emerald : FColor::Cyan, false, Lifetime, 0, 3.f);
			NextArrow += ArrowEvery;
		}

		PrevC = C; PrevL = L; PrevR = R; PrevSL = SL; PrevSR = SR;
		if (D >= Length)
		{
			break;
		}
	}

	// 퇴장구(여기서 사라진다)는 끝에 빨간 X를 그린다.
	if (bDespawnAtEnd)
	{
		const FVector End = GetLocationAtDistance(Length, 0.f) + Lift;
		DrawDebugLine(World, End + FVector(-40.f, -40.f, 0.f), End + FVector(40.f, 40.f, 0.f), FColor::Red, false, Lifetime, 0, 4.f);
		DrawDebugLine(World, End + FVector(-40.f, 40.f, 0.f), End + FVector(40.f, -40.f, 0.f), FColor::Red, false, Lifetime, 0, 4.f);
	}
#endif
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

float ANpcCrowdPath::GetLaneProgressScale(float Distance, float LaneOffset, float Along) const
{
	// 차선이 중심에 가까우면 계산할 필요가 없다.
	if (!Spline || FMath::Abs(LaneOffset) < 5.f)
	{
		return 1.f;
	}

	// 짧은 구간을 실제로 재 본다. 곡률을 직접 구하는 것보다 스플라인 모양과 정확히 맞는다.
	constexpr float Probe = 30.f;
	constexpr float MinScale = 0.5f;   // 바깥쪽: 중심선보다 최대 2배까지 길다
	constexpr float MaxScale = 2.5f;   // 안쪽: 모서리 반경이 차선보다 작으면 실제 이동이 0에 가까워진다
	const float Length = Spline->GetSplineLength();
	const float D0 = FMath::Clamp(Distance, 0.f, Length);
	const float D1 = FMath::Clamp(Distance + (Along >= 0.f ? Probe : -Probe), 0.f, Length);
	const float CenterStep = FMath::Abs(D1 - D0);
	if (CenterStep < 1.f)
	{
		return 1.f;
	}

	const float LaneStep = FVector::Dist2D(GetLocationAtDistance(D1, LaneOffset), GetLocationAtDistance(D0, LaneOffset));
	return FMath::Clamp(CenterStep / FMath::Max(LaneStep, 1.f), MinScale, MaxScale);
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
		if (Exits[i].NextPath && Exits[i].Weight > 0.f && Exits[i].NextPath->IsOpen())
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
void ANpcCrowdPath::RebuildDirectionArrows()
{
	// 이전 화살표를 지우고 다시 만든다. 점을 옮기거나 방향을 뒤집을 때마다 갱신된다.
	for (UArrowComponent* Arrow : DirectionArrows)
	{
		if (IsValid(Arrow))
		{
			Arrow->DestroyComponent();
		}
	}
	DirectionArrows.Reset();

	if (!bShowDirectionArrows || !Spline)
	{
		return;
	}

	const float Length = Spline->GetSplineLength();
	if (Length < 1.f)
	{
		return;
	}

	// 너무 많이 만들면 에디터가 무거워진다. 40개로 제한한다.
	const int32 Count = FMath::Clamp(FMath::FloorToInt(Length / FMath::Max(50.f, ArrowSpacing)), 1, 40);

	for (int32 i = 0; i < Count; ++i)
	{
		const float Dist = Length * (static_cast<float>(i) + 0.5f) / Count;

		const FVector Loc = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);

		// bSpawnReversed면 실제로 걷는 방향이 뒤집히므로 화살표도 뒤집는다.
		FVector Dir = Spline->GetDirectionAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
		if (bSpawnReversed)
		{
			Dir = -Dir;
		}

		UArrowComponent* Arrow = NewObject<UArrowComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient);
		if (!Arrow)
		{
			continue;
		}

		Arrow->SetupAttachment(Spline);
		Arrow->SetIsVisualizationComponent(true);
		Arrow->bIsEditorOnly = true;
		Arrow->SetHiddenInGame(true);
		Arrow->ArrowColor = bSpawnEntry ? FColor::Green : FColor::Cyan;
		Arrow->ArrowSize = 1.2f;
		Arrow->ArrowLength = 60.f;
		Arrow->bTreatAsASprite = false;
		Arrow->RegisterComponent();

		// 등록한 뒤에 월드 좌표를 세팅해야 부모 트랜스폼이 반영된다.
		Arrow->SetWorldLocationAndRotation(Loc + FVector(0.f, 0.f, 20.f), Dir.Rotation());

		DirectionArrows.Add(Arrow);
	}

	// 출구 연결도 화살표로 남긴다. 경로 끝에서 다음 경로 진입점을 가리킨다.
	// 주황색이라 진행 방향(하늘색)과 구분된다.
	const FVector End = Spline->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::World);

	for (const FNpcPathExit& Exit : Exits)
	{
		const ANpcCrowdPath* Next = Exit.NextPath;
		if (!Next || !Next->GetSpline())
		{
			continue;
		}

		const USplineComponent* NextSpline = Next->GetSpline();
		const float TargetDist = Exit.bEnterReversed ? NextSpline->GetSplineLength() : 0.f;
		const FVector NextEntry = NextSpline->GetLocationAtDistanceAlongSpline(TargetDist, ESplineCoordinateSpace::World);

		FVector ToNext = NextEntry - End;
		if (ToNext.IsNearlyZero())
		{
			continue;
		}

		UArrowComponent* Arrow = NewObject<UArrowComponent>(this, NAME_None, RF_Transient | RF_TextExportTransient);
		if (!Arrow)
		{
			continue;
		}

		Arrow->SetupAttachment(Spline);
		Arrow->SetIsVisualizationComponent(true);
		Arrow->bIsEditorOnly = true;
		Arrow->SetHiddenInGame(true);
		Arrow->ArrowColor = FColor::Orange;
		Arrow->ArrowSize = 1.6f;
		Arrow->ArrowLength = FMath::Max(60.f, ToNext.Size());
		Arrow->bTreatAsASprite = false;
		Arrow->RegisterComponent();
		Arrow->SetWorldLocationAndRotation(End + FVector(0.f, 0.f, 40.f), ToNext.Rotation());

		DirectionArrows.Add(Arrow);
	}
}

void ANpcCrowdPath::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 에디터에서만: 출구 연결을 화살표로 그려 망 구조를 눈으로 확인한다.
	// (실행 중에는 아무 비용도 없다)
	if (!Spline || !GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}

	RebuildDirectionArrows();
}
#endif
