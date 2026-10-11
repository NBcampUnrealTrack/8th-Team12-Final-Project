// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "FebriculaCheatManager.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API UFebriculaCheatManager : public UCheatManager
{
	GENERATED_BODY()
public:
	UFUNCTION(Exec)
	void SkipDay(int32 Day = 1);
	
	UFUNCTION(Exec)
	void SkipSchedule(int32 Count = 1);
	
	UFUNCTION(Exec)
	void GetCachedCharacter();
	
	UFUNCTION(Exec)
	void GetCurrentAP();
	
	UFUNCTION(Exec)
	void GetClearedEvents();
	
	UFUNCTION(Exec)
	void AddClearedEvent(FName EventId);
	
	UFUNCTION(Exec)
	void GetRunnableActivities();
	
	UFUNCTION(Exec)
	void GetAllActivities();
	
	UFUNCTION(Exec)
	void GetAllCachedTargetPoints();
	
	UFUNCTION(Exec)
	void GetNowStat(FName StatName);
	
	UFUNCTION(Exec)
	void GetMoveHistory();
};
