// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
// TActorIterator를 쓰려면 반드시 필요하다. 없으면 "정의되지 않은 class TActorIterator" 에러가 난다.
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcCharacter.h"

// ---------------------------------------------------------------------------
// 측정용 콘솔 변수
//
// Director는 월드 서브시스템이라 디테일 패널이 없다. 그래서 LOD나 회피를
// 에디터에서 껐다 켤 수가 없고, 성능 비교를 하려면 값을 고쳐 다시 빌드해야 했다.
// 재생 중에 한 줄로 토글할 수 있게 콘솔 변수로 뺀다.
//
//   crowd.LOD 0          거리 LOD를 끈다 (전원 최고 상세도) - LOD가 벌어주는 양 측정
//   crowd.Avoidance 0    소프트 회피를 끈다               - 회피 비용 측정
//   crowd.Stats 1        1초마다 활성/풀 인원을 로그로 찍는다
//
// 되돌릴 때는 값을 1로 주면 된다. ECVF_Cheat이라 배포 빌드에는 안 들어간다.
// ---------------------------------------------------------------------------
namespace SubNpcCrowdCVars
{
	static int32 EnableLod = 1;
	static FAutoConsoleVariableRef CVarEnableLod(
		TEXT("crowd.LOD"),
		EnableLod,
		TEXT("서브 NPC 거리 LOD. 0이면 전원을 Near로 고정한다 (측정용)."),
		ECVF_Cheat);

	static int32 EnableAvoidance = 1;
	static FAutoConsoleVariableRef CVarEnableAvoidance(
		TEXT("crowd.Avoidance"),
		EnableAvoidance,
		TEXT("서브 NPC 소프트 회피. 0이면 끈다 (측정용)."),
		ECVF_Cheat);

	static int32 PrintStats = 0;
	static FAutoConsoleVariableRef CVarPrintStats(
		TEXT("crowd.Stats"),
		PrintStats,
		TEXT("1이면 1초마다 활성/풀 인원을 로그로 찍는다."),
		ECVF_Cheat);
}


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

	// 회수 예약된 개체를 먼저 정리한다.
	// 순회 도중에 활성 배열이 바뀌지 않도록, 실제 회수는 항상 이 시점에만 일어난다.
	ProcessPendingRecycles();

	if (SubNpcCrowdCVars::PrintStats != 0)
	{
		StatsTimer += DeltaTime;
		if (StatsTimer >= 1.f)
		{
			StatsTimer = 0.f;
			UE_LOG(LogTemp, Log, TEXT("[Crowd] 활성 %d / 풀 %d / 상한 %d"),
				ActiveNpcs.Num(), PooledNpcs.Num(), MaxActiveNpcs);
		}
	}

	if (bEnableAvoidance && SubNpcCrowdCVars::EnableAvoidance != 0)
	{
		AvoidTimer += DeltaTime;
		if (AvoidTimer >= AvoidUpdateInterval)
		{
			AvoidTimer = 0.f;
			UpdateAvoidance();
		}
	}

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

	// [개선] 등장 지점이 이미 붐비면 경로를 따라 조금 밀어서 세운다.
	// 같은 자리에 겹쳐 등장하면 한 덩어리로 몰려다니고, 보기에도 바로 티가 난다.
	if (SpawnSpacing > 0.f)
	{
		const float PathLength = Path->GetPathLength();
		const float Step = SpawnSpacing;
		const float SpacingSq = FMath::Square(SpawnSpacing);

		// 최대 8칸까지만 밀어본다. 그래도 자리가 없으면 이번 등장은 포기한다.
		bool bFoundSlot = false;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const float Candidate = FMath::Clamp(
				StartDistance + (bReversed ? -1.f : 1.f) * Step * Attempt, 0.f, PathLength);

			const FVector CandidateLoc = Path->GetLocationAtDistance(Candidate, 0.f);

			bool bBlocked = false;
			for (const ASubNpcCharacter* Other : ActiveNpcs)
			{
				if (IsValid(Other) &&
					FVector::DistSquared(Other->GetActorLocation(), CandidateLoc) < SpacingSq)
				{
					bBlocked = true;
					break;
				}
			}

			if (!bBlocked)
			{
				StartDistance = Candidate;
				bFoundSlot = true;
				break;
			}
		}

		if (!bFoundSlot)
		{
			// 꺼낸 개체를 도로 넣어둔다. 다음 주기에 다시 시도된다.
			PooledNpcs.Add(Npc);
			return nullptr;
		}
	}

	FSubNpcArchetypeRow Archetype;
	PickArchetype(Archetype);

	Npc->ActivateFromPool(Archetype, Path, StartDistance, bReversed);
	ActiveNpcs.Add(Npc);

	return Npc;
}

