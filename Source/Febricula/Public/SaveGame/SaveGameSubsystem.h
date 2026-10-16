// Fill out your copyright notice in the Description page of Project Settings.
//
// 저장/불러오기의 단일 진입점.
//
// [로드 흐름 - 왜 두 단계로 나뉘는가]
//   저장된 레벨이 지금 레벨과 다르면, LoadFromSlot()이 OpenLevel을 호출한 시점에는
//   새 레벨이 아직 존재하지 않는다. 그래서:
//     1) GameInstance급 값(시간/진행도/호감도/행동력)은 LoadFromSlot()에서 즉시 반영
//     2) 레벨 종속 값(플레이어 위치, 대화 백로그)은 PendingXxx에 담아두고 대기
//     3) 새 레벨이 완전히 뜬 뒤(AFebriculaGameMode::BeginPlay) ApplyPendingLoadIfAny()를
//        호출해서 마저 반영한다.
//   저장된 레벨과 지금 레벨이 같으면 레벨 이동이 없으므로 그 자리에서 바로 2)까지 적용한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dialogue/DialogueLogSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class UFebriculaSaveGame;

UCLASS(BlueprintType)
class FEBRICULA_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxSlots = 5;

	/** 현재 게임 상태를 SlotIndex 슬롯에 저장한다. 성공하면 true */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool SaveToSlot(int32 SlotIndex);

	/**
	 * 슬롯을 불러온다.
	 * 저장된 레벨이 지금과 다르면 레벨 이동을 트리거하고, 레벨이 뜬 뒤 ApplyPendingLoadIfAny()가
	 * 마저 처리해야 완전히 끝난다. 같은 레벨이면 이 호출 안에서 전부 끝난다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool LoadFromSlot(int32 SlotIndex);

	/** SlotIndex 슬롯에 저장된 데이터가 있는지 확인 */
	UFUNCTION(BlueprintPure, Category = "SaveGame")
	bool DoesSlotExist(int32 SlotIndex) const;

	/**
	 * 레벨 로드가 끝난 뒤(AFebriculaGameMode::BeginPlay 등) 호출해서, 월드가 있어야만
	 * 적용 가능한 값(플레이어 위치, 대화 백로그)을 마저 반영한다.
	 * 대기 중인 로드가 없으면 아무 일도 하지 않으니 매 BeginPlay마다 불러도 안전하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	void ApplyPendingLoadIfAny();

	UFUNCTION(BlueprintPure, Category = "SaveGame")
	bool HasPendingLoad() const { return bHasPendingLoad; }

	/**
	 * SlotIndex 슬롯의 저장 시각(현실 시간)을 반환한다.
	 * 슬롯이 비어 있으면 FDateTime() (기본값) 을 반환한다.
	 */
	UFUNCTION(BlueprintPure, Category = "SaveGame")
	FDateTime GetSlotSaveTime(int32 SlotIndex) const;

	/**
	 * SlotIndex 슬롯의 게임 내 날짜를 반환한다.
	 * 슬롯이 비어 있으면 false를 반환하고 Out 값은 0으로 채운다.
	 */
	UFUNCTION(BlueprintPure, Category = "SaveGame")
	bool GetSlotGameDate(int32 SlotIndex, int32& OutMonth, int32& OutDay, int32& OutAbsoluteDay) const;

	/**
	 * 세이브 슬롯 UI 표시용 요약 정보를 한 번에 반환한다.
	 * - 슬롯 존재 시: bExists=true, SaveTimeText="YYYY-MM-DD HH:MM", GameDateText="N월 N일"
	 * - 슬롯 없을 시: bExists=false, SaveTimeText="빈 슬롯", GameDateText=""
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	void GetSlotDisplayInfo(int32 SlotIndex, bool& bExists, FText& SaveTimeText, FText& GameDateText) const;

protected:
	static FString GetSlotName(int32 SlotIndex);

	/** GameInstance급(레벨 무관) 서브시스템 값을 즉시 반영한다 */
	void ApplyGameInstanceData(const UFebriculaSaveGame* Save);

	// ---- 레벨이 뜬 뒤에야 적용 가능한, 대기 중인 로드 데이터 ----
	bool bHasPendingLoad = false;
	FVector PendingPlayerLocation = FVector::ZeroVector;
	FRotator PendingPlayerRotation = FRotator::ZeroRotator;
	TArray<FDialogueLogEntry> PendingDialogueLog;

private:
	bool IsValidSlot(int32 SlotIndex) const;

	/** 슬롯 데이터를 디스크에서 읽어 반환한다. 없거나 실패하면 nullptr */
	UFebriculaSaveGame* LoadSlotData(int32 SlotIndex) const;
};
