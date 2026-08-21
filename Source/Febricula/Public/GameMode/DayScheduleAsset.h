// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ScheduleDef.h"
#include "Engine/DataAsset.h"
#include "DayScheduleAsset.generated.h"

/**
 * 
 */

UCLASS(BlueprintType)
class FEBRICULA_API UDayScheduleAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	TArray<FScheduleDef> ScheduleDefs;
};
