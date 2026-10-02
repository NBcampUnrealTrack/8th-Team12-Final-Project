// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDefinition.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ActivitySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActivityRequested, FActivityDefinition, Definition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActivityFinished, FName, ChannelName, FName, ActivityName);
/**
 *
 */
UCLASS()
class FEBRICULA_API UActivitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION()
	void HandleDialogueFinished();
	
	virtual void BindDialogue();
	
	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void FindRunnableActivities();

	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void PrintFixedActivities();
	
	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	bool RunActivityByIndex(int32 Index);
	
	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	bool TryConsumeAP(int32 Cost);

	// ---- 세이브/로드 ----
	
	UFUNCTION(BlueprintCallable, Category = "ActivitySubsystem | Save")
	void SetCurrentAP(int32 NewAP) { CurrentAP = NewAP; }

	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void GoHome();
	
	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	int32 GetCurrentAP();
	
	void ActivityFinished(FName ActivityName);
	
	void ActivityFinished(FName NpcName, int32 EventNumber);
	
	int32 GetRunnableLength();
	
	TArray<FGameplayTag> GetRunnableActivityTag() const;

	FGameplayTag GetNowScheduleTag() const;

	UPROPERTY(BlueprintAssignable, Category = ActivitySubsystem)
	FOnActivityRequested OnActivityRequested;
	
	UPROPERTY(BlueprintAssignable, Category = ActivitySubsystem)
	FOnActivityFinished OnActivityFinished;
	
	UPROPERTY()
	bool bFreeActivity;
	
	UPROPERTY()
	bool bWaitActivityChoice;
	

	// 디버그용
#if !UE_BUILD_SHIPPING
	const TMap<FGameplayTag, FActivityDefinition>& GetAllActivities() const;
	const TMap<FGameplayTag, FActivityDefinition>& GetRunnableActivities() const;

#endif

protected:
	UFUNCTION()
	void OnChangeSchedule(FGameplayTag ChangedSchedule);

	void OnLevelLoaded(UWorld* LoadedWorld);
	
	void SortRunnableActivity();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	FGameplayTag NowSchedule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	TMap<FGameplayTag, FActivityDefinition> AllActivities;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	TMap<FGameplayTag, FActivityDefinition> RunnableActivities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity | FixedEventAsset")
	TObjectPtr<UDataTable> FixedEvents;
	
	UPROPERTY(EditdefaultsOnly, BlueprintReadOnly, Category = "Activity | FixedEventAsset")
	TObjectPtr<UDataTable> FixedDialogues;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	int32 CurrentAP;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity")
	int32 MaxAP;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	bool bProcessActivity;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity")
	FGameplayTag NowPlayedActivity;
};