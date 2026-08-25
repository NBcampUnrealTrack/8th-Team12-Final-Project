// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "TestEventAsset.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API UTestEventAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	TSoftObjectPtr<UWorld> ConnectLevel;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")
	FGameplayTag ConnectSchedule;
};
