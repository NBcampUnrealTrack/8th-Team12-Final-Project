// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Dialogue/DialogueLogSubsystem.h"
#include "FebriculaSaveGame.generated.h"

/**
 * 슬롯 하나에 기록되는 세이브 데이터.
 *
 * 값을 직접 채우지 말고 각 서브시스템의 PackIntoSave / RestoreFromSave를 통해
 * 읽고 쓴다. USaveGameSubsystem이 이 흐름을 조율한다.
 */
UCLASS(BlueprintType)
class FEBRICULA_API UFebriculaSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 저장한 현실 시각 */
	UPROPERTY(BlueprintReadWrite, Category = "SaveGame")
	FDateTime SavedAt;

	// ---- TimeSubsystem ----

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Time")
	int32 AbsoluteDay = 1;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Time")
	int32 NowDay = 1;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Time")
	int32 NowMonth = 1;

	// ---- GameProgressSubsystem ----

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Progress")
	TSet<FName> Tags;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Progress")
	TSet<FName> Topics;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Progress")
	TSet<FName> CompletedEvents;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Progress")
	TMap<FName, int32> Counters;

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Progress")
	TMap<FName, int32> Stats;

	// ---- NpcRelationshipSubsystem ----

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|Relationship")
	TMap<FName, float> AffinityByNpcId;

	// ---- DialogueLogSubsystem ----

	UPROPERTY(BlueprintReadWrite, Category = "SaveGame|DialogueLog")
	TArray<FDialogueLogEntry> DialogueLog;
};
