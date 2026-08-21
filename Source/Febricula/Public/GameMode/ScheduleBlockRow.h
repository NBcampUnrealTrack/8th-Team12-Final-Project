#pragma once
#include "GameplayTagContainer.h"
#include "ScheduleBlockRow.generated.h"

USTRUCT(BlueprintType)
struct FScheduleBlock
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleBlock")
	TArray<FGameplayTag> Sequence;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleBlock")
	int32 RepeatCount = 1;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleBlock")
	FGameplayTag NextScheduleTag;
};
