#pragma once
#include "GameplayTagContainer.h"
#include "ScheduleDef.generated.h"

// [skydodo] 2026-08-26 수정: ConnectLevel 제거, ScheduleName 태그 규칙 정리, ActionPoint 기본값
/**
 * 하루 시간표의 한 칸 = "시간대".
 *
 * [변경] ConnectLevel 제거.
 *   시간대는 "언제"만 정의하고, "어디"는 스폰/레벨 담당이 처리한다.
 *   한 구조체가 두 축을 같이 들고 있으면 7칸 중 실제로 레벨 이동이 필요한 1~2칸을 빼고는
 *   전부 의미 없는 값을 채워야 하고, "비어있음 = 이동 안 함"이라는 규칙이 데이터가 아니라
 *   코드에만 남는다.
 */
USTRUCT(BlueprintType)
struct FScheduleDef
{
	GENERATED_BODY()

	/**
	 * 이 시간대의 식별자.
	 *
	 * [태그 규칙] 시간대 이름만 넣는다: Schedule.WakeUp, Schedule.MorningClass ...
	 *   평일/주말 구분은 여기 섞지 않는다. 날 종류는 UDayScheduleAsset.DayTypeTag가 따로 들고 있다.
	 *   (Schedule.WeekDay.WakeUp 처럼 두 축을 합쳐두면 "기상 시간대인가"를 물을 때마다
	 *    평일용·주말용 태그를 전부 나열해야 하고, 시험기간/방학이 생기면 계속 늘어난다)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	FGameplayTag ScheduleName;

	/** 이 시간대에 쓸 수 있는 행동력 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 ActionPoint = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 RepeatCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | ScheduleDef")
	int32 BackTo = 0;

};
