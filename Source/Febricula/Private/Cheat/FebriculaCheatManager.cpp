// Fill out your copyright notice in the Description page of Project Settings.


#include "Cheat/FebriculaCheatManager.h"
#include "GameFramework/Character.h"
#include "Dialogue/DialogueSubsystem.h"
#include "GameMode/TimeSubsystem.h"
#include "Progress/GameProgressSubsystem.h"

void UFebriculaCheatManager::SkipDay(int32 Day)
{
	if (UTimeSubsystem* TimeSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		for (int i = 0; i < Day; i++)
		{
			TimeSubsystem->SpendDay();
		}
	}
}

void UFebriculaCheatManager::SkipSchedule(int32 Count)
{
	if (UTimeSubsystem* TimeSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		for (int i = 0; i < Count; i++)
		{
			TimeSubsystem->SpendSchedule();
		}
	}
}

void UFebriculaCheatManager::GetCachedCharacter()
{
	if (UDialogueSubsystem* DialogueSubsystem = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		UE_LOG(LogTemp, Warning, TEXT("GetCachedCharacter()"));
		for (const TPair<FName, TWeakObjectPtr<ACharacter>>& Pair : DialogueSubsystem->GetCachedCharacter())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Pair.Key.ToString());
			UE_LOG(LogTemp, Warning, TEXT("%s"), *GetNameSafe(Pair.Value.Get()));
			UE_LOG(LogTemp, Warning, TEXT("\n"));
		}
	}
}

void UFebriculaCheatManager::GetClearedEvents()
{
	UE_LOG(LogTemp, Warning, TEXT("GetClearedEvents()"));
	if (UGameProgressSubsystem* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		const TSet<FName> ClearedEvents = Progress->GetClearedEvents();
		for (FName Event : ClearedEvents)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Event.ToString());
		}
	}
}
