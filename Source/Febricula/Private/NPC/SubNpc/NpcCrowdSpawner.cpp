// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/NpcCrowdSpawner.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/TimeSubsystem.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcCharacter.h"
#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"
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

	// 한 번에 우르르 등장하면 "갑자기 나타났다"가 눈에 보인다. 조금씩 채운다.
	int32 SpawnedThisTick = 0;

	while (Deficit > 0 && SpawnedThisTick < MaxSpawnPerTick)
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
		const float StartDistance = bReversed
			? FMath::FRandRange(Length * 0.8f, Length)
			: FMath::FRandRange(0.f, Length * 0.2f);

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
