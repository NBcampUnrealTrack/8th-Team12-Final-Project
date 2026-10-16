// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/TimeSubsystem.h"

#include "GameMode/DayScheduleAsset.h"
#include "GameMode/ScheduleBlockRow.h"
#include "GameMode/TimeSettings.h"
#include "Progress/GameProgressSubsystem.h"

// [skydodo] 2026-10-13 리팩토링: SpendSchedule과 GetNextScheduleName이 "다음 칸" 규칙을 각자 복사해 들고 있어
// 한쪽만 고치면 미리보기와 실제 진행이 어긋났다 -> 한 곳에서 계산한다
namespace TimeScheduleStep
{
	struct FStep
	{
		/** 다음 칸 인덱스. Defs.Num() 이상이면 오늘 시간표가 끝났다는 뜻 (호출하는 쪽이 하루를 넘긴다) */
		int32 Index = 0;
		int32 RepeatCount = 0;
	};

	/** Index 칸 다음 칸. BackTo가 있으면 RepeatCount번 될 때까지 BackTo칸 앞으로 되돌아간다 */
	static FStep Next(const TArray<FScheduleDef>& Defs, int32 Index, int32 RepeatCount)
	{
		const FScheduleDef& Def = Defs[Index];
		FStep Step{ Index, RepeatCount };
		if (Def.BackTo > 0 && ++Step.RepeatCount < Def.RepeatCount)
		{
			Step.Index -= Def.BackTo;
		}
		else
		{
			if (Def.BackTo > 0)
			{
				Step.RepeatCount = 0;
			}
			++Step.Index;
		}
		Step.Index = FMath::Max(Step.Index, 0);
		return Step;
	}
}

// [skydodo] 2026-08-26 수정: 누적 일차(AbsoluteDay) 초기화, 시간표 애셋이 비었을 때 크래시 방어
void UTimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const auto* S = GetDefault<UTimeSettings>();

	NowDay = 1;
	AbsoluteDay = 1;

	MaxMonth = S->MaxMonth;

	StartMonth = S->StartMonth;
	NowMonth = S->StartMonth;
	MaxDay = GetDayPerMonth();

	
	WeekDaySchedule = S->WeekDaySchedule.LoadSynchronous();
	WeekEndSchedule = S->WeekEndSchedule.LoadSynchronous();
	
	ScheduleGroupMap = S->ScheduleGroupMap;

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

// [skydodo] 2026-08-26 추가: 선언만 있던 InitScheduleBlock 정의 + ScheduleBlockTable 로드
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

// [skydodo] 2026-08-26 수정: 누적 일차 증가
// [skydodo] 2026-10-13 버그 수정: 12월 다음이 13월이 됐다 -> 1월로 되돌림
void UTimeSubsystem::SpendDay()
{
	// [변경] 누적 일차는 달이 바뀌어도 계속 증가시킨다.
	++AbsoluteDay;

	if (++NowDay > MaxDay)
	{
		NowDay = 1;
		if (++NowMonth > 12)
		{
			NowMonth = 1;
		}
		MaxDay = GetDayPerMonth();
	}
	OnDayChanged.Broadcast(NowDay);
}

// [skydodo] 2026-08-26 수정: 빈 시간표 방어, 날이 바뀐 뒤 시간표 다시 읽기, 인덱스 범위 보정, 로그
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

		const TimeScheduleStep::FStep Step = TimeScheduleStep::Next(Defs, NowScheduleIndex, NowRepeatCount);
		NowScheduleIndex = Step.Index;
		NowRepeatCount = Step.RepeatCount;

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


int32 UTimeSubsystem::GetNowScheduleAp()
{
	return NowSchedule.ActionPoint;
}

// [skydodo] 2026-10-13 버그 수정: 시간표가 비어 있으면 SpendSchedule이 바로 return해 무한 루프
void UTimeSubsystem::ResetSchedule()
{
	// 시간표가 비어 있으면 SpendSchedule이 아무것도 하지 않아 아래 루프가 끝나지 않는다.
	if (GetScheduleDefs().Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Time] ResetSchedule: 시간표가 비어 있어 건너뜀"));
		return;
	}
	int32 Day = GetAbsoluteDay();
	while (AbsoluteDay == Day)
	{
		SpendSchedule();
	}
}

