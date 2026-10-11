#pragma once
#include "GameplayTagContainer.h"
#include "Dialogue/DialogueDataTypes.h"
#include "ActivityDefinition.generated.h"

USTRUCT(BlueprintType)
struct FActivityLocation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	FGameplayTagContainer ConnectedScheduleTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TArray<TSoftObjectPtr<UWorld>> ConnectedLevel;
};
	
USTRUCT(BlueprintType)
struct FActivityDefinition : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	FGameplayTag ActivityNameTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TArray<FActivityLocation> ActivityLocations;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	int32 ActionPointCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TObjectPtr<UDataTable> ConnectedEvent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	FName NodeId;
};