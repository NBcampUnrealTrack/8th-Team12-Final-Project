// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcCrowdSpawner.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/TimeSubsystem.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcCharacter.h"
#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

ANpcCrowdSpawner::ANpcCrowdSpawner()
{
	// 인원 점검은 타이머로 돌린다. 매 프레임 할 이유가 전혀 없다.
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ANpcCrowdSpawner::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>();
	if (!Director)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Crowd] Director 서브시스템을 찾지 못했습니다."));
		return;
	}

	if (!NpcClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Crowd] %s: Npc Class가 지정되지 않아 군중이 생성되지 않습니다."), *GetNameSafe(this));
		return;
	}

	Director->SetArchetypeTable(ArchetypeTable);
	Director->SetMaxActiveNpcs(FMath::Max(BaseTargetCount, PoolSize));

	// 풀은 여기서 한 번만 만든다. 이후 런타임에는 액터가 새로 생기지 않는다.
	Director->PrewarmPool(NpcClass, PoolSize);

	// 입구 경로를 지정 안 했으면 레벨에서 자동으로 찾는다.
	if (EntryPaths.Num() == 0)
	{
		for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
		{
			if (It->IsSpawnEntry())
			{
				EntryPaths.Add(*It);
			}
		}

		if (EntryPaths.Num() == 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Crowd] 입구 경로가 없습니다. NpcCrowdPath 액터 중 하나 이상에서 'Spawn Entry'를 켜세요."));
		}
	}

	World->GetTimerManager().SetTimer(
		SpawnTimerHandle, this, &ANpcCrowdSpawner::TickSpawn,
		FMath::Max(SpawnInterval, 0.1f), /*bLoop=*/true);
}

void ANpcCrowdSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 안 지우면 레벨이 바뀐 뒤에도 죽은 액터를 향해 콜백이 날아간다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}

	EntryPaths.Reset();

	Super::EndPlay(EndPlayReason);
}

int32 ANpcCrowdSpawner::GetCurrentTargetCount() const
{
	float Multiplier = 1.f;

	// 시간대 배율. 시간 시스템이 없으면 그냥 기본값을 쓴다.
	if (DensityBySchedule.Num() > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GI = World->GetGameInstance())
			{
				if (const UTimeSubsystem* Time = GI->GetSubsystem<UTimeSubsystem>())
				{
					const FName NowTag = Time->GetNowScheduleTag().GetTagName();
					for (const FCrowdDensityByHour& Entry : DensityBySchedule)
					{
						if (Entry.ScheduleTagName == NowTag)
						{
							Multiplier = Entry.DensityMultiplier;
							break;
						}
					}
				}
			}
		}
	}

	return FMath::Max(0, FMath::RoundToInt(BaseTargetCount * Multiplier));
}

ANpcCrowdPath* ANpcCrowdSpawner::PickEntryPath() const
{
	// 죽은 참조를 건너뛰며 랜덤으로 하나 고른다.
	TArray<ANpcCrowdPath*> Valid;
	Valid.Reserve(EntryPaths.Num());
	for (ANpcCrowdPath* Path : EntryPaths)
	{
		if (IsValid(Path))
		{
			Valid.Add(Path);
		}
	}

	if (Valid.Num() == 0)
	{
		return nullptr;
	}
	return Valid[FMath::RandRange(0, Valid.Num() - 1)];
}

int32 ANpcCrowdSpawner::RollGroupSize() const
{
	if (FMath::FRand() < SoloChance)
	{
		return 1;
	}
	return FMath::RandRange(2, FMath::Max(2, MaxGroupSize));
}

bool ANpcCrowdSpawner::GetViewerLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

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