ASubNpcCharacter* USubNpcDirectorSubsystem::SpawnAtNearestPath(const FVector& WorldLocation, float SearchRadius)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 레벨의 모든 경로 중 이 좌표에 가장 가까운 지점을 찾는다.
	// 경로 개수는 보통 수십 개 수준이라 매번 훑어도 문제없다
	// (매 프레임 부르는 함수가 아니라 스폰 시점에만 부르는 함수다).
	ANpcCrowdPath* BestPath = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	float BestDistanceAlong = 0.f;

	for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
	{
		float DistanceAlong = 0.f;
		const float DistSq = It->FindClosestPointOnPath(WorldLocation, DistanceAlong);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestDistanceAlong = DistanceAlong;
			BestPath = *It;
		}
	}

	if (!BestPath)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Crowd] 레벨에 NpcCrowdPath가 하나도 없어 스폰할 수 없습니다."));
		return nullptr;
	}

	if (SearchRadius > 0.f && BestDistSq > FMath::Square(SearchRadius))
	{
		// 지정한 위치에서 너무 먼 곳에 세우면 "엉뚱한 데서 튀어나온다"가 된다. 차라리 실패시킨다.
		UE_LOG(LogTemp, Verbose,
			TEXT("[Crowd] 지정 위치 %s 주변 %.0fcm 안에 경로가 없습니다."), *WorldLocation.ToString(), SearchRadius);
		return nullptr;
	}

	return SpawnOnPath(BestPath, BestDistanceAlong, /*bReversed=*/false);
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

void USubNpcDirectorSubsystem::ProcessPendingRecycles()
{
	// 뒤에서부터 순회한다. RemoveSingleSwap이 뒤 원소를 앞으로 당기므로
	// 앞에서부터 돌면 방금 당겨온 원소를 건너뛴다.
	for (int32 i = ActiveNpcs.Num() - 1; i >= 0; --i)
	{
		ASubNpcCharacter* Npc = ActiveNpcs[i];
		if (!IsValid(Npc))
		{
			ActiveNpcs.RemoveAtSwap(i, 1, EAllowShrinking::No);
			continue;
		}

		if (Npc->IsPendingRecycle())
		{
			Npc->ClearPendingRecycle();
			ReturnToPool(Npc);
		}
	}
}

