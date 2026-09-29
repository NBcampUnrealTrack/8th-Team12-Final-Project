// Fill out your copyright notice in the Description page of Project Settings.

#include "Progress/GameProgressSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameMode/TimeSubsystem.h"
#include "GameplayTagContainer.h"
#include "Activity/ActivitySetting.h"
#include "Activity/ActivitySubsystem.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"

const UTimeSubsystem* UGameProgressSubsystem::GetTime() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UTimeSubsystem>() : nullptr;
}

int32 UGameProgressSubsystem::GetCurrentDay() const
{
	// 일차의 주인은 UTimeSubsystem이다. 여긴 읽기만 한다.
	if (const UTimeSubsystem* Time = GetTime())
	{
		return Time->GetAbsoluteDay();
	}
	return FallbackDay;
}

int32 UGameProgressSubsystem::GetCounter(FName CounterId) const
{
	const int32* Found = Counters.Find(CounterId);
	return Found ? *Found : 0;
}

int32 UGameProgressSubsystem::AddCounter(FName CounterId, int32 Delta)
{
	int32& Value = Counters.FindOrAdd(CounterId);
	Value += Delta;
	return Value;
}

int32 UGameProgressSubsystem::GetStat(FName StatId) const
{
	// const int32* Found = Stats.Find(StatId);
	// return Found ? *Found : 0;
	return 0;
}

// int32 UGameProgressSubsystem::AddStat(FName StatId, int32 Delta)
// {
// 	
// 	// int32& Value = Stats.FindOrAdd(StatId);
// 	// Value += Delta;
// 	// return Value;
// 	return 0;
// }

