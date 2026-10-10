// Fill out your copyright notice in the Description page of Project Settings.
//
// 세이브 슬롯 하나에 실제로 디스크에 저장되는 데이터 그릇.
// 각 Subsystem은 자기 상태를 스냅샷 구조체(FGameProgressSaveData 등)로 내보내고,
// 이 클래스는 그것들을 한데 모아 파일 하나로 직렬화되게만 한다.
//
// 레벨 종속 값(플레이어 위치, 대화 백로그)과 GameInstance급 값(시간/진행도/호감도/행동력)을
// 한 파일에 같이 담되, 적용 시점은 USaveGameSubsystem이 나눠서 처리한다:
//   - GameInstance급 값은 로드 즉시 반영 가능
//   - 레벨 종속 값은 저장된 레벨을 먼저 연 뒤에만 반영 가능

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"
#include "GameMode/TimeSubsystem.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Progress/GameProgressSaveData.h"
#include "FebriculaSaveGame.generated.h"

UCLASS()
class FEBRICULA_API UFebriculaSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 저장된 시각. 슬롯 목록 UI에서 "마지막 저장: ..."을 보여줄 때 쓴다 */
	UPROPERTY(VisibleAnywhere, Category = "Save")
	FDateTime SavedAt;

	// ---- 플레이어 (레벨 종속) ----

	UPROPERTY(VisibleAnywhere, Category = "Save | Player")
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Save | Player")
	FRotator PlayerRotation = FRotator::ZeroRotator;

	/** 저장 당시 플레이어가 있던 레벨. 로드 시 이 레벨을 먼저 열어야 한다 */
	UPROPERTY(VisibleAnywhere, Category = "Save | Player")
	FName LevelName;

	// ---- GameInstance급 진행 상태 ----

	UPROPERTY(VisibleAnywhere, Category = "Save | Time")
	FTimeSaveData TimeData;

	UPROPERTY(VisibleAnywhere, Category = "Save | Progress")
	FGameProgressSaveData ProgressData;

	UPROPERTY(VisibleAnywhere, Category = "Save | Relationship")
	FNpcRelationshipSaveData RelationshipData;

	UPROPERTY(VisibleAnywhere, Category = "Save | Activity")
	int32 CurrentAP = 0;

	// ---- 레벨 종속: 대화 백로그 ----

	UPROPERTY(VisibleAnywhere, Category = "Save | Dialogue")
	TArray<FDialogueLogEntry> DialogueLog;
	
	UPROPERTY(VisibleAnywhere, Category = "Save | MainCharacter Spawner")
	int32 LevelSeed = 0;
};

