// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ScheduleBlockRow.h"
#include "ScheduleDef.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimeSubsystem.generated.h"

class UDayScheduleAsset;
enum class EDaySchedule : uint8;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDayChangedDelegate, int32, ChangedDay);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FScheduleChangedDelegate, FGameplayTag, ChangedScheduleName);
/**
 * 
 */
UCLASS()
class FEBRICULA_API UTimeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	UPROPERTY(BlueprintAssignable)
	FDayChangedDelegate OnDayChanged;
	
	UPROPERTY(BlueprintAssignable)
	FScheduleChangedDelegate OnScheduleChanged;
	
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	int32 GetDayPerMonth();
	
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	void SpendDay();
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Schedule")
	void SpendSchedule();
	
	int32 GetNowDay();
	int32 GetMaxMonth();
	
	FScheduleDef& GetScheduleByIndex(int32 index) const;
	const FScheduleDef* GetScheduleByTag(FGameplayTag Tag) const;
	
	
protected:
	
	const TArray<FScheduleDef>& GetScheduleDefs() const;
	
	
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Febricula | Day")
	int32 NowDay;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 NowMonth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 MaxMonth;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 MaxDay;

	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Weather")
	//Weather 제어 어트리뷰트 추가 예정
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	int32 NowScheduleIndex;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	FScheduleDef NowSchedule;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	TObjectPtr<UDayScheduleAsset> WeekDaySchedule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	TObjectPtr<UDayScheduleAsset> WeekEndSchedule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	TObjectPtr<UDataTable> ScheduleBlockTable;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	FScheduleBlock ScheduleBlock;
	
	int32 NowRepeatCount;
};