// [skydodo] 2026-10-13 버그 수정: 범위를 맞춘 ScheduleIndex 대신 NowScheduleIndex로 인덱싱했고,
// 하루의 마지막 시간대에서는 내일 시간표가 아니라 오늘 시간표의 0번을 돌려줘 평일<->주말 경계에서 틀렸다.
// SpendSchedule과 같은 규칙으로 다음 칸을 계산하되 상태는 건드리지 않는다.
FName UTimeSubsystem::GetNextScheduleName()
{
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	if (Defs.Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTimeSubsystem::GetNextScheduleName: 오늘 쓸 ScheduleDefs가 비어 있습니다."));
		return NAME_None;
	}

	const TimeScheduleStep::FStep Step = TimeScheduleStep::Next(Defs, FMath::Clamp(NowScheduleIndex, 0, Defs.Num() - 1), NowRepeatCount);
	if (Step.Index < Defs.Num())
	{
		return FName(Defs[Step.Index].ScheduleName.ToString());
	}

	// 시간표 끝 -> 내일 시간표의 첫 칸. 내일이 주말인지는 IsWeekend()와 같은 식을 누적 일차 + 1로 계산한다.
	const bool bTomorrowWeekend = (AbsoluteDay % 7) >= 5;
	const UDayScheduleAsset* Tomorrow = bTomorrowWeekend ? WeekEndSchedule : WeekDaySchedule;
	if (!Tomorrow || Tomorrow->ScheduleDefs.Num() <= 0)
	{
		return NAME_None;
	}
	return FName(Tomorrow->ScheduleDefs[0].ScheduleName.ToString());
}

// [skydodo] 2026-08-26 추가: 누적 일차 기준 주말 판정
bool UTimeSubsystem::IsWeekend() const
{
	// [변경] 원본은 NowDay % 7 을 썼는데, NowDay는 달마다 1로 리셋되므로
	// 달이 바뀌는 순간 요일 주기가 끊긴다. 누적 일차 기준으로 계산한다.
	return ((AbsoluteDay - 1) % 7) >= 5;
}

FText UTimeSubsystem::GetDayName() const
{
	const UEnum* E = StaticEnum<EDateName>();
	const int32 Count = E->NumEnums() - 1;    
	int32 Index = 0;
	Index = (AbsoluteDay-1) / 7;

	if (Index >= 0 && Index < Count)
	{
		return E->GetDisplayNameTextByIndex(Index);        // 식별자 이름
	}

	return FText::GetEmpty();
}

// 시간대에 따른 태양각 조절을 위한 임시 로직
// 최종발표회 후에라도 수정할 것

FString UTimeSubsystem::GetNowSchedulePoint() const
{
	FString Text;
	if (UGameProgressSubsystem* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		if (Progress->EvaluateCondition(FGameCondition(EGameConditionType::ScheduleIsChildOf, TEXT("MorningTime"), 0)))
		{
			Text = TEXT("MorningTime"); 
		}
		else if (Progress->EvaluateCondition(FGameCondition(EGameConditionType::ScheduleIsChildOf, TEXT("AfternoonTime"), 0)))
		{
			Text = TEXT("AfternoonTime");
		}
		else
		{
			Text = TEXT("DinnerTime");
		}
		
	}
	return Text;
}

// [skydodo] 2026-08-26 수정: 참조 반환 -> 포인터 반환 (범위 밖이면 nullptr)
const FScheduleDef* UTimeSubsystem::GetScheduleByIndex(int32 Index) const
{
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	return Defs.IsValidIndex(Index) ? &Defs[Index] : nullptr;
}

// [skydodo] 2026-08-26 수정: 포인터 반환 (못 찾으면 nullptr)
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

// [skydodo] 2026-08-26 추가: 오늘(평일/주말) 시간표 애셋
const UDayScheduleAsset* UTimeSubsystem::GetTodaySchedule() const
{
	return IsWeekend() ? WeekEndSchedule : WeekDaySchedule;
}

// [skydodo] 2026-08-26 추가: 애셋이 없을 때 빈 배열을 돌려주는 안전한 시간표 조회
const TArray<FScheduleDef>& UTimeSubsystem::GetScheduleDefs() const
{
	// 애셋이 없을 때 참조를 반환해야 하므로 빈 배열 하나를 정적으로 들고 있는다.
	static const TArray<FScheduleDef> EmptyDefs;

	const UDayScheduleAsset* Asset = GetTodaySchedule();
	return Asset ? Asset->ScheduleDefs : EmptyDefs;
}

