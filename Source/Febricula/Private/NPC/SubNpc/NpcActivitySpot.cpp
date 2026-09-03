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

bool ANpcActivitySpot::PickActivity(FNpcActivityOption& OutOption) const
{
	if (Activities.Num() == 0)
	{
		return false;
	}

	TArray<float> Weights;
	Weights.Reserve(Activities.Num());
	for (const FNpcActivityOption& Option : Activities)
	{
		Weights.Add(Option.Weight);
	}

	const int32 Picked = NpcWeightedPick::PickIndex(Weights);
	if (Picked == INDEX_NONE)
	{
		return false;
	}

	OutOption = Activities[Picked];
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

bool ANpcActivitySpot::TryOccupy(ASubNpcCharacter* Npc)
{
	if (!Npc || !HasRoom())
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

	Occupants.Add(Npc);
	LastUseTime.Add(Npc, Now);
	++CurrentOccupants;

	// 죽은 참조가 쌓이지 않도록 등록할 때마다 한 번씩 청소한다.
	// (NPC가 풀로 회수되면서 ReleaseOccupancy를 못 부른 경우에 대한 보험)
	Occupants.RemoveAll([](const TWeakObjectPtr<ASubNpcCharacter>& Weak) { return !Weak.IsValid(); });
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

	Occupants.RemoveAll([Npc](const TWeakObjectPtr<ASubNpcCharacter>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == Npc;
	});

	CurrentOccupants = Occupants.Num();
}
