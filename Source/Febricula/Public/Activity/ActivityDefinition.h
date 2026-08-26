#pragma once
#include "GameplayTagContainer.h"
#include "ActivityDefinition.generated.h"

UENUM(BlueprintType)
enum class EActivityType : uint8
{
	AutoEvent,
	ManualEvent
};

USTRUCT(BlueprintType)
struct FActivityLocation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	FGameplayTag ConnectedScheduleTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TArray<TSoftObjectPtr<UWorld>> ConnectedLevel;
};

USTRUCT(BlueprintType)
struct FActivityDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	EActivityType ActivityType;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TArray<FActivityLocation> ActivityLocations;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	int32 ActionPointCost;
};