// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcCharacter.h"

void USubNpcDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LodCursor = 0;
	LodTimer = 0.f;
}

void USubNpcDirectorSubsystem::Deinitialize()
{
	// [메모리] 레벨을 떠날 때 풀에 잠들어 있던 개체를 명시적으로 파괴한다.
	// 숨겨둔 액터는 눈에 안 보여서 "레벨 전환 후에도 메모리가 안 줄어드는" 원인이 되기 쉽다.
	for (ASubNpcCharacter* Npc : PooledNpcs)
	{
		if (IsValid(Npc))
		{
			Npc->Destroy();
		}
	}
	PooledNpcs.Reset();

	for (ASubNpcCharacter* Npc : ActiveNpcs)
	{
		if (IsValid(Npc))
		{
			Npc->Destroy();
		}
	}
	ActiveNpcs.Reset();

	ArchetypeTable = nullptr;

	Super::Deinitialize();
}

TStatId USubNpcDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USubNpcDirectorSubsystem, STATGROUP_Tickables);
}

void USubNpcDirectorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	LodTimer += DeltaTime;
	if (LodTimer < LodUpdateInterval)
	{
		return;
	}
	LodTimer = 0.f;

	UpdateLodSlice();
}

// ---------------------------------------------------------------------------
// 풀
// ---------------------------------------------------------------------------

void USubNpcDirectorSubsystem::PrewarmPool(TSubclassOf<ASubNpcCharacter> NpcClass, int32 Count)
{
	UWorld* World = GetWorld();
	if (!World || !NpcClass || Count <= 0)
	{
		return;
	}

	FActorSpawnParameters Params;
	// 스폰 위치가 지형에 겹쳐도 무조건 생성돼야 한다 (어차피 바로 숨길 것이므로).
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	// 화면 밖 아주 먼 곳에서 만든다.
	const FVector Nowhere(0.f, 0.f, -100000.f);

	for (int32 i = 0; i < Count; ++i)
	{
		ASubNpcCharacter* Npc = World->SpawnActor<ASubNpcCharacter>(NpcClass, Nowhere, FRotator::ZeroRotator, Params);
		if (!Npc)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Crowd] 풀 생성 실패 (%d/%d). 클래스 설정을 확인하세요."), i, Count);
			break;
		}

		Npc->DeactivateToPool();
		PooledNpcs.Add(Npc);
	}

	UE_LOG(LogTemp, Log, TEXT("[Crowd] 풀 준비 완료: %d명"), PooledNpcs.Num());
}

void USubNpcDirectorSubsystem::SetArchetypeTable(UDataTable* InTable)
{
	ArchetypeTable = InTable;
}

bool USubNpcDirectorSubsystem::PickArchetype(FSubNpcArchetypeRow& OutRow) const
{
	if (!ArchetypeTable)
	{
		// 테이블이 없어도 기본값으로 돌아가야 한다 (세팅 안 됐다고 군중이 통째로 사라지면 곤란).
		OutRow = FSubNpcArchetypeRow();
		return true;
	}

	TArray<float> Weights;
	TArray<const FSubNpcArchetypeRow*> Rows;

	for (const TPair<FName, uint8*>& Pair : ArchetypeTable->GetRowMap())
	{
		const FSubNpcArchetypeRow* Row = reinterpret_cast<const FSubNpcArchetypeRow*>(Pair.Value);
		if (Row && Row->SpawnWeight > 0.f)
		{
			Rows.Add(Row);
			Weights.Add(Row->SpawnWeight);
		}
	}

	const int32 Picked = NpcWeightedPick::PickIndex(Weights);
	if (Picked == INDEX_NONE)
	{
		OutRow = FSubNpcArchetypeRow();
		return true;
	}

	OutRow = *Rows[Picked];
	return true;
}

ASubNpcCharacter* USubNpcDirectorSubsystem::SpawnOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed)
{
	if (!Path)
	{
		return nullptr;
	}

	// 하드 캡. 이 한 줄이 메모리·GPU 예산을 지키는 마지막 방어선이다.
	if (ActiveNpcs.Num() >= MaxActiveNpcs || PooledNpcs.Num() == 0)
	{
		return nullptr;
	}

	ASubNpcCharacter* Npc = PooledNpcs.Pop(EAllowShrinking::No);
	if (!IsValid(Npc))
	{
		return nullptr;
	}

	FSubNpcArchetypeRow Archetype;
	PickArchetype(Archetype);

	Npc->ActivateFromPool(Archetype, Path, StartDistance, bReversed);
	ActiveNpcs.Add(Npc);

	return Npc;
}

