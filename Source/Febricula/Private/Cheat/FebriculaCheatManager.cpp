// Fill out your copyright notice in the Description page of Project Settings.


#include "Cheat/FebriculaCheatManager.h"

#include "SNegativeActionButton.h"
#include "SWarningOrErrorBox.h"
#include "Activity/ActivitySubsystem.h"
#include "Cache/ActorCacheSubsystem.h"
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
		for (const TPair<FName, FCharacterCache>& Pair : DialogueSubsystem->GetCachedCharacter())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Pair.Key.ToString());
			UE_LOG(LogTemp, Warning, TEXT("%s"), *GetNameSafe(Pair.Value.Character.Get()));
			UE_LOG(LogTemp, Warning, TEXT("\n"));
		}
	}
}

void UFebriculaCheatManager::GetCurrentAP()
{
	if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		UE_LOG(LogTemp, Warning, TEXT("잔여 AP: %d"), ActivitySubsystem->GetCurrentAP());
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

void UFebriculaCheatManager::AddClearedEvent(FName EventId)
{
	if (EventId.IsNone()) return;
	if (UGameProgressSubsystem* Progress = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		Progress->MarkEventCompleted(EventId);
	}
}

void UFebriculaCheatManager::GetRunnableActivities()
{
	if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		const TMap<FGameplayTag, FActivityDefinition> RunnableActivity = ActivitySubsystem->GetRunnableActivities();
		for (const TPair<FGameplayTag, FActivityDefinition>& Pair : RunnableActivity)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Pair.Key.ToString());
			for (FActivityLocation Location : Pair.Value.ActivityLocations)
			{
				UE_LOG(LogTemp, Warning, TEXT("%s"), *Location.ConnectedScheduleTag.ToString());
				for (TSoftObjectPtr<UWorld> World : Location.ConnectedLevel)
				{
					UE_LOG(LogTemp, Warning, TEXT("%s"), *World.ToSoftObjectPath().ToString());
				}
				UE_LOG(LogTemp, Warning, TEXT("\n"));
			}
			UE_LOG(LogTemp, Warning, TEXT("\n"));
		}
	}
}

void UFebriculaCheatManager::GetAllActivities()
{
	if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		const TMap<FGameplayTag, FActivityDefinition> AllActivities = ActivitySubsystem->GetAllActivities();
		for (const TPair<FGameplayTag, FActivityDefinition>& Pair : AllActivities)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Pair.Key.ToString());
			for (FActivityLocation Location : Pair.Value.ActivityLocations)
			{
				UE_LOG(LogTemp, Warning, TEXT("%s"), *Location.ConnectedScheduleTag.ToString());
				for (TSoftObjectPtr<UWorld> World : Location.ConnectedLevel)
				{
					UE_LOG(LogTemp, Warning, TEXT("%s"), *World.ToSoftObjectPath().ToString());
				}
				UE_LOG(LogTemp, Warning, TEXT("\n"));
			}
			UE_LOG(LogTemp, Warning, TEXT("\n"));
		}
	}
}

void UFebriculaCheatManager::GetAllCachedTargetPoints()
{
	if (UActorCacheSubsystem* ActorCacheSubsystem = GetWorld()->GetSubsystem<UActorCacheSubsystem>())
	{
		ActorCacheSubsystem->PrintAllCachedTarget();
	}
}

void UFebriculaCheatManager::GetNowStat(FName StatName)
{
	if (UGameProgressSubsystem* ProgressSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s 현재 값: %d"), *StatName.ToString(), ProgressSubsystem->GetStat(StatName));
	}
}

