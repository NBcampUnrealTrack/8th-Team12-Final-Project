// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TimeSettings.generated.h"

class UDayScheduleAsset;
/**
 * 
 */
UCLASS(Config="Game")
class FEBRICULA_API UTimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Calander Default")
	int32 MaxMonth;
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Calander Default")
	int32 StartMonth;

	
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Calander Default")
	TSoftObjectPtr<UDataTable> ScheduleBlockData;
	
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Schedule Default")
	TSoftObjectPtr<UDayScheduleAsset> WeekDaySchedule; 
	
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Schedule Default")
	TSoftObjectPtr<UDayScheduleAsset> WeekEndSchedule;
	
	
};
