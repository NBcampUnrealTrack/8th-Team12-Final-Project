// Fill out your copyright notice in the Description page of Project Settings.


#include "Cheat/FebriculaCheatManager.h"
#include "GameFramework/Character.h"
#include "Dialogue/DialogueSubsystem.h"
#include "GameMode/TimeSubsystem.h"

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
