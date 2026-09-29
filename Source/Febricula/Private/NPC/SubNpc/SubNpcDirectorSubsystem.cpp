// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"

#include "DrawDebugHelpers.h"
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

	static int32 AvoidPairsOverride = -1;
	static FAutoConsoleVariableRef CVarAvoidPairs(
		TEXT("crowd.AvoidPairs"),
		AvoidPairsOverride,
		TEXT("회피 계산에 참여시킬 최대 인원. -1이면 코드 기본값."),
		ECVF_Cheat);

	static float SpawnSpacingOverride = -1.f;
	static FAutoConsoleVariableRef CVarSpawnSpacing(
		TEXT("crowd.SpawnSpacing"),
		SpawnSpacingOverride,
		TEXT("등장 간격(cm) 덮어쓰기. -1이면 코드 기본값, 0이면 간격 없이 등장."),
		ECVF_Cheat);

	static int32 EnableMeshCollision = 0;
	static FAutoConsoleVariableRef CVarMeshCollision(
		TEXT("crowd.MeshCollision"),
		EnableMeshCollision,
		TEXT("서브 NPC 메시의 물리 콜리전. 1이면 켠다 (UpdateKinematicBonesToAnim 비용 비교용)."),
		ECVF_Cheat);

	/**
	 * LOD 밴드 경계의 히스테리시스 폭 (cm).
	 *
	 * 경계에 걸친 개체는 조금만 움직여도 밴드가 오락가락하는데, 밴드가 바뀌면
	 * 강제 LOD·그림자·애니메이션 주기가 전부 다시 적용되어 그 자체로 깜빡여 보인다.
	 * 이 값만큼 "지금 밴드에 머무는 쪽"으로 경계를 밀어서 진동을 없앤다.
	 *
	 * [주의] 클래스 멤버가 아니라 파일 스코프 상수다.
	 * USubNpcDirectorSubsystem에 UPROPERTY를 추가하면 그 뒤의 ActiveNpcs / PooledNpcs
	 * 오프셋이 밀린다. 빌드가 조금이라도 섞이면 배열 포인터를 엉뚱한 자리에서 읽어
	 * 해제 시점에 접근 위반으로 죽는다. 클래스 크기를 건드리지 않는 게 안전하다.
	 */
	constexpr float LodHysteresis = 500.f;

	/**
	 * 앞사람과 이 거리(cm) 안으로 붙으면 속도를 줄인다.
	 *
	 * 좌우로만 밀어내면 앞사람이 느릴 때 뒷사람이 그대로 통과해버린다.
	 * 위와 같은 이유로 클래스 멤버가 아니라 파일 스코프 상수로 둔다.
	 */
	constexpr float FollowSlowDistance = 200.f;

	/** 이 거리(cm) 안으로 붙으면 완전히 멈춘다. 캡슐 지름(약 68cm)보다 커야 겹치지 않는다 */
	constexpr float FollowStopDistance = 85.f;

	/** 차선이 이만큼(cm) 안으로 겹칠 때만 "앞이 막혔다"로 본다 */
	constexpr float FollowLaneWidth = 70.f;

	/**
	 * 플레이어를 이 거리(cm) 안에서 인지하고 피한다.
	 *
	 * [왜 따로 필요한가] 회피 계산은 ASubNpcCharacter끼리만 돈다. 플레이어는
	 * 그 목록에 없으므로 행인 입장에서는 **존재하지 않는 것과 같다.**
	 * 게다가 행인 이동은 bSweep=false라 캡슐로 막히지도 않는다 (플레이어만 막힌다).
	 * 그래서 플레이어를 명시적으로 회피 대상에 넣어준다.
	 */
	constexpr float PlayerAvoidDistance = 200.f;

	/** 플레이어를 피해 옆으로 비키는 세기. 행인끼리보다 세게 민다 */
	constexpr float PlayerAvoidStrength = 1.6f;

	/** 이 비율 안쪽으로 정면에서 막히면 속도를 줄인다 */
	constexpr float PlayerBlockRatio = 0.65f;

	static int32 TraceSpawn = 0;
	static FAutoConsoleVariableRef CVarTraceSpawn(
		TEXT("crowd.TraceSpawn"),
		TraceSpawn,
		TEXT("보충이 실패할 때 이유를 로그로 찍는다. 회수 후 재등장이 안 될 때 쓴다."),
		ECVF_Cheat);

	static int32 DebugDraw = 0;
	static FAutoConsoleVariableRef CVarDebugDraw(
		TEXT("crowd.Debug"),
		DebugDraw,
		TEXT("활성 서브 NPC 위에 디버그 표식을 그린다. 메시가 보이든 안 보이든 항상 그린다.\n")
		TEXT("표식은 있는데 사람이 안 보이면 = 렌더링 문제.\n")
		TEXT("표식까지 같이 사라지면 = 풀로 회수된 것(게임플레이 문제)."),
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
	// [크래시 대응] 예전에는 여기서 풀과 활성 목록을 순회하며 Destroy()를 불렀다.
	//
	// 그런데 Destroy()는 그 자리에서 EndPlay를 부르고, ASubNpcCharacter::EndPlay는
	// Director->UnregisterNpc(this)를 호출한다. 그 함수가 하는 일이
	// ActiveNpcs.RemoveSingleSwap(this) 다 - 지금 순회 중인 바로 그 배열이다.
	// 순회 중 배열이 줄어들면 유효하지 않은 메모리를 읽게 되고, 인원이 많을수록
	// 반드시 터진다. ("사람이 적으면 안 터진다"의 정체)
	//
	// 애초에 여기서 Destroy를 부를 이유가 없다. 이 시점은 월드가 정리되는 중이고,
	// 월드는 자기 액터를 **전부 스스로 파괴한다.** 우리가 먼저 손대면 순서만 꼬인다.
	// 그래서 참조만 끊고 물러난다. 액터 수명은 월드에 맡긴다.
	//
	// 레벨 전환 후 메모리가 남는 걱정은 없다. 풀 액터는 RF_Transient로 스폰되고
	// PIE/레벨 월드와 함께 사라진다.
	ActiveNpcs.Reset();
	PooledNpcs.Reset();
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

#if !UE_BUILD_SHIPPING
	if (SubNpcCrowdCVars::DebugDraw != 0)
	{
		DrawDebugMarkers();
	}
#endif

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

		// [버그 수정] 런타임 SpawnActor는 **그 자리에서 BeginPlay를 호출한다.**
		// ASubNpcCharacter::BeginPlay 는 Director->RegisterNpc(this) 를 부르므로,
		// 방금 만든 이 개체는 이미 ActiveNpcs 안에 들어가 있다.
		//
		// 그대로 PooledNpcs에 넣으면 **같은 액터가 두 배열에 동시에 존재**하게 된다.
		// 그 상태에서 벌어지는 일:
		//   - GetActiveCount()가 처음부터 풀 크기만큼 나온다 (스포너가 "이미 찼다"고 판단)
		//   - UpdateLodSlice가 풀 액터를 순회하며 ApplyLodBand로 다시 보이게 만든다
		//     → Base Target Count가 0이어도 100명이 화면에 나타난다
		//   - 회수·파괴 경로에서 같은 액터가 두 번 처리된다
		//
		// 풀에 넣기 전에 활성 목록에서 확실히 뺀다.
		ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);

		Npc->DeactivateToPool();
		PooledNpcs.AddUnique(Npc);
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
		if (SubNpcCrowdCVars::TraceSpawn != 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Crowd] 등장 실패: %s (활성 %d / 상한 %d / 풀 %d)"),
				(PooledNpcs.Num() == 0) ? TEXT("풀이 비었다") : TEXT("정원이 찼다"),
				ActiveNpcs.Num(), MaxActiveNpcs, PooledNpcs.Num());
		}
		return nullptr;
	}

	ASubNpcCharacter* Npc = PooledNpcs.Pop(EAllowShrinking::No);

	// [버그 수정] 예전에는 여기서 그냥 return nullptr 했다. 그러면 방금 꺼낸 개체가
	// 풀에도 활성 목록에도 없는 상태로 **영원히 사라진다.** 죽은 개체가 몇 번 섞이면
	// 풀이 조금씩 말라서 "한참 뒤부터 사람이 안 나온다"가 된다.
	// 유효하지 않은 항목은 버리고 다음 것으로 계속 시도한다.
	while (!IsValid(Npc) && PooledNpcs.Num() > 0)
	{
		Npc = PooledNpcs.Pop(EAllowShrinking::No);
	}

	if (!IsValid(Npc))
	{
		if (SubNpcCrowdCVars::TraceSpawn != 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Crowd] 등장 실패: 풀에 유효한 개체가 없다."));
		}
		return nullptr;
	}

	// [개선] 등장 지점이 이미 붐비면 경로를 따라 조금 밀어서 세운다.
	// 같은 자리에 겹쳐 등장하면 한 덩어리로 몰려다니고, 보기에도 바로 티가 난다.
	const float EffectiveSpacing = (SubNpcCrowdCVars::SpawnSpacingOverride >= 0.f)
		? SubNpcCrowdCVars::SpawnSpacingOverride
		: SpawnSpacing;

	if (EffectiveSpacing > 0.f)
	{
		const float PathLength = Path->GetPathLength();
		const float Step = EffectiveSpacing;
		const float SpacingSq = FMath::Square(EffectiveSpacing);

		// 최대 8칸까지만 밀어본다. 그래도 자리가 없으면 이번 등장은 포기한다.
		// 자리를 못 찾아도 등장 자체를 막지는 않는다.
		//
		// 처음에는 실패 시 풀에 되돌렸는데, 등장구가 적으면 입구가 금방 차서
		// 그 뒤로 계속 실패하고 목표 인원에 영영 도달하지 못했다.
		// 간격은 "겹쳐 보이지 않게" 하려는 편의 기능이지 인원을 제한하는 규칙이 아니다.
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
				break;
			}
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

	const float Length = FMath::Max(1.f, Path->GetPathLength());

	for (int32 i = 1; i < GroupSize; ++i)
	{
		// [개선] 예전에는 전원을 리더와 **똑같은 지점**에 세우고 뒤에서 간격만 벌렸다.
		// 그러면 등장하는 순간 한 점에 겹쳐 있다가 서서히 풀리는 게 눈에 보인다.
		// 처음부터 조금씩 뒤에 세운다.
		const float MemberStart = FMath::Clamp(
			StartDistance + (bReversed ? 1.f : -1.f) * FMath::FRandRange(60.f, 140.f) * i,
			0.f, Length);

		ASubNpcCharacter* Member = SpawnOnPath(Path, MemberStart, bReversed);
		if (!Member)
		{
			break;
		}

		// 나란히 걷되 살짝 어긋나게. 완전히 일직선이면 행진처럼 보인다.
		const float SideOffset = FMath::Clamp(
			(i % 2 == 0 ? 1.f : -1.f) * FMath::FRandRange(50.f, 90.f), -HalfWidth, HalfWidth);
		const float Gap = FMath::FRandRange(60.f, 140.f) * i;

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

	// [방어] 이미 풀에 들어있는 개체는 활성으로 올리지 않는다.
	// IsPooled() 검사만으로는 부족하다 - PrewarmPool은 BeginPlay(여기) 다음에
	// DeactivateToPool을 부르므로, 그 시점에는 아직 Pooled 상태가 아니다.
	if (PooledNpcs.Contains(Npc))
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

#if !UE_BUILD_SHIPPING
void USubNpcDirectorSubsystem::DrawDebugMarkers() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 이 표식은 액터 위치에 직접 그린다. 스켈레탈 메시가 숨겨져 있어도,
	// LOD가 최저여도, 애니메이션이 멈춰 있어도 상관없이 그려진다.
	// 그래서 "사람이 안 보이는" 원인이 렌더링인지 회수인지 눈으로 가를 수 있다.
	for (const TObjectPtr<ASubNpcCharacter>& Npc : ActiveNpcs)
	{
		if (!IsValid(Npc))
		{
			continue;
		}

		FColor Color = FColor::White;
		const TCHAR* BandName = TEXT("?");
		switch (Npc->GetLodBand())
		{
		case ESubNpcLodBand::Near:		Color = FColor::Green;	BandName = TEXT("N"); break;
		case ESubNpcLodBand::Mid:		Color = FColor::Yellow;	BandName = TEXT("M"); break;
		case ESubNpcLodBand::Far:		Color = FColor::Orange;	BandName = TEXT("F"); break;
		case ESubNpcLodBand::Culled:	Color = FColor::Red;	BandName = TEXT("C"); break;
		default: break;
		}

		const FVector Loc = Npc->GetActorLocation();

		// 발밑 원 - 위치가 어디인지
		DrawDebugCircle(World, Loc - FVector(0, 0, 88.f), 34.f, 12, Color,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, 0, 2.f,
			FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);

		// 머리 위 글자 - 지금 어느 LOD 밴드인지
		DrawDebugString(World, Loc + FVector(0, 0, 110.f), BandName, nullptr, Color, 0.f, true, 1.2f);
	}
}
#endif

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

	// [측정 근거] stat game에서 Director 전체(TickableGameObjects Time)의
	// 0명 -> 90명 델타가 0.06 ms에 불과했다. 예전 기본값 24는 근거 없이 보수적이었고,
	// 그 탓에 90명 중 24명만 회피에 참여해 나머지가 그대로 겹쳐 보였다.
	const int32 PairBudget = (SubNpcCrowdCVars::AvoidPairsOverride >= 0)
		? SubNpcCrowdCVars::AvoidPairsOverride
		: MaxAvoidancePairs;

	// 가까이 보이는 개체만 모은다. 여기가 회피 비용을 결정한다.
	TArray<ASubNpcCharacter*> Nearby;
	Nearby.Reserve(PairBudget);

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
		if (Nearby.Num() >= PairBudget)
		{
			break;
		}
	}

	// ---- 플레이어 회피 --------------------------------------------------------
	//
	// 행인끼리의 회피와 별개로, 앞을 막고 선 플레이어를 피해 비켜 간다.
	// 옆으로 밀어내고, 그래도 정면이면 속도를 줄인다. 둘을 같이 해야
	// "사람을 보고 비켜 간다"로 읽힌다 - 옆으로만 밀면 좁은 길에서 그냥 통과한다.
	{
		const float PlayerAvoidSq = FMath::Square(SubNpcCrowdCVars::PlayerAvoidDistance);

		for (ASubNpcCharacter* Npc : Nearby)
		{
			const FVector ToPlayer = ViewerLocation - Npc->GetActorLocation();

			// 높이 차이는 무시한다. 위층에 있는 플레이어를 피할 이유는 없다.
			const float DistSq = ToPlayer.SizeSquared2D();
			if (DistSq >= PlayerAvoidSq || DistSq < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// 진행 방향 기준으로 앞에 있을 때만 피한다. 이미 지나친 사람을
			// 계속 피하면 뒤를 힐끔거리며 걷는 것처럼 보인다.
			const float Forward = FVector::DotProduct(ToPlayer, Npc->GetActorForwardVector());
			if (Forward <= 0.f)
			{
				continue;
			}

			const float Dist = FMath::Sqrt(DistSq);
			const float Overlap = 1.f - (Dist / SubNpcCrowdCVars::PlayerAvoidDistance);

			// 플레이어가 오른쪽에 있으면 왼쪽으로 비킨다. 정확히 정면이면
			// 개체 이름 해시로 방향을 갈라서, 마주친 사람들이 같은 쪽으로만
			// 쏠리지 않게 한다.
			const float Side = FVector::DotProduct(ToPlayer, Npc->GetActorRightVector());
			const float Sign = (FMath::Abs(Side) > 1.f)
				? (Side > 0.f ? -1.f : 1.f)
				: ((GetTypeHash(Npc->GetFName()) & 1) ? -1.f : 1.f);

			Npc->AddAvoidanceNudge(
				Sign * Overlap * SubNpcCrowdCVars::PlayerAvoidDistance * SubNpcCrowdCVars::PlayerAvoidStrength);

			// 정면으로 바짝 막혔으면 감속한다. 옆으로 비킬 공간이 생기면
			// 위의 밀어내기가 먼저 풀어주므로 영원히 멈춰 있지는 않는다.
			const float BlockDist = SubNpcCrowdCVars::PlayerAvoidDistance * SubNpcCrowdCVars::PlayerBlockRatio;
			if (Dist < BlockDist && FMath::Abs(Side) < BlockDist * 0.5f)
			{
				Npc->RequestSlowDown(FMath::Clamp(Dist / BlockDist, 0.1f, 1.f));
			}
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

			// [의도] 같은 그룹(친구)끼리는 밀어내지 않는다.
			// "친구는 붙고 남남은 떨어진다"는 그림이 이 한 줄에서 나온다.
			if (A->IsSameGroup(B))
			{
				continue;
			}

			// [종방향 회피] 좌우 회피보다 **먼저, 더 넓은 거리에서** 판정한다.
			//
			// 예전에는 이 검사가 아래 좌우 회피의 거리 조건(AvoidDistance) 안에 갇혀
			// 있었다. 그래서 110cm보다 멀면 검사조차 안 했고, 그 사이에 이미 겹쳐서
			// 지나가버렸다. 앞사람을 인지하려면 붙기 전에 봐야 한다.
			if (A->IsReversedOnPath() == B->IsReversedOnPath())
			{
				const float Along = A->IsReversedOnPath() ? -1.f : 1.f;
				const float Ahead = (B->GetPathDistance() - A->GetPathDistance()) * Along;
				const float LaneGap = FMath::Abs(A->GetLaneOffset() - B->GetLaneOffset());
				const float Gap = FMath::Abs(Ahead);

				// 차선이 절반 이상 겹치고, 제동 거리 안에 들어왔으면 뒷사람을 늦춘다.
				if (LaneGap < SubNpcCrowdCVars::FollowLaneWidth && Gap < SubNpcCrowdCVars::FollowSlowDistance)
				{
					// Ahead > 0 이면 B가 앞, A가 뒤.
					ASubNpcCharacter* Rear = (Ahead > 0.f) ? A : B;

					// 바짝 붙으면 0까지 떨어뜨려 **완전히 멈추게** 한다.
					// 예전 하한 0.15는 "느리게 통과"를 만들 뿐이었다.
					// 옆으로 비킬 공간이 생기면 아래 좌우 회피가 먼저 풀어준다.
					const float Scale = FMath::Clamp(
						(Gap - SubNpcCrowdCVars::FollowStopDistance)
							/ FMath::Max(1.f, SubNpcCrowdCVars::FollowSlowDistance - SubNpcCrowdCVars::FollowStopDistance),
						0.f, 1.f);

					Rear->RequestSlowDown(Scale);
				}
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
	// 밴드 경계는 개체마다 히스테리시스가 붙으므로 루프 안에서 계산한다.
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
			Npc->ApplyMovementTickRate(ESubNpcLodBand::Near);
			Npc->ApplyLodBand(ESubNpcLodBand::Near);
			continue;
		}

		// 거리만으로 정한 밴드. 아래에서 렌더링 여부로 한 단계 더 내려갈 수 있지만,
		// **이동 틱 주기는 이 값으로 고정한다.**
		//
		// [히스테리시스] 경계에 딱 걸친 개체는 미세한 거리 변화만으로 밴드가 널뛴다.
		// 밴드가 바뀔 때마다 강제 LOD·그림자·애니메이션 주기가 전부 다시 적용되므로
		// 그 자체로 눈에 띄는 깜빡임이 된다.
		// 그래서 "지금 있는 밴드에 머무는 쪽"으로 경계를 LodHysteresis만큼 밀어준다.
		// 밖으로 나가려면 조금 더 멀어져야 하고, 들어오려면 조금 더 가까워져야 한다.
		const int32 CurBand = static_cast<int32>(Npc->GetLodBand());
		constexpr float H = SubNpcCrowdCVars::LodHysteresis;

		const float NearLimit = (CurBand <= 0) ? NearDistance + H : NearDistance - H;
		const float MidLimit  = (CurBand <= 1) ? MidDistance  + H : MidDistance  - H;
		const float FarLimit  = (CurBand <= 2) ? FarDistance  + H : FarDistance  - H;

		ESubNpcLodBand DistanceBand;
		if (DistSq <= FMath::Square(NearLimit))      { DistanceBand = ESubNpcLodBand::Near; }
		else if (DistSq <= FMath::Square(MidLimit))  { DistanceBand = ESubNpcLodBand::Mid; }
		else if (DistSq <= FMath::Square(FarLimit))  { DistanceBand = ESubNpcLodBand::Far; }
		else                                         { DistanceBand = ESubNpcLodBand::Culled; }

		// 카메라를 돌리면 뒤쪽 개체가 순간적으로 "안 그려짐" 판정을 받는다.
		// 그때 틱 주기까지 같이 널뛰면 위치 갱신이 뚝뚝 끊겨서 순간이동으로 보인다.
		// 이동은 보이든 말든 매끄러워야 하므로 거리 기준으로만 정한다.
		Npc->ApplyMovementTickRate(DistanceBand);

		ESubNpcLodBand Band = DistanceBand;

		// 실제로 화면에 안 그려지고 있으면 한 단계 낮춘다.
		// WasRecentlyRendered는 렌더 스레드가 채워주는 값이라 공짜에 가깝다.
		//
		// [버그 수정] 예전에는 이 강등이 Culled까지 갈 수 있었다. 그런데 Culled는
		// 메시를 SetVisibility(false)로 숨긴다. 숨은 메시는 영원히 렌더링되지 않으므로
		// WasRecentlyRendered가 계속 false로 남고, 다음 갱신에서 또 Culled로 떨어진다.
		// 한 번 숨으면 거리가 Mid 안쪽으로 들어올 때까지 스스로 못 빠져나오는 자물쇠였다.
		//
		// 3인칭이라 카메라를 돌릴 때마다 등 뒤 개체가 "안 그려짐" 판정을 받는다.
		// 그래서 경계 근처에서 사라졌다 나타났다를 반복했다.
		//
		// 이제 "안 그려진다"를 이유로 한 강등은 Far까지만 허용한다.
		// Culled(=숨김)는 오직 거리로만 판정한다. 거리는 숨겨도 계속 갱신되므로
		// 스스로 되돌아올 수 있다.
		if (bDemoteWhenNotRendered && Band != ESubNpcLodBand::Culled)
		{
			if (!Npc->WasRecentlyRendered(0.5f))
			{
				// uint8 + 1은 int로 승격되므로 FMath::Min의 두 인자 타입이 어긋난다.
				// 양쪽을 int32로 맞춰서 비교한 뒤 다시 enum으로 되돌린다.
				const int32 Demoted = FMath::Min(
					static_cast<int32>(Band) + 1,
					static_cast<int32>(ESubNpcLodBand::Far));
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
