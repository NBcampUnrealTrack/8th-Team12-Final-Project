// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDefinition.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ActivitySubsystem.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API UActivitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void FindRunnableActivities();
	// UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)	
	// void RunActivity();
	
	FGameplayTag GetNowScheduleTag() const;
	
protected:
	UFUNCTION()
	void OnChangeSchedule(FGameplayTag ChangedSchedule);
	
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category= "Activity")
	FGameplayTag NowSchedule;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category= "Activity")
	TMap<FGameplayTag, FActivityDefinition> AllActivities;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category= "Activity")
	TMap<FGameplayTag, FActivityDefinition> RunnableActivities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category= "Activity | FixedEventAsset")
	TObjectPtr<UDataTable> FixedEvents;
};