TArray<ASubNpcCharacter*> USubNpcDirectorSubsystem::SpawnGroupOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed, int32 GroupSize)
{
	TArray<ASubNpcCharacter*> Group;
	if (!Path || GroupSize <= 0)
	{
		return Group;
	}

	ASubNpcCharacter* Leader = SpawnOnPath(Path, StartDistance, bReversed);
	if (!Leader)
	{
		return Group;
	}
	Group.Add(Leader);

	const float HalfWidth = Path->GetLaneHalfWidth();

	for (int32 i = 1; i < GroupSize; ++i)
	{
		ASubNpcCharacter* Member = SpawnOnPath(Path, StartDistance, bReversed);
		if (!Member)
		{
			break;
		}

		// 나란히 걷되 살짝 어긋나게. 완전히 일직선이면 행진처럼 보인다.
		const float SideOffset = FMath::Clamp(
			(i % 2 == 0 ? 1.f : -1.f) * FMath::FRandRange(50.f, 90.f), -HalfWidth, HalfWidth);
		const float Gap = FMath::FRandRange(0.f, 40.f) * i;

		Member->SetGroupLeader(Leader, Gap, SideOffset);
		Group.Add(Member);
	}

	return Group;
}

void USubNpcDirectorSubsystem::ReturnToPool(ASubNpcCharacter* Npc)
{
	if (!IsValid(Npc))
	{
		return;
	}

	ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);

	Npc->DeactivateToPool();

	// 중복 반납 방지. 같은 개체가 풀에 두 번 들어가면 두 명이 동시에 활성화된다.
	PooledNpcs.AddUnique(Npc);
}

void USubNpcDirectorSubsystem::RegisterNpc(ASubNpcCharacter* Npc)
{
	if (!IsValid(Npc) || Npc->IsPooled())
	{
		return;
	}
	ActiveNpcs.AddUnique(Npc);
}

void USubNpcDirectorSubsystem::UnregisterNpc(ASubNpcCharacter* Npc)
{
	ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);
	PooledNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);
}

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

bool USubNpcDirectorSubsystem::GetViewerLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 카메라가 아니라 폰 위치를 기준으로 삼는다. 3인칭에서 카메라가 뒤로 빠져 있어도
	// "플레이어 주변"이 기준이어야 군중 밀도가 자연스럽다.
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			OutLocation = Pawn->GetActorLocation();
			return true;
		}
	}
	return false;
}

void USubNpcDirectorSubsystem::UpdateLodSlice()
{
	FVector ViewerLocation;
	if (!GetViewerLocation(ViewerLocation))
	{
		return;
	}

	if (ActiveNpcs.Num() == 0)
	{
		LodCursor = 0;
		return;
	}

	// 거리 제곱으로 비교해서 제곱근 연산을 피한다. 인원이 많을수록 차이가 난다.
	const float NearSq = FMath::Square(NearDistance);
	const float MidSq = FMath::Square(MidDistance);
	const float FarSq = FMath::Square(FarDistance);
	const float RecycleSq = FMath::Square(RecycleDistance);

	const int32 Count = FMath::Min(LodSliceSize, ActiveNpcs.Num());

	// 회수 대상은 순회 도중에 배열을 건드리지 않도록 모았다가 나중에 처리한다.
	TArray<ASubNpcCharacter*> ToRecycle;

	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveNpcs.Num() == 0)
		{
			break;
		}

		LodCursor = (LodCursor + 1) % ActiveNpcs.Num();
		ASubNpcCharacter* Npc = ActiveNpcs[LodCursor];

		if (!IsValid(Npc))
		{
			ActiveNpcs.RemoveAtSwap(LodCursor, 1, EAllowShrinking::No);
			continue;
		}

		const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), ViewerLocation);

		if (DistSq > RecycleSq)
		{
			ToRecycle.Add(Npc);
			continue;
		}

		ESubNpcLodBand Band;
		if (DistSq <= NearSq)       { Band = ESubNpcLodBand::Near; }
		else if (DistSq <= MidSq)   { Band = ESubNpcLodBand::Mid; }
		else if (DistSq <= FarSq)   { Band = ESubNpcLodBand::Far; }
		else                        { Band = ESubNpcLodBand::Culled; }

		// 실제로 화면에 안 그려지고 있으면 한 단계 낮춘다.
		// WasRecentlyRendered는 렌더 스레드가 채워주는 값이라 공짜에 가깝다.
		if (bDemoteWhenNotRendered && Band != ESubNpcLodBand::Culled)
		{
			if (!Npc->WasRecentlyRendered(0.5f))
			{
				Band = static_cast<ESubNpcLodBand>(FMath::Min(
					static_cast<uint8>(Band) + 1, static_cast<uint8>(ESubNpcLodBand::Culled)));
			}
		}

		Npc->ApplyLodBand(Band);
	}

	for (ASubNpcCharacter* Npc : ToRecycle)
	{
		ReturnToPool(Npc);
	}

	// 정원이 줄어들었으면 초과분을 먼 쪽부터 천천히 회수한다.
	// 한 프레임에 몰아서 처리하면 눈앞에서 사람들이 사라지는 게 보인다.
	if (ActiveNpcs.Num() > MaxActiveNpcs)
	{
		ASubNpcCharacter* Farthest = nullptr;
		float FarthestDistSq = -1.f;

		for (ASubNpcCharacter* Npc : ActiveNpcs)
		{
			if (!IsValid(Npc))
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), ViewerLocation);
			if (DistSq > FarthestDistSq)
			{
				FarthestDistSq = DistSq;
				Farthest = Npc;
			}
		}

		if (Farthest)
		{
			ReturnToPool(Farthest);
		}
	}
}
