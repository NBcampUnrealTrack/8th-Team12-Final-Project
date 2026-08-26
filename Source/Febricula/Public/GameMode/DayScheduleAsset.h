// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ScheduleDef.h"
#include "Engine/DataAsset.h"
#include "DayScheduleAsset.generated.h"

/**
 * 하루치 시간표.
 *
 * [변경] DayTypeTag 추가 (필드 추가만, 기존 필드는 그대로).
 *   FScheduleDef.ScheduleName에서 평일/주말 구분을 빼냈으므로,
 *   "이 시간표가 어떤 날의 것인가"는 애셋 자신이 선언한다.
 *   조건 판정에서 시간대(Schedule.*)와 날 종류(DayType.*)를 독립적으로 물어볼 수 있게 하려는 것이다.
 *
 *   예) DA_WeekDaySchedule  -> DayType.WeekDay
 *       DA_WeekEndSchedule  -> DayType.WeekEnd
 *       DA_ExamWeekSchedule -> DayType.ExamWeek   (나중에 추가해도 C++ 수정 없음)
 */
UCLASS(BlueprintType)
class FEBRICULA_API UDayScheduleAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	/** 이 시간표가 적용되는 날의 종류. 비워두면 조건 DayTypeIs가 항상 실패한다 */
	UPROPERTY(EditAnywhere, Category = "Febricula | DaySchedule")
	FGameplayTag DayTypeTag;

	UPROPERTY(EditAnywhere)
	TArray<FScheduleDef> ScheduleDefs;
};
