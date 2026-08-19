// Fill out your copyright notice in the Description page of Project Settings.
//
// 게임 진행 상태(일차, 태그, 카운터, 능력치, 완료 이벤트)의 단일 소유자.
// 호감도만 UNpcRelationshipSubsystem이 따로 들고 있고, 나머지 진행 상태는 전부 여기 모인다.
// 조건 판정(EvaluateCondition)도 여기서 처리해서, 이벤트/대화/선택지가 같은 규칙을 공유하게 한다.
//
// 지금은 런타임 메모리 스텁이고, SaveGame 연동은 이 클래스 안에서 확장하면 된다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dialogue/DialogueDataTypes.h"
#include "GameProgressSubsystem.generated.h"

UCLASS(BlueprintType)
class FEBRICULA_API UGameProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ---- 일차 ----

	UFUNCTION(BlueprintPure, Category = "Progress")
	int32 GetCurrentDay() const { return CurrentDay; }

	UFUNCTION(BlueprintCallable, Category = "Progress")
	void SetCurrentDay(int32 NewDay) { CurrentDay = NewDay; }

	UFUNCTION(BlueprintCallable, Category = "Progress")
	void AdvanceDay(int32 Days = 1) { CurrentDay += Days; }

	// ---- 태그 (일름보 / 철두철미 / 다정다감 / 불요불굴 ...) ----

	UFUNCTION(BlueprintPure, Category = "Progress|Tag")
	bool HasTag(FName Tag) const { return Tags.Contains(Tag); }

	UFUNCTION(BlueprintCallable, Category = "Progress|Tag")
	void GrantTag(FName Tag) { Tags.Add(Tag); }

	UFUNCTION(BlueprintCallable, Category = "Progress|Tag")
	void RemoveTag(FName Tag) { Tags.Remove(Tag); }

	// ---- 카운터 (교칙위반 횟수, 미담 횟수 등) ----

	UFUNCTION(BlueprintPure, Category = "Progress|Counter")
	int32 GetCounter(FName CounterId) const;

	UFUNCTION(BlueprintCallable, Category = "Progress|Counter")
	int32 AddCounter(FName CounterId, int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Progress|Counter")
	void SetCounter(FName CounterId, int32 NewValue) { Counters.Add(CounterId, NewValue); }

	// ---- 능력치 (매력 / 담력 / 행운 ...) ----

	UFUNCTION(BlueprintPure, Category = "Progress|Stat")
	int32 GetStat(FName StatId) const;

	UFUNCTION(BlueprintCallable, Category = "Progress|Stat")
	void SetStat(FName StatId, int32 NewValue) { Stats.Add(StatId, NewValue); }

	UFUNCTION(BlueprintCallable, Category = "Progress|Stat")
	int32 AddStat(FName StatId, int32 Delta);

	// ---- 이벤트 완료 기록 ----

	UFUNCTION(BlueprintPure, Category = "Progress|Event")
	bool IsEventCompleted(FName EventId) const { return CompletedEvents.Contains(EventId); }

	UFUNCTION(BlueprintCallable, Category = "Progress|Event")
	void MarkEventCompleted(FName EventId) { CompletedEvents.Add(EventId); }

	// ---- 대화 주제 (대화 주제 "이서아" 획득 같은 것) ----

	UFUNCTION(BlueprintPure, Category = "Progress|Topic")
	bool HasTopic(FName TopicId) const { return Topics.Contains(TopicId); }

	UFUNCTION(BlueprintCallable, Category = "Progress|Topic")
	void GrantTopic(FName TopicId) { Topics.Add(TopicId); }

	// ---- 조건 판정 ----

	/** 조건 하나를 평가한다. 관계(RelationshipAtLeast)는 UNpcRelationshipSubsystem에 위임한다 */
	UFUNCTION(BlueprintCallable, Category = "Progress|Condition")
	bool EvaluateCondition(const FGameCondition& Condition) const;

	/** 조건 배열 전체를 AND로 평가한다. 배열이 비어 있으면 true */
	bool EvaluateConditions(const TArray<FGameCondition>& Conditions) const;

protected:
	UPROPERTY()
	int32 CurrentDay = 1;

	UPROPERTY()
	TSet<FName> Tags;

	UPROPERTY()
	TSet<FName> Topics;

	UPROPERTY()
	TSet<FName> CompletedEvents;

	UPROPERTY()
	TMap<FName, int32> Counters;

	UPROPERTY()
	TMap<FName, int32> Stats;
};
