// Fill out your copyright notice in the Description page of Project Settings.

#include "SaveGame/SaveGameSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "SaveGame/FebriculaSaveGame.h"
#include "GameMode/TimeSubsystem.h"
#include "Progress/GameProgressSubsystem.h"
#include "NPC/MainNpc/NpcRelationshipSubsystem.h"
#include "Engine/GameInstance.h"

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

bool USaveGameSubsystem::IsValidSlot(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < MaxSlots;
}

FString USaveGameSubsystem::GetSlotName(int32 SlotIndex) const
{
	return FString::Printf(TEXT("SaveSlot%d"), SlotIndex);
}

UFebriculaSaveGame* USaveGameSubsystem::LoadSlotData(int32 SlotIndex) const
{
	if (!IsValidSlot(SlotIndex))
	{
		return nullptr;
	}
	return Cast<UFebriculaSaveGame>(
		UGameplayStatics::LoadGameFromSlot(GetSlotName(SlotIndex), 0));
}

// ---------------------------------------------------------------------------
// 공개 API
// ---------------------------------------------------------------------------

bool USaveGameSubsystem::SaveToSlot(int32 SlotIndex)
{
	if (!IsValidSlot(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("USaveGameSubsystem::SaveToSlot: 유효하지 않은 슬롯 인덱스 %d"), SlotIndex);
		return false;
	}

	UFebriculaSaveGame* SaveData = Cast<UFebriculaSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UFebriculaSaveGame::StaticClass()));
	if (!SaveData)
	{
		return false;
	}

	SaveData->SavedAt = FDateTime::Now();

	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}

	if (UTimeSubsystem* Time = GI->GetSubsystem<UTimeSubsystem>())
	{
		Time->PackIntoSave(SaveData);
	}
	if (UGameProgressSubsystem* Progress = GI->GetSubsystem<UGameProgressSubsystem>())
	{
		Progress->PackIntoSave(SaveData);
	}
	if (UNpcRelationshipSubsystem* Relationship = GI->GetSubsystem<UNpcRelationshipSubsystem>())
	{
		Relationship->PackIntoSave(SaveData);
	}

	return UGameplayStatics::SaveGameToSlot(SaveData, GetSlotName(SlotIndex), 0);
}

bool USaveGameSubsystem::LoadFromSlot(int32 SlotIndex)
{
	if (!IsValidSlot(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("USaveGameSubsystem::LoadFromSlot: 유효하지 않은 슬롯 인덱스 %d"), SlotIndex);
		return false;
	}

	UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	if (!SaveData)
	{
		return false;
	}

	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}

	if (UTimeSubsystem* Time = GI->GetSubsystem<UTimeSubsystem>())
	{
		Time->RestoreFromSave(SaveData);
	}
	if (UGameProgressSubsystem* Progress = GI->GetSubsystem<UGameProgressSubsystem>())
	{
		Progress->RestoreFromSave(SaveData);
	}
	if (UNpcRelationshipSubsystem* Relationship = GI->GetSubsystem<UNpcRelationshipSubsystem>())
	{
		Relationship->RestoreFromSave(SaveData);
	}

	return true;
}

bool USaveGameSubsystem::DoesSlotExist(int32 SlotIndex) const
{
	if (!IsValidSlot(SlotIndex))
	{
		return false;
	}
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(SlotIndex), 0);
}

FDateTime USaveGameSubsystem::GetSlotSaveTime(int32 SlotIndex) const
{
	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	return SaveData ? SaveData->SavedAt : FDateTime();
}

bool USaveGameSubsystem::GetSlotGameDate(int32 SlotIndex, int32& OutMonth, int32& OutDay, int32& OutAbsoluteDay) const
{
	OutMonth = 0;
	OutDay = 0;
	OutAbsoluteDay = 0;

	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	if (!SaveData)
	{
		return false;
	}

	OutMonth = SaveData->NowMonth;
	OutDay = SaveData->NowDay;
	OutAbsoluteDay = SaveData->AbsoluteDay;
	return true;
}

void USaveGameSubsystem::GetSlotDisplayInfo(int32 SlotIndex, bool& bExists, FText& SaveTimeText, FText& GameDateText) const
{
	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	if (!SaveData)
	{
		bExists       = false;
		SaveTimeText  = NSLOCTEXT("SaveGame", "EmptySlot", "빈 슬롯");
		GameDateText  = FText::GetEmpty();
		return;
	}

	bExists = true;

	// "YYYY-MM-DD HH:MM"
	const FDateTime& T = SaveData->SavedAt;
	SaveTimeText = FText::FromString(
		FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d"),
			T.GetYear(), T.GetMonth(), T.GetDay(),
			T.GetHour(), T.GetMinute()));

	// "N월 N일"
	GameDateText = FText::FromString(
		FString::Printf(TEXT("%d월 %d일"), SaveData->NowMonth, SaveData->NowDay));
}
