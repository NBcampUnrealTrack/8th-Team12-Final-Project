// Fill out your copyright notice in the Description page of Project Settings.
//
// 게임 진행 상태(일차, 태그, 카운터, 능력치, 완료 이벤트)의 단일 소유자.
// 호감도만 UNpcRelationshipSubsystem이 따로 들고 있고, 나머지 진행 상태는 전부 여기 모인다.
// 조건 판정(EvaluateCondition)도 여기서 처리해서, 이벤트/대화/선택지가 같은 규칙을 공유하게 한다.
//
// 지금은 런타임 메모리 스텁이고, SaveGame 연동은 이 클래스 안에서 확장하면 된다.

#pragma once

#include "CoreMinimal.h"
#include "Components/PlayerStat.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dialogue/DialogueDataTypes.h"
#include "GameProgressSubsystem.generated.h"

class UTimeSubsystem;

UCLASS(BlueprintType)
class FEBRICULA_API UGameProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ---- 일차 ----
	//
	// [변경] 일차의 주인이 이 클래스에서 UTimeSubsystem으로 넘어갔다.
	// 팀원이 만든 시간 시스템과 이쪽이 각자 일차를 세고 있으면 반드시 어긋나기 때문에,
	// 여기서는 값을 소유하지 않고 UTimeSubsystem::GetAbsoluteDay()를 읽기만 한다.
	// 덕분에 기존 이벤트 테이블의 DayAtLeast 조건이 코드 수정 없이 그대로 날짜와 연동된다.

	/** UTimeSubsystem의 누적 일차. 시간 시스템이 없으면 아래 FallbackDay를 쓴다 */
	UFUNCTION(BlueprintPure, Category = "Progress")
	int32 GetCurrentDay() const;

	/**
	 * 시간 시스템이 없는 테스트 레벨에서만 의미가 있는 폴백 값 설정.
	 * UTimeSubsystem이 살아 있으면 이 값은 무시된다 - 날짜를 실제로 넘기려면
	 * UTimeSubsystem::SpendDay()를 호출해야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progress")
	void SetFallbackDay(int32 NewDay) { FallbackDay = NewDay; }

	UFUNCTION(BlueprintCallable, Category = "Progress")
	void AdvanceFallbackDay(int32 Days = 1) { FallbackDay += Days; }

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
	void SetStat(FName StatId, int32 NewValue)
	{
		const int32 Val = StaticEnum<EPlayerStat>()->GetValueByName(StatId);
		if (Val == INDEX_NONE) return;
		Stats[Val] = NewValue;
	}

	//UFUNCTION(BlueprintCallable, Category = "Progress|Stat")
	//int32 AddStat(FName StatId, int32 Delta);

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
	UFUNCTION(BlueprintCallable, Category = "Progress|Condition")
	bool EvaluateConditions(const TArray<FGameCondition>& Conditions) const;
	
	//디버그용
	UFUNCTION(BlueprintCallable, Category = "Progress|Debug")
	const TSet<FName>& GetClearedEvents() const { return CompletedEvents; }

protected:
	/** 시간 조건 판정에 쓰는 UTimeSubsystem 접근자. 없으면 nullptr */
	const UTimeSubsystem* GetTime() const;

public:

protected:
	/** UTimeSubsystem이 없을 때만 쓰는 폴백 일차 */
	UPROPERTY()
	int32 FallbackDay = 1;

	UPROPERTY()
	TSet<FName> Tags;

	UPROPERTY()
	TSet<FName> Topics;

	UPROPERTY()
	TSet<FName> CompletedEvents;

	UPROPERTY()
	TMap<FName, int32> Counters;

	UPROPERTY()
	int32 Stats[static_cast<uint8>(EPlayerStat::Max)] = {};
};
