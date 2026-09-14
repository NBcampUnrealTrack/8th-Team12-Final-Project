// Fill out your copyright notice in the Description page of Project Settings.


#include "Dialogue/NpcCrowdManager.h"

#include "GameplayTagContainer.h"
#include "GameMode/TimeSubsystem.h"
#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"


// Sets default values
ANpcCrowdManager::ANpcCrowdManager()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void ANpcCrowdManager::BeginPlay()
{
	Super::BeginPlay();
}

void ANpcCrowdManager::Play(FName Tag)
{
	UWorld* World = GetWorld();
	if (!World) return;
	
	FCrowdSequence* NowCrowdSequence = CrowdSequence.Find(Tag);
	if (!NowCrowdSequence)
	{
		UE_LOG(LogTemp, Display, TEXT("CrowdManager: %s와 연결된 Crowd가 없음"), *Tag.ToString());
		return;
	}
	if (USubNpcDirectorSubsystem* SubNpcDirectorSubsystem = World->GetSubsystem<USubNpcDirectorSubsystem>())
	{
		for (int i = 0; i < NowCrowdSequence->SpawnCount; i++)
		{
			const int32 Row = i / NowCrowdSequence->NpcPerRow;
			const float Distance = NowCrowdSequence->StartOffset + Row * NowCrowdSequence->NpcGap;
			if (!IsValid(NowCrowdSequence->CrowdPath))
			{
				UE_LOG(LogTemp, Warning, TEXT("CrowdManager: %s의 CrowdPath가 지정되지 않음"), *Tag.ToString());
				return;
			}
			ASubNpcCharacter* SubNpc = SubNpcDirectorSubsystem->SpawnOnPath(NowCrowdSequence->CrowdPath, Distance, false);
			if (!SubNpc)
			{
				UE_LOG(LogTemp, Warning, TEXT("CrowdManager: %s의 NPC를 스폰하는데 실패함"), *Tag.ToString());
				return;
			}
			
		}
	}
}

// Called every frame

