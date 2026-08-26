// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameMode/FebriculaGameMode.h"

#include "GameMode/ActivitySubsystem.h"

AFebriculaGameMode::AFebriculaGameMode()
{
	// stub
}

AActor* AFebriculaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (UActivitySubsystem* ActivitySubsystem = GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		ActivitySubsystem->FindRunnableActivities();
		//const FGameplayTag SpawnLocationTag = 
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}
