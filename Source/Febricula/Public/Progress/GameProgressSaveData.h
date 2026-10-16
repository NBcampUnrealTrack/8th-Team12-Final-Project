#pragma once
#include  "GameProgressSaveData.generated.h"

USTRUCT(BlueprintType)
struct FGameProgressSaveData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TSet<FName> Tags;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TSet<FName> Topics;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TSet<FName> CompletedEvents;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TMap<FName, int32> Counters;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TMap<FName, int32> Stats;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 FallbackDay = 1;
};