void ANpcCrowdSpawner::TickSpawn()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>();
	if (!Director)
	{
		return;
	}

	const int32 Target = GetCurrentTargetCount();
	int32 Deficit = Target - Director->GetActiveCount();
	if (Deficit <= 0)
	{
		return;
	}

	// 초기 대량 충원인가.
	//
	// 전원이 입구 근처 20% 구간에 몰려 등장하면 90명이 한 덩어리가 되고
	// 이후에도 비슷한 속도로 움직여 잘 풀리지 않는다. 그래서 첫 충원만
	// 경로 전체에 흩어 놓는다.
	//
	// [버그 수정 1] 예전 조건은 "부족 인원이 목표의 절반 이상"이었다. 그러면
	// 퇴장구에서 여러 명이 한꺼번에 회수될 때마다 다시 참이 되어, 플레이 도중에
	// 경로 한복판에서 사람이 우르르 생겨났다. 깜빡임의 정체가 이것이었다.
	//
	// [버그 수정 2] 그 다음 조건("활성 0")은 반대로 너무 좁았다. 첫 주기에
	// MaxSpawnPerTick(기본 2)만큼만 등장하면 활성이 0이 아니게 되어, 대량 충원이
	// 두 번 다시 발동하지 않았다. 그래서 90명을 요청해도 20명도 안 찼다.
	// 이제 "한 번 다 채울 때까지"를 플래그로 기억한다.
	const bool bBulkFill = !bInitialFillDone;

	// 초기 충원은 한 프레임에 목표치까지 밀어 넣는다. 어차피 로딩 직후라
	// 플레이어가 보고 있지 않고, 조금씩 채우면 사람이 계속 생겨나는 게 보인다.
	const int32 SpawnBudget = bBulkFill ? FMath::Max(MaxSpawnPerTick, Deficit) : MaxSpawnPerTick;

	int32 SpawnedThisTick = 0;

	while (Deficit > 0 && SpawnedThisTick < SpawnBudget)
	{
		ANpcCrowdPath* Entry = PickEntryPath();
		if (!Entry)
		{
			return;
		}

		// 경로가 "역방향으로 등장" 설정이면 뒤에서부터 걷는다.
		// 스플라인을 반대로 그린 경우를 점을 다시 찍지 않고 데이터로 뒤집는다.
		const bool bReversed = Entry->IsSpawnReversed();
		const float Length = FMath::Max(1.f, Entry->GetPathLength());

		// 입구 경로의 "시작 쪽" 어딘가에서 등장한다. 항상 같은 지점이면 한 자리에서 계속 튀어나온다.
		// 역방향이면 시작 쪽이 곧 경로의 끝이다.
		float StartDistance;
		if (bBulkFill)
		{
			StartDistance = FMath::FRandRange(Length * 0.05f, Length * 0.95f);
		}
		else
		{
			StartDistance = bReversed
				? FMath::FRandRange(Length * 0.8f, Length)
				: FMath::FRandRange(0.f, Length * 0.2f);
		}

		// 플레이어 눈앞에서 사람이 생기는 것을 되도록 피한다.
		//
		// [중요] 이건 금지가 아니라 **선호**다. 후보 지점을 몇 번 굴려보고
		// 조건을 만족하는 게 있으면 그걸 쓰고, 없으면 그 중 가장 먼 자리를 쓴다.
		//
		// 예전에는 조건을 못 채우면 그 주기를 통째로 건너뛰었다. 그런데 입구
		// 구간이 플레이어 근처에 있는 레벨에서는 매 주기가 전부 실패해서
		// **군중이 아예 채워지지 않았다.** 인원이 안 차는 것이 팝인보다 훨씬 나쁘다.
		if (!bBulkFill && MinSpawnDistanceFromViewer > 0.f)
		{
			FVector ViewerLocation;
			if (GetViewerLocation(ViewerLocation))
			{
				const float MinDistSq = FMath::Square(MinSpawnDistanceFromViewer);

				float BestDistance = StartDistance;
				float BestDistSq = FVector::DistSquared(
					Entry->GetLocationAtDistance(StartDistance), ViewerLocation);

				for (int32 Attempt = 0; Attempt < 5 && BestDistSq < MinDistSq; ++Attempt)
				{
					const float TryDistance = bReversed
						? FMath::FRandRange(Length * 0.8f, Length)
						: FMath::FRandRange(0.f, Length * 0.2f);

					const float TryDistSq = FVector::DistSquared(
						Entry->GetLocationAtDistance(TryDistance), ViewerLocation);

					if (TryDistSq > BestDistSq)
					{
						BestDistSq = TryDistSq;
						BestDistance = TryDistance;
					}
				}

				StartDistance = BestDistance;
			}
		}

		const int32 GroupSize = RollGroupSize();
		const TArray<ASubNpcCharacter*> Spawned =
			(GroupSize <= 1)
			? TArray<ASubNpcCharacter*>{ Director->SpawnOnPath(Entry, StartDistance, bReversed) }
			: Director->SpawnGroupOnPath(Entry, StartDistance, bReversed, GroupSize);

		int32 Actual = 0;
		for (ASubNpcCharacter* Npc : Spawned)
		{
			if (Npc)
			{
				++Actual;
			}
		}

		if (Actual == 0)
		{
			// 풀이 비었거나 정원이 찼다. 다음 주기에 다시 시도한다.
			return;
		}

		Deficit -= Actual;
		SpawnedThisTick += Actual;
	}

	// 목표치를 채웠으면 초기 충원 종료. 이후 보충은 입구에서만 등장한다.
	if (bBulkFill && Deficit <= 0)
	{
		bInitialFillDone = true;
	}
}

void ANpcCrowdSpawner::FillImmediately()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>();
	if (!Director)
	{
		return;
	}

	const int32 Target = GetCurrentTargetCount();

	// 테스트용이므로 경로 전체에 고르게 뿌린다.
	int32 Guard = 0;
	while (Director->GetActiveCount() < Target && Guard++ < 500)
	{
		ANpcCrowdPath* Entry = PickEntryPath();
		if (!Entry)
		{
			return;
		}

		const float StartDistance = FMath::FRandRange(0.f, Entry->GetPathLength());
		if (!Director->SpawnOnPath(Entry, StartDistance, /*bReversed=*/false))
		{
			return;
		}
	}
}
