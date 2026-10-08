// [skydodo] 2026-10-08 작성
// 요약: 메인 NPC 테스트용 콘솔 명령(mainnpc.*) - 날짜/시간대 넘기기, 레벨 이동, 호감도·능력치 조작
// Fill out your copyright notice in the Description page of Project Settings.
//
// 메인 NPC 스케줄 테스트용 콘솔 명령 (mainnpc.*). PIE 중에 ` 키 콘솔에서 친다.
//
//   mainnpc.Status                         지금 레벨/일차/시간대, 호감도, 능력치, 세워진 NPC
//   mainnpc.NextDay [횟수]                  다음 날 아침으로 (시간대를 끝까지 넘겨서 날을 바꾼다)
//   mainnpc.NextSchedule [횟수]             다음 시간대로
//   mainnpc.Travel <A|B|레벨 경로>           레벨 이동 (A/B는 MainNpcTest 테스트 맵)
//   mainnpc.Affinity <NpcId> <증감>          호감도 수치 더하기
//   mainnpc.Relation <NpcId> <단계>          호감도를 단계로 맞추기 (Wary/Neutral/Favor/Trust/Intimate)
//   mainnpc.Stat <능력치> <증감>             능력치 더하기 (Health/Mental/Int/Luck/Courage/HandWork/Speech)
//   mainnpc.Tag <태그> [remove]             진행 태그 부여/제거
//   mainnpc.Event <EventId>                 이벤트 완료 처리
//   mainnpc.AIState <NpcId> <상태> [RouteId] Blackboard AIState 강제 (Idle/Patrol/Guide/Chase)
//   mainnpc.Refresh                         스포너가 지금 상태로 다시 배치
//
// 상태를 바꾸는 명령은 끝에 스포너 갱신까지 걸어 주므로, 치자마자 NPC 자리가 바뀌는 걸 볼 수 있다.

#include "CoreMinimal.h"
#include "Components/PlayerStat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameMode/TimeSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/MainNpc/MainNpcAIController.h"
#include "NPC/MainNpc/MainNpcCharacter.h"
#include "NPC/MainNpc/MainNpcSpawner.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"
#include "Progress/GameProgressSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMainNpcDebug, Log, All);

