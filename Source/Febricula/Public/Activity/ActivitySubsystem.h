// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDefinition.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ActivitySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActivityRequested, FGameplayTag, ActivityTag, FActivityDefinition, Definition);

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

	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void RunActivity(FGameplayTag ActivityTag);

	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	bool TryConsumeAP(int32 Cost);

	// ---- 세이브/로드 ----

	UFUNCTION(BlueprintPure, Category = "ActivitySubsystem | Save")
	int32 GetCurrentAP() const { return CurrentAP; }

	UFUNCTION(BlueprintCallable, Category = "ActivitySubsystem | Save")
	void SetCurrentAP(int32 NewAP) { CurrentAP = NewAP; }

	UFUNCTION(BlueprintCallable, Category = ActivitySubsystem)
	void GoHome();

	FGameplayTag GetNowScheduleTag() const;

	UPROPERTY(BlueprintAssignable, Category = ActivitySubsystem)
	FOnActivityRequested OnActivityRequested;

	// 디버그용
#if !UE_BUILD_SHIPPING
	const TMap<FGameplayTag, FActivityDefinition>& GetAllActivities() const;
	const TMap<FGameplayTag, FActivityDefinition>& GetRunnableActivities() const;

#endif

protected:
	UFUNCTION()
	void OnChangeSchedule(FGameplayTag ChangedSchedule);

	void OnLevelLoaded(UWorld* LoadedWorld);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	FGameplayTag NowSchedule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	TMap<FGameplayTag, FActivityDefinition> AllActivities;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	TMap<FGameplayTag, FActivityDefinition> RunnableActivities;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity | FixedEventAsset")
	TObjectPtr<UDataTable> FixedEvents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Activity")
	int32 CurrentAP;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Activity")
	int32 MaxAP;
};