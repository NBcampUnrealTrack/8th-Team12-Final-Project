#pragma once
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "CrowdSequence.generated.h"

USTRUCT(BlueprintType)
struct FCrowdSequence
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager")
	TObjectPtr<ANpcCrowdPath> CrowdPath;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager", meta = (ClampMin = 1))
	int32 SpawnCount = 20;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager", meta = (ClampMin = 1.f))
	float NpcGap = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager", meta = (ClampMin = 0.f))
	float StartOffset = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager", meta = (ClampMin = 1))
	int32 NpcPerRow = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager")
	bool bDespawnOnEnd = true;
};
