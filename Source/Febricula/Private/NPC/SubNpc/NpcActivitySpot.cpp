// [skydodo] 2026-09-01 작성 (최종 수정 2026-10-07)
// 요약: 행인 활동 지점 구현 - 행동 선택, 칸/줄 자리 점유 관리
// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcActivitySpot.h"

#include "Components/ArrowComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "NPC/SubNpc/SubNpcCharacter.h"

ANpcActivitySpot::ANpcActivitySpot()
{
	// 스팟은 게임에서는 순수 데이터 액터다. 점유 관리도 NPC 쪽에서 호출해주므로 틱이 필요 없다.
	// 에디터 뷰포트에서만 틱해서 칸·줄 자리를 그리고, 게임이 시작되면 첫 틱에 스스로 틱을 끈다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SpotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SpotRoot"));
	SetRootComponent(SpotRoot);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SpotRoot);
	FacingArrow->ArrowSize = 1.5f;
	FacingArrow->SetHiddenInGame(true);
}

void ANpcActivitySpot::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨 종료/언로드 시 남은 참조를 확실히 끊는다.
	// 스팟이 NPC를, NPC가 스팟을 서로 붙잡고 있으면 풀 회수 후에도 메모리가 안 풀린다.
	Occupants.Reset();
	WaitQueue.Reset();
	LastUseTime.Reset();
	CurrentOccupants = 0;

	Super::EndPlay(EndPlayReason);
}

bool ANpcActivitySpot::PickActivity(FNpcActivityOption& OutOption, int32& OutIndex) const
{
	OutIndex = INDEX_NONE;

	if (Activities.Num() == 0)
	{
		return false;
	}

	// 1) 고정 확률 행동. 주사위를 한 번만 굴리고 확률을 누적해서 구간으로 나눈다.
	//    행동마다 따로 굴리면 뒤쪽 행동은 앞에서 안 뽑혀야만 기회가 와서 적힌 확률보다 덜 나온다.
	//    지금 못 하는 독점 행동은 구간을 차지하지 않고, 그 몫은 아래 가중치 선택으로 넘어간다.
	const float Roll = FMath::FRand();
	float Accumulated = 0.f;
	for (int32 i = 0; i < Activities.Num(); ++i)
	{
		if (Activities[i].Chance <= 0.f || !IsActivityAvailable(i))
		{
			continue;
		}

		Accumulated += Activities[i].Chance;
		if (Roll < Accumulated)
		{
			OutIndex = i;
			break;
		}
	}

	// 2) 안 뽑혔으면 나머지(Chance 0) 행동을 가중치로 고른다.
	if (OutIndex == INDEX_NONE)
	{
		TArray<float> Weights;
		TArray<int32> Indices;
		Weights.Reserve(Activities.Num());
		Indices.Reserve(Activities.Num());
		for (int32 i = 0; i < Activities.Num(); ++i)
		{
			if (Activities[i].Chance <= 0.f && IsActivityAvailable(i))
			{
				Weights.Add(Activities[i].Weight);
				Indices.Add(i);
			}
		}

		const int32 Picked = NpcWeightedPick::PickIndex(Weights);
		if (Picked == INDEX_NONE)
		{
			return false;
		}
		OutIndex = Indices[Picked];
	}

	OutOption = Activities[OutIndex];
	return true;
}

bool ANpcActivitySpot::IsActivityAvailable(int32 ActivityIndex) const
{
	if (!Activities.IsValidIndex(ActivityIndex))
	{
		return false;
	}

	const FNpcActivityOption& Option = Activities[ActivityIndex];
	if (!Option.bExclusive)
	{
		return true;
	}

	// 같은 행동을 하는 사람이 있으면 안 된다. 다른 행동이라도 애니메이션이 같으면
	// 나란히 같은 포즈가 나오므로 똑같이 막는다.
	for (const FNpcSpotOccupant& Occupant : Occupants)
	{
		if (!Occupant.Npc.IsValid())
		{
			continue;
		}

		if (Occupant.ActivityIndex == ActivityIndex)
		{
			return false;
		}

		if (!Option.AnimId.IsNone()
			&& Activities.IsValidIndex(Occupant.ActivityIndex)
			&& Activities[Occupant.ActivityIndex].AnimId == Option.AnimId)
		{
			return false;
		}
	}

	return true;
}

