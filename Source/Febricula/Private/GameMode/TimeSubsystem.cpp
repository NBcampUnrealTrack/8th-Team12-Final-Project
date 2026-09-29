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
	AbsoluteDay = 1;

	MaxMonth = S->MaxMonth;

	NowMonth = S->StartMonth;
	MaxDay = GetDayPerMonth();

	WeekDaySchedule = S->WeekDaySchedule.LoadSynchronous();
	WeekEndSchedule = S->WeekEndSchedule.LoadSynchronous();

	// [변경] 원래는 WeekDaySchedule->ScheduleDefs[0] 을 바로 읽었는데,
	// 프로젝트 세팅에서 애셋을 아직 안 지정했거나 배열이 비어 있으면 게임 시작과 동시에 크래시가 난다.
	// NPC 쪽 테스트 중에 실제로 걸릴 수 있는 지점이라 방어 코드를 넣었다.
	NowScheduleIndex = 0;
	if (WeekDaySchedule && WeekDaySchedule->ScheduleDefs.Num() > 0)
	{
		NowSchedule = WeekDaySchedule->ScheduleDefs[0];
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UTimeSubsystem: WeekDaySchedule이 비어 있습니다. "
				"프로젝트 세팅 > Calander Default / Schedule Default 에서 DayScheduleAsset을 지정하세요."));
	}

	InitScheduleBlock();

	NowRepeatCount = 0;
}

void UTimeSubsystem::InitScheduleBlock()
{
	// [변경] 선언만 있고 정의가 없어서, 누가 호출하는 순간 링크 에러가 나는 상태였다.
	// 설정에 ScheduleBlockData가 있는데 아무도 읽지 않아 ScheduleBlockTable이 항상 null이었으므로
	// 여기서 로드까지 함께 처리한다.
	const auto* S = GetDefault<UTimeSettings>();
	if (!S)
	{
		return;
	}

	ScheduleBlockTable = S->ScheduleBlockData.LoadSynchronous();
}

int32 UTimeSubsystem::GetDayPerMonth()
{
	if (NowMonth % 2 == 0)
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
	// [변경] 누적 일차는 달이 바뀌어도 계속 증가시킨다.
	++AbsoluteDay;

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
	// [변경] 빈 배열이면 아래 인덱싱이 전부 크래시라서 먼저 막는다.
	if (GetScheduleDefs().Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTimeSubsystem::SpendSchedule: 오늘 쓸 ScheduleDefs가 비어 있습니다."));
		return;
	}

	{
		const TArray<FScheduleDef>& Defs = GetScheduleDefs();
		NowScheduleIndex = FMath::Clamp(NowScheduleIndex, 0, Defs.Num() - 1);
		NowSchedule = Defs[NowScheduleIndex];

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

		NowScheduleIndex = FMath::Max(NowScheduleIndex, 0);

		if (NowScheduleIndex >= Defs.Num())
		{
			NowScheduleIndex = 0;
			NowRepeatCount = 0;
			SpendDay();
		}
	}

	// [변경] SpendDay가 평일<->주말을 바꿀 수 있어서 위에서 잡아둔 Defs 참조는 이 시점에 다른 배열을 가리킬 수 있다.
	// 그래서 하루가 넘어간 뒤에는 반드시 다시 가져와야 한다. (원본은 예전 배열로 인덱싱하고 있었다)
	const TArray<FScheduleDef>& NewDefs = GetScheduleDefs();
	if (NewDefs.Num() <= 0)
	{
		return;
	}
	NowScheduleIndex = FMath::Clamp(NowScheduleIndex, 0, NewDefs.Num() - 1);
	NowSchedule = NewDefs[NowScheduleIndex];

	UE_LOG(LogTemp, Log, TEXT("[Time] Day %d (Absolute %d) / Schedule %d = %s"),
		NowDay, AbsoluteDay, NowScheduleIndex, *NowSchedule.ScheduleName.ToString());

	OnScheduleChanged.Broadcast(NowSchedule.ScheduleName);
}

void UTimeSubsystem::ResetSchedule()
{
	int32 Day = GetAbsoluteDay();
	while (AbsoluteDay == Day)
	{
		SpendSchedule();
	}
}

bool UTimeSubsystem::IsWeekend() const
{
	// [변경] 원본은 NowDay % 7 을 썼는데, NowDay는 달마다 1로 리셋되므로
	// 달이 바뀌는 순간 요일 주기가 끊긴다. 누적 일차 기준으로 계산한다.
	return ((AbsoluteDay - 1) % 7) >= 5;
}

const FScheduleDef* UTimeSubsystem::GetScheduleByIndex(int32 Index) const
{
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	return Defs.IsValidIndex(Index) ? &Defs[Index] : nullptr;
}

const FScheduleDef* UTimeSubsystem::GetScheduleByTag(FGameplayTag Tag) const
{
	for (const FScheduleDef& ScheduleDef : GetScheduleDefs())
	{
		if (ScheduleDef.ScheduleName == Tag)
		{
			return &ScheduleDef;
		}
	}
	return nullptr;
}

const UDayScheduleAsset* UTimeSubsystem::GetTodaySchedule() const
{
	return IsWeekend() ? WeekEndSchedule : WeekDaySchedule;
}

const TArray<FScheduleDef>& UTimeSubsystem::GetScheduleDefs() const
{
	// 애셋이 없을 때 참조를 반환해야 하므로 빈 배열 하나를 정적으로 들고 있는다.
	static const TArray<FScheduleDef> EmptyDefs;

	const UDayScheduleAsset* Asset = GetTodaySchedule();
	return Asset ? Asset->ScheduleDefs : EmptyDefs;
}

FGameplayTag UTimeSubsystem::GetNowDayTypeTag() const
{
	// 날 종류는 시간표 애셋이 스스로 선언한다.
	// 여기서 IsWeekend()로 태그를 만들어내지 않는 이유: 시험기간/방학처럼 평일도 주말도 아닌
	// 날이 생겼을 때 이 함수를 고칠 필요가 없어야 하기 때문이다.
	const UDayScheduleAsset* Asset = GetTodaySchedule();
	return Asset ? Asset->DayTypeTag : FGameplayTag();
}

FTimeSaveData UTimeSubsystem::GetSaveData() const
{
	FTimeSaveData Data;
	Data.NowDay = NowDay;
	Data.NowMonth = NowMonth;
	Data.AbsoluteDay = AbsoluteDay;
	Data.NowScheduleIndex = NowScheduleIndex;
	Data.NowSchedule = NowSchedule;
	Data.NowRepeatCount = NowRepeatCount;
	return Data;
}

void UTimeSubsystem::LoadSaveData(const FTimeSaveData& Data)
{
	NowDay = Data.NowDay;
	NowMonth = Data.NowMonth;
	AbsoluteDay = Data.AbsoluteDay;
	NowScheduleIndex = Data.NowScheduleIndex;
	NowSchedule = Data.NowSchedule;
	NowRepeatCount = Data.NowRepeatCount;
	MaxDay = GetDayPerMonth();

	// ActivitySubsystem 등 현재 시간대를 구독하는 쪽에 로드된 값을 알려준다.
	OnScheduleChanged.Broadcast(NowSchedule.ScheduleName);
	OnDayChanged.Broadcast(NowDay);
}