bool UGameProgressSubsystem::EvaluateCondition(const FGameCondition& Condition) const
{
	switch (Condition.Type)
	{
	case EGameConditionType::None:
		return true;

	case EGameConditionType::DayAtLeast:
		return GetCurrentDay() >= Condition.Value;

	// ---- 시간/날짜 (UTimeSubsystem 연동) ----

	case EGameConditionType::DayEquals:
		return GetCurrentDay() == Condition.Value;

	case EGameConditionType::DayAtMost:
		return GetCurrentDay() <= Condition.Value;

	case EGameConditionType::MonthEquals:
		{
			const UTimeSubsystem* Time = GetTime();
			return Time ? (Time->GetNowMonth() == Condition.Value) : false;
		}

	case EGameConditionType::ScheduleIs:
		{
			const UTimeSubsystem* Time = GetTime();
			if (!Time)
			{
				return false;
			}
			// Key에 적힌 문자열을 GameplayTag로 변환해서 현재 시간대와 비교한다.
			// 등록되지 않은 태그 이름이면 아래에서 경고가 나가고 false가 된다.
			const FGameplayTag Required =
				FGameplayTag::RequestGameplayTag(Condition.Key, /*ErrorIfNotFound=*/false);
			if (!Required.IsValid())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("ScheduleIs 조건의 태그 '%s'를 찾을 수 없습니다. GameplayTag가 등록됐는지 확인하세요."),
					*Condition.Key.ToString());
				return false;
			}
			return Time->GetNowScheduleTag() == Required;
		}

	case EGameConditionType::ScheduleIsChildOf:
		{
			const UTimeSubsystem* Time = GetTime();
			if (!Time)
			{
				return false;
			}
			const FGameplayTag Parent =
				FGameplayTag::RequestGameplayTag(Condition.Key, /*ErrorIfNotFound=*/false);
			return Parent.IsValid() && Time->GetNowScheduleTag().MatchesTag(Parent);
		}

	case EGameConditionType::DayTypeIs:
		{
			const UTimeSubsystem* Time = GetTime();
			if (!Time)
			{
				return false;
			}
			const FGameplayTag Required =
				FGameplayTag::RequestGameplayTag(Condition.Key, /*ErrorIfNotFound=*/false);
			if (!Required.IsValid())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("DayTypeIs 조건의 태그 '%s'를 찾을 수 없습니다. GameplayTag가 등록됐는지 확인하세요."),
					*Condition.Key.ToString());
				return false;
			}
			return Time->GetNowDayTypeTag() == Required;
		}

	case EGameConditionType::DayTypeIsChildOf:
		{
			const UTimeSubsystem* Time = GetTime();
			if (!Time)
			{
				return false;
			}
			const FGameplayTag Parent =
				FGameplayTag::RequestGameplayTag(Condition.Key, /*ErrorIfNotFound=*/false);
			return Parent.IsValid() && Time->GetNowDayTypeTag().MatchesTag(Parent);
		}

	case EGameConditionType::IsWeekend:
		{
			const UTimeSubsystem* Time = GetTime();
			return Time ? Time->IsWeekend() : false;
		}

	case EGameConditionType::IsWeekday:
		{
			const UTimeSubsystem* Time = GetTime();
			return Time ? !Time->IsWeekend() : true;
		}

	case EGameConditionType::EventCompleted:
		return IsEventCompleted(Condition.Key);

	case EGameConditionType::EventNotCompleted:
		return !IsEventCompleted(Condition.Key);

	case EGameConditionType::HasTag:
		return HasTag(Condition.Key);

	case EGameConditionType::NotHasTag:
		return !HasTag(Condition.Key);

	case EGameConditionType::CounterAtLeast:
		return GetCounter(Condition.Key) >= Condition.Value;

	case EGameConditionType::CounterAtMost:
		return GetCounter(Condition.Key) <= Condition.Value;

	case EGameConditionType::APAtLeat:
		if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
		{
			return Condition.Value <= ActivitySubsystem->GetCurrentAP();
		}
		
	case EGameConditionType::APAtMost:
		if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
		{
			return Condition.Value >= ActivitySubsystem->GetCurrentAP();
		}

		
	case EGameConditionType::StatAtLeast:
		return GetStat(Condition.Key) >= Condition.Value;

	case EGameConditionType::RelationshipAtLeast:
		{
			// 호감도는 이 서브시스템이 아니라 UNpcRelationshipSubsystem이 소유하므로 위임한다.
			if (const UGameInstance* GI = GetGameInstance())
			{
				if (const UNpcAffinitySubsystem* Relationship = GI->GetSubsystem<UNpcAffinitySubsystem>())
				{
					const ENpcRelationshipLevel Required =
						static_cast<ENpcRelationshipLevel>(FMath::Clamp(Condition.Value, 0, static_cast<int32>(ENpcRelationshipLevel::Intimate)));
					return Relationship->IsRelationshipAtLeast(Condition.Key, Required);
				}
			}
			return false;
		}

	case EGameConditionType::RollCheck:
		{
			// 1d20 + (Key 능력치 보정) >= Value 이면 성공.
			// 실패 시 친밀도 하락 같은 처리는 분기(FDialogueBranch) 쪽에서 하도록 데이터에 맡긴다.
			const int32 Bonus = Condition.Key.IsNone() ? 0 : GetStat(Condition.Key);
			const int32 Roll = FMath::RandRange(1, 20);
			return (Roll + Bonus) >= Condition.Value;
		}
		
	case EGameConditionType::NowLevel:
	{
			const UActivitySetting* ActivitySetting = GetDefault<UActivitySetting>();
			if (!ActivitySetting) return false;
			
			if (const TSoftObjectPtr<UWorld>* TargetLevel = ActivitySetting->Levels.Find(Condition.Key))
			{
				if (TargetLevel->IsNull()) return false;
				if (const UWorld* NowLevel = GetWorld())
				{
					const FString NowName = UWorld::RemovePIEPrefix(NowLevel->GetOutermost()->GetName());
					return NowName == TargetLevel->ToSoftObjectPath().GetLongPackageName();
				}
			}
			return false;
	}
		
	/** 
	 * 이지민 수정(2026. 08. 26)
	 *  NPC가 아닌 상호작용 가능한 일반 액터와의 액션도 처리하기 위해 추가된 로직	 
	 *  문 / 노트북 / 기타 액티비티용 액터 등이 FGameAction 체계 공유하기 위해 추가됨
	 */
	
	default:
		return true;
	}
}

bool UGameProgressSubsystem::EvaluateConditions(const TArray<FGameCondition>& Conditions) const
{
	for (const FGameCondition& Condition : Conditions)
	{
		if (!EvaluateCondition(Condition))
		{
			return false;
		}
	}
	return true;
}
