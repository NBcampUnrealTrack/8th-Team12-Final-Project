// Fill out your copyright notice in the Description page of Project Settings.

#include "Progress/GameProgressSubsystem.h"

#include "Engine/GameInstance.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"

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
	const int32* Found = Stats.Find(StatId);
	return Found ? *Found : 0;
}

int32 UGameProgressSubsystem::AddStat(FName StatId, int32 Delta)
{
	int32& Value = Stats.FindOrAdd(StatId);
	Value += Delta;
	return Value;
}

bool UGameProgressSubsystem::EvaluateCondition(const FGameCondition& Condition) const
{
	switch (Condition.Type)
	{
	case EGameConditionType::None:
		return true;

	case EGameConditionType::DayAtLeast:
		return CurrentDay >= Condition.Value;

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

	case EGameConditionType::StatAtLeast:
		return GetStat(Condition.Key) >= Condition.Value;

	case EGameConditionType::RelationshipAtLeast:
		{
			// 호감도는 이 서브시스템이 아니라 UNpcRelationshipSubsystem이 소유하므로 위임한다.
			if (const UGameInstance* GI = GetGameInstance())
			{
				if (const UNpcRelationshipSubsystem* Relationship = GI->GetSubsystem<UNpcRelationshipSubsystem>())
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
