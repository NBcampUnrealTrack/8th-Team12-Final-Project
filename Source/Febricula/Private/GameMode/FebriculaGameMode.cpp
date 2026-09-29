// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameMode/FebriculaGameMode.h"

#include "EngineUtils.h"
#include "FebriculaCharacter.h"
#include "Activity/ActivitySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Dialogue/DialogueSubsystem.h"
#include "GameMode/FebriculaPlayerStart.h"
#include "GameMode/LevelAutoEventSetting.h"
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

	for (TActorIterator<AFebriculaPlayerStart> It(GetWorld()); It; ++It)
	{
		AFebriculaPlayerStart* Now = *It;
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Now->SpawnPointTag.GetTagName().ToString());
		UE_LOG(LogTemp, Warning, TEXT("SpawnPoint Tag: %d"), Now->SpawnPointTag.IsValid());
		UE_LOG(LogTemp, Warning, TEXT("%d"), NowSchedule.MatchesTag(Now->SpawnPointTag));
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

	// 세이브 로드로 레벨이 이동해 온 경우, 대기 중이던 플레이어 위치/대화 백로그를 여기서 마저 적용한다.
	// 대기 중인 로드가 없으면 아무 일도 하지 않으므로 항상 호출해도 안전하다.
	if (USaveGameSubsystem* SaveGameSubsystem = GetGameInstance()->GetSubsystem<USaveGameSubsystem>())
	{
		SaveGameSubsystem->ApplyPendingLoadIfAny();
	}

	NPCCharacterMap = GetDefault<UNPCSetting>()->NPCCharacterMap;

	//GetWorldTimerManager().SetTimer(AutoEventTimer, this, &AFebriculaGameMode::PlayEvent, 3.0f, false);
	PlayEvent();

	if (ALevelAutoEventSetting* AES = Cast<ALevelAutoEventSetting>(GetWorldSettings()))
	{
		ActivityWidget = AES->ActivityWidget;
	}
	if (ActivityWidget.IsNull()) return;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		ActivityWidgetObject = CreateWidget<UUserWidget>(PC, ActivityWidget.LoadSynchronous());
		ActivityWidgetObject->AddToViewport();


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

void AFebriculaGameMode::PlayEvent()
{
	if (ALevelAutoEventSetting* AES = Cast<ALevelAutoEventSetting>(GetWorldSettings()))
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
						DialogueSubsystem->TryStartEventForNpc("", UGameplayStatics::GetPlayerController(this, 0)->GetPawn());
					}
				}
			}
		}
	}

}