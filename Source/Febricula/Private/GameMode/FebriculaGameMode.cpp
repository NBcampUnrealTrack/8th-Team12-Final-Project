// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameMode/FebriculaGameMode.h"

#include "EngineUtils.h"
#include "Activity/ActivitySubsystem.h"
#include "GameMode/FebriculaPlayerStart.h"

AFebriculaGameMode::AFebriculaGameMode()
{
	// stub
}

AActor* AFebriculaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	FGameplayTag NowSchedule; 
	if (UActivitySubsystem* ActivitySubsystem = GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		NowSchedule = ActivitySubsystem->GetNowScheduleTag();
		
		
		ActivitySubsystem->FindRunnableActivities();
		//const FGameplayTag SpawnLocationTag = 
	} 
	
	AFebriculaPlayerStart* Matched = nullptr;
	AFebriculaPlayerStart* Fallback = nullptr;
	
	for (TActorIterator<AFebriculaPlayerStart> It(GetWorld()); It; ++It)
	{
		AFebriculaPlayerStart* Now = *It;
		if (!Now->SpawnPointTag.IsValid())
		{
			if (!Fallback)
			{
				Fallback = Now;
			}
			continue;
		}
		if (NowSchedule.IsValid() && NowSchedule.MatchesTag(Now->SpawnPointTag))
		{
			Matched = Now;
			break;
		}
	}
	if (Matched) return Matched;
	if (Fallback) return Fallback;
	
	return Super::ChoosePlayerStart_Implementation(Player);
}