FVector ANpcActivitySpot::GetSlotCenter(int32 SlotIndex) const
{
	// 칸들을 스팟 중심 기준으로 좌우 대칭으로 늘어놓는다.
	const float Centered = SlotIndex - (MaxOccupants - 1) * 0.5f;
	return GetActorLocation() + GetActorRightVector() * (Centered * SlotSpacing);
}

void ANpcActivitySpot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 게임에서는 할 일이 없다.
	const UWorld* World = GetWorld();
	if (!World || World->IsGameWorld())
	{
		SetActorTickEnabled(false);
		return;
	}

#if WITH_EDITORONLY_DATA
	if (bDrawSlotsInEditor)
	{
		DrawSpotDebug();
	}
#endif
}

void ANpcActivitySpot::DrawSpotDebug(float Lifetime) const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 액터 높이는 캡슐 중심(지면 + 88)이라 발밑 근처로 내려 그린다.
	const FVector Down(0.f, 0.f, -80.f);
	const FVector Up(1.f, 0.f, 0.f);
	const FVector Side(0.f, 1.f, 0.f);
	constexpr float BodyRadius = 34.f;   // 행인 캡슐 반지름. 원끼리 겹치면 몸도 겹친다

	for (int32 Slot = 0; Slot < MaxOccupants; ++Slot)
	{
		const FVector C = GetSlotCenter(Slot) + Down;
		DrawDebugCircle(World, C, BodyRadius, 20, FColor::Yellow, false, Lifetime, 0, 2.f, Up, Side, false);
		DrawDebugString(World, C + FVector(0.f, 0.f, 30.f), FString::Printf(TEXT("칸 %d"), Slot), nullptr, FColor::Yellow, Lifetime);
	}

	for (int32 Index = 0; Index < MaxQueueLength; ++Index)
	{
		const FVector Q = GetQueueLocation(Index) + Down;
		DrawDebugCircle(World, Q, BodyRadius, 20, FColor::Orange, false, Lifetime, 0, 2.f, Up, Side, false);
		DrawDebugString(World, Q + FVector(0.f, 0.f, 30.f), FString::Printf(TEXT("줄 %d"), Index + 1), nullptr, FColor::Orange, Lifetime);
	}

	// 일행 대기 자리 (일행 들르기를 켠 스팟만). 가장 많이 기다리는 5명 무리(4명 대기) 기준으로 그린다.
	if (bAllowGroupVisit)
	{
		const FVector WaitCenter = GetGroupWaitCenter() + Down;
		DrawDebugLine(World, GetActorLocation() + Down, WaitCenter, FColor::Green, false, Lifetime, 0, 1.f);
		DrawDebugString(World, WaitCenter + FVector(0.f, 0.f, 50.f), TEXT("일행 대기"), nullptr, FColor::Green, Lifetime);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector W = GetGroupWaitLocation(Index, 4) + Down;
			DrawDebugCircle(World, W, BodyRadius, 20, FColor::Green, false, Lifetime, 0, 2.f, Up, Side, false);
		}
	}

	// 행인이 바라볼 방향 (스팟 정면)
	const FVector Center = GetActorLocation() + Down;
	DrawDebugDirectionalArrow(World, Center, Center + GetActorForwardVector() * 120.f, 40.f, FColor::Cyan, false, Lifetime, 0, 3.f);
#endif
}

FVector ANpcActivitySpot::GetStandLocation(int32 SlotIndex) const
{
	FVector Location = GetSlotCenter(SlotIndex);

	if (StandJitterRadius > 0.f)
	{
		// 매번 정확히 같은 자리에 서면 "복제 붙여넣기"처럼 보인다.
		// 다만 옆 칸을 침범하면 다시 겹치므로 칸 간격의 일부로만 흔든다.
		const float Jitter = FMath::Min(StandJitterRadius, (SlotSpacing - 70.f) * 0.5f + 5.f);
		const FVector2D Offset = FMath::RandPointInCircle(Jitter);
		Location += FVector(Offset.X, Offset.Y, 0.f);
	}

	return Location;
}

