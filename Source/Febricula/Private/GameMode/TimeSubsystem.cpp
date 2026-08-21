// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/TimeSubsystem.h"

#include "GameMode/DayScheduleAsset.h"
#include "GameMode/ScheduleBlockRow.h"
#include "GameMode/TimeSettings.h"

void UTimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const auto* S = GetDefault<UTimeSettings>();
	
	NowDay = 1;
	
	MaxMonth = S->MaxMonth;
	
	NowMonth = S->StartMonth;
	MaxDay = GetDayPerMonth();
	
	WeekDaySchedule = S->WeekDaySchedule.LoadSynchronous();
	WeekEndSchedule = S->WeekEndSchedule.LoadSynchronous();
	NowSchedule = WeekDaySchedule->ScheduleDefs[0];
	
	NowRepeatCount = 0;
}

int32 UTimeSubsystem::GetDayPerMonth()
{
	if (NowMonth % 2 == 0 )
	{
		return 30;
	}
	else
	{
		return 31;
	}
}

void UTimeSubsystem::SpendDay()
{
	if (++NowDay > MaxDay)
	{
		NowDay = 1;
		NowMonth += 1;
		MaxDay = GetDayPerMonth();
	}
	OnDayChanged.Broadcast(NowDay);
}

void UTimeSubsystem::SpendSchedule()
{
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	NowSchedule = Defs[NowScheduleIndex];
	
	UE_LOG(LogTemp, Warning, TEXT("%d"), NowRepeatCount)
	if (NowSchedule.BackTo > 0)
	{
		if (++NowRepeatCount < NowSchedule.RepeatCount)
		{
			NowScheduleIndex -= NowSchedule.BackTo;
		}
		else
		{
			NowRepeatCount = 0;
			++NowScheduleIndex;
		}
	}
	else
	{
		++NowScheduleIndex;
	}
	UE_LOG(LogTemp, Warning, TEXT("Now Schedule Index: %d / %d"), NowScheduleIndex, Defs.Num() - 1)
	if (NowScheduleIndex >= Defs.Num())
	{
		NowScheduleIndex = 0;
		SpendDay();
		NowRepeatCount = 0;
	}
	UE_LOG(LogTemp, Warning, TEXT("After Schedule Index: %d / %d"), NowScheduleIndex, Defs.Num() - 1)
	
	NowSchedule = Defs[NowScheduleIndex];
	OnScheduleChanged.Broadcast(NowSchedule.ScheduleName);

	//const uint8 ChangedLoop = 
	// if (NowDay % 7 < 5)e
	// {
	// 	if (NowScheduleIndex + 1 < WeekDaySchedule->ScheduleDefs.Num())
	// 	{
	// 		NowSchedule = WeekDaySchedule->ScheduleDefs[++NowScheduleIndex];
	// 	}
	// 	else
	// 	{
	// 		NowScheduleIndex = -1;
	// 		SpendDay();
	// 		SpendSchedule();
	// 	}
	// }
	// else
	// {
	// 	if (NowScheduleIndex + 1 < WeekEndSchedule->ScheduleDefs.Num())
	// 	{
	// 		NowSchedule = WeekEndSchedule->ScheduleDefs[++NowScheduleIndex];
	// 	}
	// 	else
	// 	{
	// 		NowScheduleIndex = -1;
	// 		SpendDay();
	// 		SpendSchedule();
	// 	}
	// }
}

int32 UTimeSubsystem::GetNowDay()
{
	return NowDay;
}

int32 UTimeSubsystem::GetMaxMonth()
{
	return MaxMonth;
}

FScheduleDef& UTimeSubsystem::GetScheduleByIndex(int32 index) const
{
	if (NowDay % 7 < 5)
	{
		return WeekDaySchedule->ScheduleDefs[index];
	}
	return WeekEndSchedule->ScheduleDefs[index];
}

const FScheduleDef* UTimeSubsystem::GetScheduleByTag(FGameplayTag Tag) const
{
	if (NowDay % 7 < 5)
	{
		for (FScheduleDef& ScheduleDef : WeekDaySchedule->ScheduleDefs)
		{
			if (ScheduleDef.ScheduleName == Tag)
			{
				return &ScheduleDef;
			}
		}
	}
	else
	{
		for (FScheduleDef& ScheduleDef : WeekEndSchedule->ScheduleDefs)
		{
			if (ScheduleDef.ScheduleName == Tag)
			{
				return &ScheduleDef;
			}
		}
	}
	return nullptr;

}

const TArray<FScheduleDef>& UTimeSubsystem::GetScheduleDefs() const
{
	if (NowDay % 7 < 5)
	{
		return WeekDaySchedule->ScheduleDefs;
	}
	else
	{
		return WeekEndSchedule->ScheduleDefs;
	}
}
