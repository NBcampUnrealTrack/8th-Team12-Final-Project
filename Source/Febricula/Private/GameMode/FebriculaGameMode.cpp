// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameMode/FebriculaGameMode.h"

#include "EngineUtils.h"
#include "FebriculaCharacter.h"
#include "Activity/ActivitySubsystem.h"
#include "Dialogue/DialogueSubsystem.h"
#include "GameMode/FebriculaPlayerStart.h"
#include "GameMode/LevelAutoEventSetting.h"
#include "Kismet/GameplayStatics.h"
#include "Progress/GameProgressSubsystem.h"

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
			if (UGameProgressSubsystem* ProgressSubsystem = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
			{
				if (ProgressSubsystem->EvaluateConditions(Now->Conditions))
				{
					Matched = Now;
					break;		
				}
			}
		}
	}
	if (Matched) return Matched;
	if (Fallback) return Fallback;
	
	return Super::ChoosePlayerStart_Implementation(Player);
}

void AFebriculaGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (UDialogueSubsystem* DialogueSubsystem = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		DialogueSubsystem->BlockPlayerInput(false);
	}

	//GetWorldTimerManager().SetTimer(AutoEventTimer, this, &AFebriculaGameMode::PlayEvent, 3.0f, false);
	PlayEvent();
}

void AFebriculaGameMode::PlayEvent()
{
	if (ALevelAutoEventSetting* AES = Cast<ALevelAutoEventSetting>(GetWorldSettings()))
	{
		AutoEventTable = AES->AutoEventRow;
		AutoDialogueTable = AES->AutoDialogueRow;
		if (AutoEventTable)
		{
			if(UDialogueSubsystem* DialogueSubsystem = GetWorld()->GetSubsystem<UDialogueSubsystem>())
			{
				UE_LOG(LogTemp, Warning, TEXT("WOW!!!!!"));
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				{
					if (PC->GetPawn())
					{
						DialogueSubsystem->SetDataTables(AutoEventTable, AutoDialogueTable);
						DialogueSubsystem->TryStartEventForNpc("", UGameplayStatics::GetPlayerController(this, 0)->GetPawn());
					}
				}
			}
		}
	}

}
