// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameMode/FebriculaGameMode.h"

#include "EngineUtils.h"
#include "FebriculaCharacter.h"
#include "FebriculaPlayerController.h"
#include "Activity/ActivitySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Dialogue/DialogueSubsystem.h"
#include "GameMode/FebriculaPlayerStart.h"
#include "GameMode/LevelSetting.h"
#include "GameMode/NPCSetting.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/BaseNpcCharacter.h"
#include "Progress/GameProgressSubsystem.h"
#include "SaveGame/SaveGameSubsystem.h"

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
	if (UGameProgressSubsystem* ProgressSubsystem = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{

		for (TActorIterator<AFebriculaPlayerStart> It(GetWorld()); It; ++It)
		{
			AFebriculaPlayerStart* Now = *It;
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Now->SpawnPointTag.GetTagName().ToString());
			UE_LOG(LogTemp, Warning, TEXT("SpawnPoint Tag: %s"), *NowSchedule.ToString());
			UE_LOG(LogTemp, Warning, TEXT("%d"), NowSchedule.MatchesTag(Now->SpawnPointTag));
			if (!Now->SpawnPointTag.IsValid())
			{
				if (!Fallback)
				{
					if (Now->Conditions.IsEmpty())
					{
						Fallback = Now;
					}
					else
					{
						if (ProgressSubsystem->EvaluateConditions(Now->Conditions))
						{
							Matched = Now;
						}
					}
				}
				continue;
			}
			if (NowSchedule.IsValid() && NowSchedule.MatchesTag(Now->SpawnPointTag))
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

	
	// ���̺� �ε�� ������ �̵��� �� ���, ��� ���̴� �÷��̾� ��ġ/��ȭ ��α׸� ���⼭ ���� �����Ѵ�.
	// ��� ���� �ε尡 ������ �ƹ� �ϵ� ���� �����Ƿ� �׻� ȣ���ص� �����ϴ�.
	if (USaveGameSubsystem* SaveGameSubsystem = GetGameInstance()->GetSubsystem<USaveGameSubsystem>())
	{
		SaveGameSubsystem->ApplyPendingLoadIfAny();
	}

	NPCCharacterMap = GetDefault<UNPCSetting>()->NPCCharacterMap;

	//GetWorldTimerManager().SetTimer(AutoEventTimer, this, &AFebriculaGameMode::PlayEvent, 3.0f, false);
	const bool bSpecialEvent = PlayEvent();

	if (!bSpecialEvent)
	{
		if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
		{
			TArray<FGameplayTag> RunnableActivityTags = ActivitySubsystem->GetRunnableActivityTag();
			if (RunnableActivityTags.Num() > 0)
			{
				ActivitySubsystem->PrintFixedActivities();
			}
		}
	}
}

TSoftClassPtr<ABaseNpcCharacter> AFebriculaGameMode::GetNpcViaName(FName NowName)
{
	if (TSoftClassPtr<ABaseNpcCharacter>* NPC = NPCCharacterMap.Find(NowName))
	{
		return NPC->LoadSynchronous();
	}
	return nullptr;
}

bool AFebriculaGameMode::PlayEvent()
{
	if (ALevelSetting* AES = Cast<ALevelSetting>(GetWorldSettings()))
	{
		AutoEventTable = AES->AutoEventRow;
		AutoDialogueTable = AES->AutoDialogueRow;
		if (AutoEventTable)
		{
			if (UDialogueSubsystem* DialogueSubsystem = GetWorld()->GetSubsystem<UDialogueSubsystem>())
			{
				UE_LOG(LogTemp, Warning, TEXT("WOW!!!!!"));
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				{
					if (PC->GetPawn())
					{
						DialogueSubsystem->SetDataTables(AutoEventTable, AutoDialogueTable);
						const bool bEventExist = DialogueSubsystem->TryStartEventForNpc("", UGameplayStatics::GetPlayerController(this, 0)->GetPawn());
						return bEventExist;
					}
				}
			}
		}
	}
	return false;
}