namespace MainNpcDebug
{
	/** 출력 로그와 화면에 같이 찍는다. 게임 화면만 보고 있어도 결과가 보이게 */
	void Print(const FString& Message, const FColor& Color = FColor::Cyan)
	{
		UE_LOG(LogMainNpcDebug, Display, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, Color, Message);
		}
	}

	template <typename T>
	T* GetGameInstanceSubsystem(const UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<T>() : nullptr;
	}

	/** 바뀐 상태를 바로 보이게 스포너들에 갱신을 건다 */
	void RefreshSpawners(UWorld* World)
	{
		for (TActorIterator<AMainNpcSpawner> It(World); It; ++It)
		{
			It->RequestRefresh();
		}
	}

	AMainNpcCharacter* FindNpc(UWorld* World, FName NpcId)
	{
		for (TActorIterator<AMainNpcCharacter> It(World); It; ++It)
		{
			if (It->GetNpcId() == NpcId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	int32 ArgAsInt(const TArray<FString>& Args, int32 Index, int32 Default)
	{
		return Args.IsValidIndex(Index) ? FCString::Atoi(*Args[Index]) : Default;
	}

	/** 스케줄 테이블에 나오는 NPC ID 전부 + 레벨에 있는 메인 NPC ID */
	TArray<FName> CollectNpcIds(UWorld* World)
	{
		TSet<FName> Ids;
		for (TActorIterator<AMainNpcSpawner> It(World); It; ++It)
		{
			if (const UDataTable* Table = It->GetScheduleTable())
			{
				Table->ForeachRow<FMainNpcSpawnRow>(TEXT("MainNpcDebug"),
					[&Ids](const FName&, const FMainNpcSpawnRow& Row) { Ids.Add(Row.NpcId); });
			}
		}
		for (TActorIterator<AMainNpcCharacter> It(World); It; ++It)
		{
			Ids.Add(It->GetNpcId());
		}
		Ids.Remove(NAME_None);
		return Ids.Array();
	}

	// ---- 명령 ----

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		const UTimeSubsystem* Time = GetGameInstanceSubsystem<UTimeSubsystem>(World);
		const UGameProgressSubsystem* Progress = GetGameInstanceSubsystem<UGameProgressSubsystem>(World);
		const UNpcAffinitySubsystem* Affinity = GetGameInstanceSubsystem<UNpcAffinitySubsystem>(World);

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("레벨 %s  |  %d일차  |  시간대 %s"),
			*UGameplayStatics::GetCurrentLevelName(World),
			Progress ? Progress->GetCurrentDay() : -1,
			Time ? *Time->GetNowScheduleTag().ToString() : TEXT("(시간 시스템 없음)")));

		if (Affinity)
		{
			for (const FName& NpcId : CollectNpcIds(World))
			{
				Lines.Add(FString::Printf(TEXT("  호감도 %s = %.0f (%s)"), *NpcId.ToString(),
					Affinity->GetAffinityValue(NpcId),
					*UEnum::GetDisplayValueAsText(Affinity->GetRelationshipLevel(NpcId)).ToString()));
			}
		}

		if (Progress)
		{
			FString StatLine = TEXT("  능력치");
			const UEnum* StatEnum = StaticEnum<EPlayerStat>();
			for (int32 i = 0; i < static_cast<int32>(EPlayerStat::Max); ++i)
			{
				const FName StatName(*StatEnum->GetNameStringByValue(i));
				StatLine += FString::Printf(TEXT(" %s=%d"), *StatName.ToString(), Progress->GetStat(StatName));
			}
			Lines.Add(StatLine);
		}

		for (TActorIterator<AMainNpcSpawner> It(World); It; ++It)
		{
			const TArray<FString> Spawned = It->DescribeSpawnedNpcs();
			if (Spawned.Num() == 0)
			{
				Lines.Add(TEXT("  이 레벨에 세워진 메인 NPC 없음"));
			}
			for (const FString& Line : Spawned)
			{
				Lines.Add(TEXT("  ") + Line);
			}
		}

		// 출력 로그는 위에서부터 순서대로, 화면 메시지는 아래에서 위로 쌓이므로 거꾸로 넣는다.
		for (const FString& Line : Lines)
		{
			UE_LOG(LogMainNpcDebug, Display, TEXT("%s"), *Line);
		}
		if (GEngine)
		{
			for (int32 i = Lines.Num() - 1; i >= 0; --i)
			{
				GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Yellow, Lines[i]);
			}
		}
	}

	void NextDay(const TArray<FString>& Args, UWorld* World)
	{
		UTimeSubsystem* Time = GetGameInstanceSubsystem<UTimeSubsystem>(World);
		if (!Time)
		{
			Print(TEXT("시간 시스템이 없습니다."), FColor::Red);
			return;
		}

		const int32 Count = FMath::Max(1, ArgAsInt(Args, 0, 1));
		for (int32 i = 0; i < Count; ++i)
		{
			// 시간대를 끝까지 넘겨야 실제 게임처럼 "다음 날 아침"이 된다 (UTimeSubsystem::ResetSchedule과 같은 흐름).
			// ResetSchedule은 시간표가 비어 있으면 끝나지 않으므로, 횟수를 제한해서 직접 돌리고 안 되면 날만 넘긴다.
			const int32 Before = Time->GetAbsoluteDay();
			for (int32 Guard = 0; Guard < 64 && Time->GetAbsoluteDay() == Before; ++Guard)
			{
				Time->SpendSchedule();
			}
			if (Time->GetAbsoluteDay() == Before)
			{
				Time->SpendDay();
			}
		}

		RefreshSpawners(World);
		Print(FString::Printf(TEXT("%d일차 %s"), Time->GetAbsoluteDay(), *Time->GetNowScheduleTag().ToString()));
	}

	void NextSchedule(const TArray<FString>& Args, UWorld* World)
	{
		UTimeSubsystem* Time = GetGameInstanceSubsystem<UTimeSubsystem>(World);
		if (!Time)
		{
			Print(TEXT("시간 시스템이 없습니다."), FColor::Red);
			return;
		}

		const int32 Count = FMath::Max(1, ArgAsInt(Args, 0, 1));
		for (int32 i = 0; i < Count; ++i)
		{
			Time->SpendSchedule();
		}

		RefreshSpawners(World);
		Print(FString::Printf(TEXT("%d일차 %s"), Time->GetAbsoluteDay(), *Time->GetNowScheduleTag().ToString()));
	}

	void Travel(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			Print(TEXT("사용법: mainnpc.Travel <A|B|/Game/...레벨 경로>"), FColor::Red);
			return;
		}

		FString Target = Args[0];
		if (Target.Equals(TEXT("A"), ESearchCase::IgnoreCase))
		{
			Target = TEXT("/Game/Test/MainNpcTest/Lvl_MainNpcTest_A");
		}
		else if (Target.Equals(TEXT("B"), ESearchCase::IgnoreCase))
		{
			Target = TEXT("/Game/Test/MainNpcTest/Lvl_MainNpcTest_B");
		}

		Print(FString::Printf(TEXT("레벨 이동: %s"), *Target));
		UGameplayStatics::OpenLevel(World, FName(*Target));
	}

	void AddAffinity(const TArray<FString>& Args, UWorld* World)
	{
		UNpcAffinitySubsystem* Affinity = GetGameInstanceSubsystem<UNpcAffinitySubsystem>(World);
		if (!Affinity || Args.Num() < 2)
		{
			Print(TEXT("사용법: mainnpc.Affinity <NpcId> <증감>"), FColor::Red);
			return;
		}

		const FName NpcId(*Args[0]);
		const float NewValue = Affinity->AddAffinity(NpcId, FCString::Atof(*Args[1]));
		RefreshSpawners(World);
		Print(FString::Printf(TEXT("%s 호감도 %.0f (%s)"), *NpcId.ToString(), NewValue,
			*UEnum::GetDisplayValueAsText(Affinity->GetRelationshipLevel(NpcId)).ToString()));
	}

	void SetRelation(const TArray<FString>& Args, UWorld* World)
	{
		UNpcAffinitySubsystem* Affinity = GetGameInstanceSubsystem<UNpcAffinitySubsystem>(World);
		if (!Affinity || Args.Num() < 2)
		{
			Print(TEXT("사용법: mainnpc.Relation <NpcId> <Wary|Neutral|Favor|Trust|Intimate>"), FColor::Red);
			return;
		}

		// 단계 경계값(기본 -20/20/50/80) 안쪽 값으로 맞춘다.
		static const TMap<FString, float> ValueByLevel = {
			{TEXT("Wary"), -50.f}, {TEXT("Neutral"), 0.f}, {TEXT("Favor"), 30.f},
			{TEXT("Trust"), 60.f}, {TEXT("Intimate"), 90.f},
		};

		const float* Value = nullptr;
		for (const TPair<FString, float>& Pair : ValueByLevel)
		{
			if (Pair.Key.Equals(Args[1], ESearchCase::IgnoreCase))
			{
				Value = &Pair.Value;
			}
		}
		if (!Value)
		{
			Print(FString::Printf(TEXT("알 수 없는 단계: %s"), *Args[1]), FColor::Red);
			return;
		}

		const FName NpcId(*Args[0]);
		Affinity->SetAffinity(NpcId, *Value);
		RefreshSpawners(World);
		Print(FString::Printf(TEXT("%s 호감도 %.0f (%s)"), *NpcId.ToString(), *Value,
			*UEnum::GetDisplayValueAsText(Affinity->GetRelationshipLevel(NpcId)).ToString()));
	}

	void AddStat(const TArray<FString>& Args, UWorld* World)
	{
		UGameProgressSubsystem* Progress = GetGameInstanceSubsystem<UGameProgressSubsystem>(World);
		if (!Progress || Args.Num() < 2)
		{
			Print(TEXT("사용법: mainnpc.Stat <Health|Mental|Int|Luck|Courage|HandWork|Speech> <증감>"), FColor::Red);
			return;
		}

		const FName StatName(*Args[0]);
		if (StaticEnum<EPlayerStat>()->GetValueByName(StatName) == INDEX_NONE)
		{
			Print(FString::Printf(TEXT("알 수 없는 능력치: %s"), *Args[0]), FColor::Red);
			return;
		}

		const int32 NewValue = Progress->AddStat(StatName, FCString::Atoi(*Args[1]));
		RefreshSpawners(World);
		Print(FString::Printf(TEXT("%s = %d"), *StatName.ToString(), NewValue));
	}

	void SetTag(const TArray<FString>& Args, UWorld* World)
	{
		UGameProgressSubsystem* Progress = GetGameInstanceSubsystem<UGameProgressSubsystem>(World);
		if (!Progress || Args.Num() < 1)
		{
			Print(TEXT("사용법: mainnpc.Tag <태그> [remove]"), FColor::Red);
			return;
		}

		const FName Tag(*Args[0]);
		const bool bRemove = Args.IsValidIndex(1) && Args[1].Equals(TEXT("remove"), ESearchCase::IgnoreCase);
		if (bRemove)
		{
			Progress->RemoveTag(Tag);
		}
		else
		{
			Progress->GrantTag(Tag);
		}

		RefreshSpawners(World);
		Print(FString::Printf(TEXT("태그 %s %s"), *Tag.ToString(), bRemove ? TEXT("제거") : TEXT("부여")));
	}

	void CompleteEvent(const TArray<FString>& Args, UWorld* World)
	{
		UGameProgressSubsystem* Progress = GetGameInstanceSubsystem<UGameProgressSubsystem>(World);
		if (!Progress || Args.Num() < 1)
		{
			Print(TEXT("사용법: mainnpc.Event <EventId>"), FColor::Red);
			return;
		}

		Progress->MarkEventCompleted(FName(*Args[0]));
		RefreshSpawners(World);
		Print(FString::Printf(TEXT("이벤트 %s 완료 처리"), *Args[0]));
	}

	void SetAIState(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			Print(TEXT("사용법: mainnpc.AIState <NpcId> <Idle|Patrol|Guide|Chase> [RouteId]"), FColor::Red);
			return;
		}

		AMainNpcCharacter* Npc = FindNpc(World, FName(*Args[0]));
		AMainNpcAIController* NpcAIController = Npc ? Cast<AMainNpcAIController>(Npc->GetController()) : nullptr;
		if (!NpcAIController)
		{
			Print(FString::Printf(TEXT("이 레벨에 %s 가 없거나 AI 컨트롤러가 없습니다."), *Args[0]), FColor::Red);
			return;
		}

		const FString& State = Args[1];
		const FName RouteId = Args.IsValidIndex(2) ? FName(*Args[2]) : NAME_None;
		if (State.Equals(TEXT("Patrol"), ESearchCase::IgnoreCase) || State.Equals(TEXT("Guide"), ESearchCase::IgnoreCase))
		{
			ANpcRoute* Route = Npc->GetRoute(RouteId);
			if (!Route)
			{
				Print(FString::Printf(TEXT("경로 '%s'를 찾지 못했습니다."), *RouteId.ToString()), FColor::Red);
				return;
			}
			if (State.Equals(TEXT("Patrol"), ESearchCase::IgnoreCase))
			{
				NpcAIController->StartPatrol(Route);
			}
			else
			{
				NpcAIController->StartGuide(Route);
			}
		}
		else if (State.Equals(TEXT("Chase"), ESearchCase::IgnoreCase))
		{
			NpcAIController->StartChase(UGameplayStatics::GetPlayerPawn(World, 0));
		}
		else
		{
			NpcAIController->StopRouteMovement();
			NpcAIController->StopChase();
			NpcAIController->SetAIState(ENpcAIState::Idle);
		}

		Print(FString::Printf(TEXT("%s AIState = %s"), *Args[0],
			*UEnum::GetDisplayValueAsText(NpcAIController->GetAIState()).ToString()));
	}

	void Refresh(const TArray<FString>& Args, UWorld* World)
	{
		RefreshSpawners(World);
		Print(TEXT("메인 NPC 배치 갱신"));
	}

	// ---- 등록 ----

	static FAutoConsoleCommandWithWorldAndArgs CmdStatus(TEXT("mainnpc.Status"),
		TEXT("현재 레벨/일차/시간대, 호감도, 능력치, 세워진 메인 NPC를 보여준다"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
	static FAutoConsoleCommandWithWorldAndArgs CmdNextDay(TEXT("mainnpc.NextDay"),
		TEXT("mainnpc.NextDay [횟수] - 다음 날 아침으로"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NextDay));
	static FAutoConsoleCommandWithWorldAndArgs CmdNextSchedule(TEXT("mainnpc.NextSchedule"),
		TEXT("mainnpc.NextSchedule [횟수] - 다음 시간대로"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NextSchedule));
	static FAutoConsoleCommandWithWorldAndArgs CmdTravel(TEXT("mainnpc.Travel"),
		TEXT("mainnpc.Travel <A|B|레벨 경로> - 레벨 이동"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Travel));
	static FAutoConsoleCommandWithWorldAndArgs CmdAffinity(TEXT("mainnpc.Affinity"),
		TEXT("mainnpc.Affinity <NpcId> <증감> - 호감도 더하기"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AddAffinity));
	static FAutoConsoleCommandWithWorldAndArgs CmdRelation(TEXT("mainnpc.Relation"),
		TEXT("mainnpc.Relation <NpcId> <Wary|Neutral|Favor|Trust|Intimate> - 호감도를 단계로 맞추기"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetRelation));
	static FAutoConsoleCommandWithWorldAndArgs CmdStat(TEXT("mainnpc.Stat"),
		TEXT("mainnpc.Stat <능력치> <증감> - 능력치 더하기"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AddStat));
	static FAutoConsoleCommandWithWorldAndArgs CmdTag(TEXT("mainnpc.Tag"),
		TEXT("mainnpc.Tag <태그> [remove] - 진행 태그 부여/제거"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTag));
	static FAutoConsoleCommandWithWorldAndArgs CmdEvent(TEXT("mainnpc.Event"),
		TEXT("mainnpc.Event <EventId> - 이벤트 완료 처리"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CompleteEvent));
	static FAutoConsoleCommandWithWorldAndArgs CmdAIState(TEXT("mainnpc.AIState"),
		TEXT("mainnpc.AIState <NpcId> <Idle|Patrol|Guide|Chase> [RouteId] - Blackboard AIState 강제"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetAIState));
	static FAutoConsoleCommandWithWorldAndArgs CmdRefresh(TEXT("mainnpc.Refresh"),
		TEXT("메인 NPC 스포너가 지금 상태로 다시 배치"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Refresh));
}