void UTimeSubsystem::Debug_JumpToDay(int32 TargetDay)
{
#if !UE_BUILD_SHIPPING
	const int32 Clamped = FMath::Min(TargetDay, MaxDay);
	if (Clamped != TargetDay)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Time] JumpToDay: %d월은 %d일까지 — %d일에서 멈춤"),
			NowMonth, MaxDay, Clamped);
	}
	if (Clamped <= NowDay)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Time] JumpToDay: 앞으로만 갈 수 있음 (현재 %d일, 요청 %d일)"),
			NowDay, TargetDay);
		return;
	}

	const int32 Delta = Clamped - NowDay;
	NowDay = Clamped;
	AbsoluteDay += Delta;

	// 새 날의 첫 시간대부터 시작.
	// ⚠ AbsoluteDay를 바꾼 **뒤에** 읽어야 평일/주말이 바뀐 경우 맞는 시간표를 고른다
	NowScheduleIndex = 0;
	NowRepeatCount = 0;
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	if (Defs.Num() > 0)
	{
		NowSchedule = Defs[0];
	}

	UE_LOG(LogTemp, Log, TEXT("[Time] JumpToDay: %d월 %d일 (Absolute %d, +%d일) / %s"),
		NowMonth, NowDay, AbsoluteDay, Delta, *NowSchedule.ScheduleName.ToString());

	// LoadSaveData와 같은 순서로 동기화
	OnScheduleChanged.Broadcast(NowSchedule.ScheduleName);
	OnDayChanged.Broadcast(NowDay);
#endif

}

// [skydodo] 2026-08-26 추가: 오늘의 날 종류 태그
FGameplayTag UTimeSubsystem::GetNowDayTypeTag() const
{
	// 날 종류는 시간표 애셋이 스스로 선언한다.
	// 여기서 IsWeekend()로 태그를 만들어내지 않는 이유: 시험기간/방학처럼 평일도 주말도 아닌
	// 날이 생겼을 때 이 함수를 고칠 필요가 없어야 하기 때문이다.
	const UDayScheduleAsset* Asset = GetTodaySchedule();
	return Asset ? Asset->DayTypeTag : FGameplayTag();
}

// [skydodo] 2026-10-13 버그 수정: 시간표 애셋이 없으면 null 역참조 크래시, 등록 안 된 태그 이름이면 RequestGameplayTag가 ensure를 터뜨렸다
int32 UTimeSubsystem::GetTargetScheduleIndex(FName Target)
{
	const FGameplayTag TargetTag = FGameplayTag::RequestGameplayTag(Target, /*ErrorIfNotFound=*/false);
	if (!TargetTag.IsValid())
	{
		UE_LOG(LogTemp, Log, TEXT("TimeSubsystem: 존재하지 않는 태그 %s"), *Target.ToString());
		return -1;
	}
	// GetScheduleDefs()는 애셋이 없으면 빈 배열을 준다
	const TArray<FScheduleDef>& Defs = GetScheduleDefs();
	for (int32 i = 0; i < Defs.Num(); i++)
	{
		if (Defs[i].ScheduleName == TargetTag)
		{
			return i;
		}
	}
	return -1;
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

void UTimeSubsystem::GoBackOneDay()
{
	if (AbsoluteDay <= 1) return;
	--AbsoluteDay;
	RecalcFromAbsoluteDay(AbsoluteDay);
	OnDayChanged.Broadcast(NowDay);
}

void UTimeSubsystem::SetAbsoluteDay(int32 NewAbsoluteDay)
{
	AbsoluteDay = FMath::Max(NewAbsoluteDay, 1);
	RecalcFromAbsoluteDay(AbsoluteDay);
	OnDayChanged.Broadcast(NowDay);
}

void UTimeSubsystem::RecalcFromAbsoluteDay(int32 InAbsoluteDay)
{
	int32 Remaining = InAbsoluteDay - 1;
	int32 Month = StartMonth;
	while (true)
	{
		const int32 DaysInMonth = (Month % 2 == 1) ? 31 : 30;
		if (Remaining < DaysInMonth)
		{
			NowDay = Remaining + 1;
			NowMonth = Month;
			MaxDay = DaysInMonth;
			return;
		}
		Remaining -= DaysInMonth;
		if (++Month > 12) Month = 1;
	}
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
