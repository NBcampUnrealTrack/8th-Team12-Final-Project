// Fill out your copyright notice in the Description page of Project Settings.
//
// [변경 요약 - NPC 담당자]
//  1) AbsoluteDay 추가        : NowDay는 달이 바뀌면 1로 되돌아가서 "3일차 이벤트"를 표현할 수 없다.
//                               누적 일차가 있어야 이벤트 조건(DayEquals/DayAtLeast)을 쓸 수 있어서 추가.
//  2) 공개 Getter 추가        : NowSchedule / NowMonth / IsWeekend가 protected라 외부(NPC, 이벤트)에서 못 읽었다.
//  3) GetScheduleByIndex 시그니처 변경 : 범위 검사 없이 참조를 반환하고 있어서 인덱스가 어긋나면 즉시 크래시.
//                               const FScheduleDef* 로 바꿔 nullptr로 안전하게 실패하게 했다.
//  4) ScheduleBlockTable 로드 : 설정에 ScheduleBlockData가 있는데 아무도 읽지 않아 항상 null이었다.
//                               선언만 있고 정의가 없던 InitScheduleBlock()을 여기서 구현한다.

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

/** UTimeSubsystem의 상태를 세이브 파일에 담기 위한 스냅샷 */
USTRUCT(BlueprintType)
struct FTimeSaveData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 NowDay = 1;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 NowMonth = 1;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 AbsoluteDay = 1;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 NowScheduleIndex = 0;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	FScheduleDef NowSchedule;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	int32 NowRepeatCount = 0;
};

/**
 *
 */
UCLASS()
class FEBRICULA_API UTimeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void InitScheduleBlock();

	UPROPERTY(BlueprintAssignable)
	FDayChangedDelegate OnDayChanged;

	UPROPERTY(BlueprintAssignable)
	FScheduleChangedDelegate OnScheduleChanged;
	
	int32 GetNowScheduleAp();

	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	int32 GetDayPerMonth();

	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	void SpendDay();
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Schedule")
	void SpendSchedule();
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Schedule")
	void ResetSchedule();

	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	int32 GetNowDay() const { return NowDay; }

	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	int32 GetMaxMonth() const { return MaxMonth; }

	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	int32 GetNowMonth() const { return NowMonth; }

	/**
	 * 게임 시작(1일차)부터 한 번도 초기화되지 않는 누적 일차.
	 *
	 * NowDay는 달이 바뀔 때 1로 돌아가기 때문에 "이벤트 3일차에 발생" 같은 조건을 표현할 수 없다.
	 * 이벤트/대화 조건(DayEquals, DayAtLeast ...)은 전부 이 값을 기준으로 판정한다.
	 */
	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	int32 GetAbsoluteDay() const { return AbsoluteDay; }

	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	void GoBackOneDay();

	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Day")
	void SetAbsoluteDay(int32 NewAbsoluteDay);

	/** 주말인가. 누적 일차 기준 1~5일 = 평일, 6~7일 = 주말 */
	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	bool IsWeekend() const;

	/**
	 * 현재 시간대(기상/오전수업/점심 ...)를 나타내는 GameplayTag.
	 * 시간대 이름만 들어 있다 - 평일/주말 구분은 GetNowDayTypeTag()로 따로 물어본다.
	 */
	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Schedule")
	FGameplayTag GetNowScheduleTag() const { return NowSchedule.ScheduleName; }

	/**
	 * 오늘이 어떤 종류의 날인지 (DayType.WeekDay / DayType.WeekEnd / ...).
	 * 오늘 적용 중인 UDayScheduleAsset이 선언한 값을 그대로 돌려준다.
	 * 애셋에 DayTypeTag를 안 채웠으면 빈 태그.
	 */
	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Day")
	FGameplayTag GetNowDayTypeTag() const;

	UFUNCTION(BlueprintPure, Category = "TimeSubsystem | Schedule")
	int32 GetNowScheduleIndex() const { return NowScheduleIndex; }

	const FScheduleDef& GetNowSchedule() const { return NowSchedule; }

	/**
	 * 범위를 벗어나면 nullptr.
	 *
	 * [팀원 코드 대비 변경점] 원래는 FScheduleDef& 를 반환했는데,
	 * 배열이 비어 있거나 인덱스가 어긋나면 검사 없이 접근해서 그 자리에서 크래시가 난다.
	 * 호출부에서 if (const FScheduleDef* Def = ...) 형태로 받으면 안전하게 넘어간다.
	 */
	const FScheduleDef* GetScheduleByIndex(int32 Index) const;
	const FScheduleDef* GetScheduleByTag(FGameplayTag Tag) const;

	// ---- 세이브/로드 ----

	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Save")
	FTimeSaveData GetSaveData() const;

	/**
	 * 세이브 파일의 값으로 시간 상태를 통째로 되돌린다.
	 * 다른 시스템(ActivitySubsystem 등)이 현재 시간대를 구독하고 있으므로,
	 * 값을 바꾼 뒤 OnScheduleChanged를 다시 브로드캐스트해서 동기화한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeSubsystem | Save")
	void LoadSaveData(const FTimeSaveData& Data);


protected:

	const TArray<FScheduleDef>& GetScheduleDefs() const;

	/** 오늘 적용할 시간표 애셋. 없으면 nullptr */
	const UDayScheduleAsset* GetTodaySchedule() const;

	void RecalcFromAbsoluteDay(int32 InAbsoluteDay);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Febricula | Day")
	int32 NowDay;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 NowMonth;

	/** 게임 시작부터의 누적 일차 (달이 바뀌어도 리셋되지 않는다) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 AbsoluteDay = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 MaxMonth;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 MaxDay;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Day")
	int32 StartMonth = 1;

	//UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Weather")
	//Weather 제어 어트리뷰트 추가 예정

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Febricula | Schedule")
	int32 NowScheduleIndex = 0;
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