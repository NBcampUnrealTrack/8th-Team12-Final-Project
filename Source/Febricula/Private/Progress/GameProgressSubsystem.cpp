// [skydodo] 2026-08-17 작성 (최종 수정 2026-08-26)
// 요약: 게임 진행 상태 구현 - 태그/카운터/능력치/완료 이벤트, 조건 판정
// Fill out your copyright notice in the Description page of Project Settings.

#include "Progress/GameProgressSubsystem.h"

#include "Engine/GameInstance.h"
#include "SaveGame/FebriculaSaveGame.h"
#include "GameMode/TimeSubsystem.h"
#include "GameplayTagContainer.h"
#include "Activity/ActivitySetting.h"
#include "Activity/ActivitySubsystem.h"
#include "GameMode/TimeSettings.h"
#include "Net/Core/PushModel/PushModel.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"

FRandomStream UGameProgressSubsystem::MakeRandomStream(const FMainNpcSpawnRow& NowRow, uint32 Salt) const
{
	// [skydodo] 2026-10-13 버그 수정: Salt를 섞지 않아 스폰 확률 판정(Salt 0)과 자리 섞기(Salt 1)가 같은 난수열이었다
	uint32 Hash = HashCombine(GetTypeHash(SpawnerSeed), GetTypeHash(Salt));
	Hash = HashCombine(Hash, GetTypeHash(GetCurrentDay()));
	Hash = HashCombine(Hash, FCrc::StrCrc32(*NowRow.NpcId.ToString()));
	Hash = HashCombine(Hash, FCrc::StrCrc32(*NowRow.Level.ToSoftObjectPath().ToString()));
	Hash = HashCombine(Hash, FCrc::StrCrc32(*NowRow.SpawnPointId.ToString()));
	if (const UTimeSubsystem* Time = GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		Hash = HashCombine(Hash, FCrc::StrCrc32(*Time->GetNowScheduleName().ToString()));
	}
	
	return FRandomStream(static_cast<int32>(Hash));
}

bool UGameProgressSubsystem::CheckNpcCanSpawn(const FMainNpcSpawnRow& NowRow) const
{
	if (NowRow.SpawnRate >= 100) return true;
	if (NowRow.SpawnRate <= 0) return false;
	

	return MakeRandomStream(NowRow, /*Salt=*/0).FRand() < NowRow.SpawnRate / 100.f;
}

void UGameProgressSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SpawnerSeed = FMath::Rand();	
}

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

// [skydodo] 2026-10-13 버그 수정: 범위 검사가 'Value < -1 || Value > Max'라 -1(없는 이름)과 Max가 통과해 Stats[] 범위 밖을 읽고 썼다. Get/Set/AddStat이 이 함수 하나로 검사한다
bool UGameProgressSubsystem::TryGetStatIndex(FName StatId, int32& OutIndex)
{
	// GetValueByName은 없는 이름이면 INDEX_NONE(-1)을 준다. Max는 배열 크기라 유효한 스탯이 아니다.
	const int64 Value = StaticEnum<EPlayerStat>()->GetValueByName(StatId);
	if (Value < 0 || Value >= static_cast<int64>(EPlayerStat::Max))
	{
		UE_LOG(LogTemp, Warning, TEXT("알 수 없는 스탯: %s"), *StatId.ToString());
		return false;
	}
	OutIndex = static_cast<int32>(Value);
	return true;
}

int32 UGameProgressSubsystem::GetStat(FName StatId) const
{
	int32 Index;
	// [skydodo] 2026-10-13 리팩토링: TryGetStatIndex가 이미 경고를 남기므로 같은 경고를 두 번 찍지 않는다
	if (!TryGetStatIndex(StatId, Index))
	{
		return 0;
	}
	return Stats[Index];
}

int32 UGameProgressSubsystem::AddStat(FName StatId, int32 Delta)
{
	int32 Index;
	if (!TryGetStatIndex(StatId, Index))
	{
		return 0;
	}
 	Stats[Index] += Delta;
 	return Stats[Index];
}

bool UGameProgressSubsystem::IsEventCompleted(FName EventId) const
{
	bool Temp = CompletedEvents.Contains(EventId);
	return Temp;
}