bool ANpcActivitySpot::IsOnReuseCooldown(const ASubNpcCharacter* Npc) const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	const float* Last = LastUseTime.Find(Npc);
	return Last && (Now - *Last < ReuseCooldown);
}

void ANpcActivitySpot::CleanQueue()
{
	WaitQueue.RemoveAll([](const TWeakObjectPtr<ASubNpcCharacter>& Weak) { return !Weak.IsValid(); });
}

bool ANpcActivitySpot::CanEnterNow(const ASubNpcCharacter* Npc) const
{
	if (!Npc || !HasRoom())
	{
		return false;
	}

	// 줄의 맨 앞(살아 있는 사람 중 첫 번째)만 들어갈 수 있다. 줄이 비었으면 누구든.
	for (const TWeakObjectPtr<ASubNpcCharacter>& Weak : WaitQueue)
	{
		if (Weak.IsValid())
		{
			return Weak.Get() == Npc;
		}
	}
	return true;
}

bool ANpcActivitySpot::TryJoinQueue(ASubNpcCharacter* Npc)
{
	if (!Npc || MaxQueueLength <= 0 || IsOnReuseCooldown(Npc))
	{
		return false;
	}

	CleanQueue();
	if (WaitQueue.Contains(Npc))
	{
		return true;
	}
	if (WaitQueue.Num() >= MaxQueueLength)
	{
		return false;
	}

	WaitQueue.Add(Npc);
	return true;
}

void ANpcActivitySpot::LeaveQueue(ASubNpcCharacter* Npc)
{
	WaitQueue.RemoveAll([Npc](const TWeakObjectPtr<ASubNpcCharacter>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == Npc;
	});
}