void USubNpcDirectorSubsystem::UpdateAvoidance()
{
	FVector ViewerLocation;
	if (!GetViewerLocation(ViewerLocation))
	{
		return;
	}

	// 가까이 보이는 개체만 모은다. 여기가 회피 비용을 결정한다.
	TArray<ASubNpcCharacter*> Nearby;
	Nearby.Reserve(MaxAvoidancePairs);

	for (ASubNpcCharacter* Npc : ActiveNpcs)
	{
		if (!IsValid(Npc) || !Npc->IsWalkingOnPath())
		{
			continue;
		}

		const ESubNpcLodBand Band = Npc->GetLodBand();
		if (Band != ESubNpcLodBand::Near && Band != ESubNpcLodBand::Mid)
		{
			continue;
		}

		Nearby.Add(Npc);
		if (Nearby.Num() >= MaxAvoidancePairs)
		{
			break;
		}
	}

	const float AvoidSq = FMath::Square(AvoidDistance);

	for (int32 i = 0; i < Nearby.Num(); ++i)
	{
		ASubNpcCharacter* A = Nearby[i];
		const FVector LocA = A->GetActorLocation();

		for (int32 j = i + 1; j < Nearby.Num(); ++j)
		{
			ASubNpcCharacter* B = Nearby[j];

			// 같은 경로 위에 있는 개체끼리만 비교한다.
			// 다른 경로라면 좌우 오프셋을 밀어도 서로 멀어지지 않는다.
			if (A->GetCurrentPath() != B->GetCurrentPath())
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(LocA, B->GetActorLocation());
			if (DistSq >= AvoidSq || DistSq < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// 겹친 정도(0~1). 가까울수록 세게 민다.
			const float Overlap = 1.f - (FMath::Sqrt(DistSq) / AvoidDistance);
			const float Push = Overlap * AvoidDistance * AvoidStrength;

			// 차선 오프셋이 작은 쪽을 더 왼쪽으로, 큰 쪽을 더 오른쪽으로 민다.
			// 값이 같으면 인덱스로 갈라서 서로 반대 방향이 되게 한다.
			const float OffA = A->GetLaneOffset();
			const float OffB = B->GetLaneOffset();
			const float Sign = (OffA != OffB) ? (OffA < OffB ? -1.f : 1.f) : ((i % 2 == 0) ? -1.f : 1.f);

			A->AddAvoidanceNudge(Sign * Push);
			B->AddAvoidanceNudge(-Sign * Push);
		}
	}
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

	// [개선] 인원에 비례해 슬라이스를 키운다.
	// 고정 슬라이스면 인원이 늘수록 한 개체의 재검사 주기가 같이 늘어나서,
	// 플레이어가 달릴 때 LOD가 뒤늦게 따라온다.
	int32 SliceSize = LodSliceSize;
	if (LodFullSweepTime > 0.f)
	{
		const int32 SlicesPerSweep = FMath::Max(1, FMath::CeilToInt(LodFullSweepTime / LodUpdateInterval));
		SliceSize = FMath::Max(LodSliceSize, FMath::CeilToInt(static_cast<float>(ActiveNpcs.Num()) / SlicesPerSweep));
	}

	// crowd.LOD 0 이면 전원을 한 번에 Near로 올린다.
	// 슬라이스로 나눠 처리하면 "LOD를 껐는데도 일부는 아직 낮은" 상태가 섞여
	// 측정값이 흐려진다.
	const bool bLodEnabled = (SubNpcCrowdCVars::EnableLod != 0);
	const int32 Count = bLodEnabled ? FMath::Min(SliceSize, ActiveNpcs.Num()) : ActiveNpcs.Num();

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

		// 회수는 "충분히 멀다"만으로는 부족하다. 플레이어가 보고 있는 동안 사라지면
		// 그게 제일 티가 나는 위화감이다. 최근에 그려진 개체는 거리와 무관하게 남겨둔다.
		if (DistSq > RecycleSq && !Npc->WasRecentlyRendered(0.5f))
		{
			ToRecycle.Add(Npc);
			continue;
		}

		if (!bLodEnabled)
		{
			// 측정 모드: 거리와 무관하게 최고 상세도.
			// 회수는 그대로 살려둔다 (안 그러면 맵 밖까지 걸어간 개체가 계속 쌓인다).
			Npc->ApplyLodBand(ESubNpcLodBand::Near);
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
				// uint8 + 1은 int로 승격되므로 FMath::Min의 두 인자 타입이 어긋난다.
				// 양쪽을 int32로 맞춰서 비교한 뒤 다시 enum으로 되돌린다.
				const int32 Demoted = FMath::Min(
					static_cast<int32>(Band) + 1,
					static_cast<int32>(ESubNpcLodBand::Culled));
				Band = static_cast<ESubNpcLodBand>(Demoted);
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

		// 정원 초과분도 마찬가지로, 화면에 보이는 개체는 건드리지 않는다.
		if (Farthest && !Farthest->WasRecentlyRendered(0.5f))
		{
			ReturnToPool(Farthest);
		}
	}
}