// [skydodo] 2026-10-13 버그 수정: APAtLeat/APAtMost가 ActivitySubsystem이 없으면 다음 case로 떨어졌다 -> return false. ScheduleAfter에 TimeSubsystem null 검사 추가
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
		
	case EGameConditionType::ScheduleAfter:
	{
		const UTimeSubsystem* TimeSubsystem = GetTime();
		if (!TimeSubsystem) return false;

		int32 NowIndex = TimeSubsystem->GetScheduleIndex();
		int32 TargetIndex = TimeSubsystem->GetScheduleIndex(Condition.Key);
			
			
		if (TargetIndex == -1) return false;
		if (TargetIndex < NowIndex) return true;
		return false;
	}

	case EGameConditionType::NextScheduleIs:
	{
		if (UTimeSubsystem* TimeSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UTimeSubsystem>())
		{
			TArray<FScheduleDef> ScheduleDefs = TimeSubsystem->GetScheduleDefs();
			if (ScheduleDefs.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("GameProcess Subsystem: ScheduleDef가 비어있음"));
				return false;
			}
			
			FName NextScheduleName = TimeSubsystem->GetNextScheduleName();
			
			if (NextScheduleName == NAME_None)
			{
				UE_LOG(LogTemp, Warning, TEXT("GameProgress Subsystem: NextScheduleName이 비어있음"));
				return false;
			}
			return NextScheduleName == Condition.Key;
		}
		return false;
	}
		
	case EGameConditionType::ScheduleIsChildOf:
	{
		const UTimeSubsystem* Time = GetTime();
		if (!Time)
		{
			return false;
		}
		FGameplayTagContainer Block = Time->GetScheduleBlock(Condition.Key);
		if (Block == FGameplayTagContainer::EmptyContainer)
		{
			UE_LOG(LogTemp, Warning, TEXT("GameProgress Subsystem: 존재하지 않는 스케쥴 블록 %s"), *Condition.Key.ToString());
			return false;
		}
		return Block.HasTagExact(Time->GetNowSchedule().ScheduleName);
		// const FGameplayTag Parent =
		// 	FGameplayTag::RequestGameplayTag(Condition.Key, /*ErrorIfNotFound=*/false);
		// return Parent.IsValid() && Time->GetNowScheduleTag().MatchesTag(Parent);
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
		return false;

	case EGameConditionType::APAtMost:
		if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
		{
			return Condition.Value >= ActivitySubsystem->GetCurrentAP();
		}
		return false;

		
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
		
	case EGameConditionType::PreviousLevelIs:
	{
		if (MoveHistory.IsEmpty()) return false;
		FName LastHistory = MoveHistory.Last();
		UE_LOG(LogTemp, Warning, TEXT("마지막 방문 레벨: %s"), *LastHistory.ToString());
		if (Condition.Key == "Library")
		{
			UE_LOG(LogTemp, Warning, TEXT("Last History == Condition.key: %d"), LastHistory == Condition.Key);
			if (LastHistory == Condition.Key)
			{
				return true;
			}			
		}
		if (LastHistory == Condition.Key)
		{
			return true;
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

FGameProgressSaveData UGameProgressSubsystem::GetSaveData() const
{
	FGameProgressSaveData Data;
	Data.Tags = Tags;
	Data.Topics = Topics;
	Data.CompletedEvents = CompletedEvents;
	Data.Counters = Counters;
	
	const UEnum* Enum = StaticEnum<EPlayerStat>();
	for (int32 i = 0; i < static_cast<int32>(EPlayerStat::Max); ++i)
	{
		Data.Stats.Add(FName(Enum->GetNameStringByIndex(i)), Stats[i]);
	}

	Data.FallbackDay = FallbackDay;
	return Data;
}

void UGameProgressSubsystem::LoadSaveData(const FGameProgressSaveData& Data)
{
	Tags = Data.Tags;
	Topics = Data.Topics;
	CompletedEvents = Data.CompletedEvents;
	Counters = Data.Counters;
	
	const UEnum* Enum = StaticEnum<EPlayerStat>();
	for (int32 i = 0; i < static_cast<int32>(EPlayerStat::Max); ++i)
	{
		const FName Name(Enum->GetNameStringByIndex(i));
		Stats[i] = Data.Stats.FindRef(Name);   // 없으면 0
	}
	FallbackDay = Data.FallbackDay;
}
