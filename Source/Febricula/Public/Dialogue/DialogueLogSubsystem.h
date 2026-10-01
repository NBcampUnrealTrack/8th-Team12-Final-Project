// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DialogueLogSubsystem.generated.h"

class UFebriculaSaveGame;

USTRUCT(BlueprintType)
struct FDialogueLogEntry
{
	GENERATED_BODY()

	/** 화자 이름. 나레이션이면 비어 있음 */
	UPROPERTY(BlueprintReadOnly, Category = "DialogueLog")
	FText Speaker;

	/** 대사 텍스트 */
	UPROPERTY(BlueprintReadOnly, Category = "DialogueLog")
	FText Line;

	/** 이 대사 뒤에 나온 선택지 전부. 없으면 빈 배열 */
	UPROPERTY(BlueprintReadOnly, Category = "DialogueLog")
	TArray<FText> Choices;

	/** 플레이어가 고른 선택지 칸이면 true */
	UPROPERTY(BlueprintReadOnly, Category = "DialogueLog")
	bool bIsPlayerChoice = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDialogueLogUpdated);

/**
 * 대사 로그(백로그)를 누적 보관하는 GameInstanceSubsystem.
 * DialogueSubsystem(WorldSubsystem)이 직접 보관할 수 없으므로 별도로 분리.
 */
UCLASS(BlueprintType)
class FEBRICULA_API UDialogueLogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 대사 한 줄 추가 (OnDialogueLine.Broadcast 직전에 호출) */
	void AddLine(const FText& Speaker, const FText& Line);

	/** 마지막 로그 칸에 선택지 텍스트를 붙인다 (OnDialogueChoices.Broadcast 직전에 호출) */
	void AttachChoices(const TArray<FText>& Choices);

	/** 플레이어가 고른 선택지를 새 칸으로 추가 (SelectChoice에서 호출) */
	void AddPlayerChoice(const FText& ChoiceText);

	/** 로그가 바뀔 때마다 발생. BP에서 구독해 백로그 UI를 갱신한다 */
	UPROPERTY(BlueprintAssignable, Category = "DialogueLog")
	FOnDialogueLogUpdated OnLogUpdated;

	/** 현재까지 쌓인 로그 배열을 반환한다 */
	UFUNCTION(BlueprintCallable, Category = "DialogueLog")
	TArray<FDialogueLogEntry> GetDialogueLog() const { return DialogueLog; }

	/** 로그를 전부 지운다 */
	UFUNCTION(BlueprintCallable, Category = "DialogueLog")
	void ClearDialogueLog();

	/** SaveGame 객체에 로그 데이터를 담는다 */
	void PackIntoSave(UFebriculaSaveGame* SaveData) const;

	/** SaveGame 객체에서 로그 데이터를 복원하고 OnLogUpdated를 브로드캐스트한다 */
	void RestoreFromSave(const UFebriculaSaveGame* SaveData);

private:
	/** 배열 끝에 추가하고 300개 초과분을 앞에서 삭제한 뒤 OnLogUpdated를 브로드캐스트 */
	void AddLogEntry(const FDialogueLogEntry& Entry);

	static constexpr int32 MaxLogEntries = 300;

	UPROPERTY()
	TArray<FDialogueLogEntry> DialogueLog;
};