int32 ANpcActivitySpot::GetQueueLength() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<ASubNpcCharacter>& Weak : WaitQueue)
	{
		Count += Weak.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 ANpcActivitySpot::GetQueueIndex(const ASubNpcCharacter* Npc) const
{
	// 죽은 참조는 세지 않는다. 앞사람이 회수되면 그 자리를 바로 당겨 선다.
	int32 Index = 0;
	for (const TWeakObjectPtr<ASubNpcCharacter>& Weak : WaitQueue)
	{
		if (!Weak.IsValid())
		{
			continue;
		}
		if (Weak.Get() == Npc)
		{
			return Index;
		}
		++Index;
	}
	return INDEX_NONE;
}

FVector ANpcActivitySpot::GetQueueLocation(int32 Index) const
{
	// 줄은 칸 바로 다음에서 시작해 QueueDirection으로 늘어선다. 칸은 스팟 오른쪽으로 나란히 놓이므로
	// 줄이 옆으로 뻗을 때만 칸 폭만큼 비켜서 시작한다 (뒤로 뻗으면 칸 바로 뒤부터).
	const FVector Dir = GetActorTransform().TransformVectorNoScale(QueueDirection).GetSafeNormal2D();
	const float SlotsHalfWidth = (MaxOccupants - 1) * 0.5f * SlotSpacing;
	const float Sideways = FMath::Abs(FVector::DotProduct(Dir, GetActorRightVector().GetSafeNormal2D()));
	return GetActorLocation() + Dir * (SlotsHalfWidth * Sideways + QueueSpacing * (Index + 1));
}

FVector ANpcActivitySpot::GetGroupWaitCenter() const
{
	return GetActorTransform().TransformPosition(GroupWaitCenter);
}

FVector ANpcActivitySpot::GetGroupWaitLocation(int32 Index, int32 Count) const
{
	// [변경] 예전에는 스팟 옆으로 일렬(최대 4칸, 3.4m)로 늘어서서 줄·길과 가까워졌고, 5명 무리가 기다리면 병목이 됐다.
	// 이제 사람이 배치한 중심(GroupWaitCenter)을 둘러싸고 작은 원으로 선다 - 자리를 적게 차지하고 모여 이야기하는 무리로 보인다.
	const FVector Center = GetGroupWaitCenter();
	if (Count <= 1)
	{
		return Center;
	}
	// 옆 사람과 어깨가 닿지 않게: 원 위 이웃 간 거리(현) 2R·sin(π/N) ≥ 75cm
	constexpr float MinNeighborDistance = 75.f;
	const float Radius = FMath::Max(GroupWaitRadius, MinNeighborDistance * 0.5f / FMath::Sin(PI / Count));
	// 첫 자리는 스팟 쪽 - 들른 친구가 보이는 쪽부터 채운다
	FVector ToSpot = GetActorLocation() - Center;
	ToSpot.Z = 0.f;
	const float BaseAngle = ToSpot.IsNearlyZero() ? 0.f : FMath::Atan2(ToSpot.Y, ToSpot.X);
	const float Angle = BaseAngle + 2.f * PI * Index / Count;
	return Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
}

FVector ANpcActivitySpot::GetLookAtLocation(const FNpcActivityOption& Option) const
{
	if (Option.LookAtOffset.IsNearlyZero())
	{
		// 지정이 없으면 스팟이 향한 방향 앞쪽을 본다.
		return GetActorLocation() + GetActorForwardVector() * 200.f;
	}

	// 오프셋은 스팟 로컬 기준이므로 액터 회전을 태워서 월드로 옮긴다.
	return GetActorTransform().TransformPosition(Option.LookAtOffset);
}

bool ANpcActivitySpot::TryOccupy(ASubNpcCharacter* Npc, int32 ActivityIndex, int32& OutSlotIndex)
{
	OutSlotIndex = 0;

	// [줄 서기] 먼저 와서 기다리는 사람이 있으면 그 사람이 먼저다.
	// 예전에는 줄이 없어서, 앞에서 막혀 서 있던 사람 대신 뒤늦게 온 사람이 먼저 들어갔다.
	if (!CanEnterNow(Npc))
	{
		return false;
	}

	// 고른 뒤 등록하기 전에 다른 행인이 같은 독점 행동을 잡았을 수 있으므로 다시 확인한다.
	if (!IsActivityAvailable(ActivityIndex))
	{
		return false;
	}

	// 같은 행인이 루프를 돌며 계속 같은 자리에 멈추면 금방 티가 난다.
	if (IsOnReuseCooldown(Npc))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// 비어 있는 칸 중 가장 앞 번호를 준다.
	Occupants.RemoveAll([](const FNpcSpotOccupant& Occupant) { return !Occupant.Npc.IsValid(); });
	for (int32 Slot = 0; Slot < MaxOccupants; ++Slot)
	{
		const bool bTaken = Occupants.ContainsByPredicate(
			[Slot](const FNpcSpotOccupant& Occupant) { return Occupant.SlotIndex == Slot; });
		if (!bTaken)
		{
			OutSlotIndex = Slot;
			break;
		}
	}

	LeaveQueue(Npc);

	FNpcSpotOccupant& Added = Occupants.AddDefaulted_GetRef();
	Added.Npc = Npc;
	Added.ActivityIndex = ActivityIndex;
	Added.SlotIndex = OutSlotIndex;
	LastUseTime.Add(Npc, Now);
	++CurrentOccupants;

	// 죽은 참조가 쌓이지 않도록 등록할 때마다 한 번씩 청소한다.
	// (NPC가 풀로 회수되면서 ReleaseOccupancy를 못 부른 경우에 대한 보험)
	Occupants.RemoveAll([](const FNpcSpotOccupant& Occupant) { return !Occupant.Npc.IsValid(); });
	CurrentOccupants = Occupants.Num();

	for (auto It = LastUseTime.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	return true;
}

void ANpcActivitySpot::ReleaseOccupancy(ASubNpcCharacter* Npc)
{
	if (!Npc)
	{
		return;
	}

	Occupants.RemoveAll([Npc](const FNpcSpotOccupant& Occupant)
	{
		return !Occupant.Npc.IsValid() || Occupant.Npc.Get() == Npc;
	});

	CurrentOccupants = Occupants.Num();
}
