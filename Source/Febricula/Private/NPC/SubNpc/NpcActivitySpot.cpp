// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcActivitySpot.h"

#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "NPC/SubNpc/SubNpcCharacter.h"

ANpcActivitySpot::ANpcActivitySpot()
{
	// 스팟은 순수 데이터 액터다. 점유 관리도 NPC 쪽에서 호출해주므로 틱이 필요 없다.
	PrimaryActorTick.bCanEverTick = false;

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

FVector ANpcActivitySpot::GetStandLocation() const
{
	FVector Location = GetActorLocation();

	if (StandJitterRadius > 0.f)
	{
		// 매번 정확히 같은 자리에 서면 "복제 붙여넣기"처럼 보인다.
		const FVector2D Offset = FMath::RandPointInCircle(StandJitterRadius);
		Location += FVector(Offset.X, Offset.Y, 0.f);
	}

	return Location;
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

bool ANpcActivitySpot::TryOccupy(ASubNpcCharacter* Npc, int32 ActivityIndex)
{
	if (!Npc || !HasRoom())
	{
		return false;
	}

	// 고른 뒤 등록하기 전에 다른 행인이 같은 독점 행동을 잡았을 수 있으므로 다시 확인한다.
	if (!IsActivityAvailable(ActivityIndex))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// 같은 행인이 루프를 돌며 계속 같은 자리에 멈추면 금방 티가 난다.
	if (const float* Last = LastUseTime.Find(Npc))
	{
		if (Now - *Last < ReuseCooldown)
		{
			return false;
		}
	}

	FNpcSpotOccupant& Added = Occupants.AddDefaulted_GetRef();
	Added.Npc = Npc;
	Added.ActivityIndex = ActivityIndex;
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
