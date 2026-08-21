// Fill out your copyright notice in the Description page of Project Settings.


#include "Cheat/FebriculaCheatManager.h"

#include "GameMode/TimeSubsystem.h"

void UFebriculaCheatManager::SkipDay(int32 Day)
{
	if (UTimeSubsystem* TimeSubsystem = GetPlayerController()->GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		for (int i = 0; i < Day; i++)
		{
			TimeSubsystem->SpendDay();
		}
	}
}
