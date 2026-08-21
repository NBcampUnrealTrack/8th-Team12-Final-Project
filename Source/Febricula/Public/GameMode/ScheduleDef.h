#pragma once
#include "GameplayTagContainer.h"
#include "ScheduleBlockRow.h"
#include "ScheduleDef.generated.h"

USTRUCT(BlueprintType)
struct FScheduleDef
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	FGameplayTag ScheduleName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	TSoftObjectPtr<UWorld> ConnectLevel;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 ActionPoint;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 RepeatCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 BackTo = 0;
	
};
