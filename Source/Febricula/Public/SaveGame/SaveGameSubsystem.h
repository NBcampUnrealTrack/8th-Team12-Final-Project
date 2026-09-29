// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SaveGameSubsystem.generated.h"

/**
 * 세이브/로드의 단일 진입점.
 *
 * 슬롯은 SaveSlot0 ~ SaveSlot4 다섯 개.
 * 각 서브시스템(TimeSubsystem, GameProgressSubsystem, NpcRelationshipSubsystem)의
 * PackIntoSave / RestoreFromSave를 호출해 데이터를 모으고 흩뿌린다.
 */
UCLASS(BlueprintType)
class FEBRICULA_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxSlots = 5;

	/** 현재 게임 상태를 SlotIndex 슬롯에 저장한다. 성공하면 true */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool SaveToSlot(int32 SlotIndex);

	/** SlotIndex 슬롯에서 데이터를 불러와 각 서브시스템에 복원한다. 성공하면 true */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool LoadFromSlot(int32 SlotIndex);

	/** SlotIndex 슬롯에 저장된 데이터가 있는지 확인 */
	UFUNCTION(BlueprintPure, Category = "SaveGame")
	bool DoesSlotExist(int32 SlotIndex) const;

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

private:
	bool IsValidSlot(int32 SlotIndex) const;
	FString GetSlotName(int32 SlotIndex) const;

	/** 슬롯 데이터를 디스크에서 읽어 반환한다. 없거나 실패하면 nullptr */
	class UFebriculaSaveGame* LoadSlotData(int32 SlotIndex) const;
};
