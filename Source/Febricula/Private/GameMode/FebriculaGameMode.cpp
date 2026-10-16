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
	AFebriculaPlayerStart* ConditionalMatched = nullptr;
	AFebriculaPlayerStart* Fallback = nullptr;
	if (UGameProgressSubsystem* ProgressSubsystem = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{

		for (TActorIterator<AFebriculaPlayerStart> It(GetWorld()); It; ++It)
		{
			AFebriculaPlayerStart* Now = *It;
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Now->SpawnPointTag.GetTagName().ToString());
			UE_LOG(LogTemp, Warning, TEXT("SpawnPoint Tag: %s"), *NowSchedule.ToString());
			UE_LOG(LogTemp, Warning, TEXT("%d"), NowSchedule.MatchesTag(Now->SpawnPointTag));
			UE_LOG(LogTemp, Warning, TEXT("Now Name: %s"), *Now->GetName());

			// [skydodo] 2026-10-13 버그 수정: 조건 검사가 if (!Fallback) 안에 있어 조건 없는 Start를 먼저 만나면 그 뒤의 조건부 Start는
			// 검사조차 안 됐다(배치 순서에 따라 결과가 바뀜). 우선순위: 시간대+조건 일치 > 태그 없는 조건부 일치 > 조건 없는 Start
			if (!Now->SpawnPointTag.IsValid())
			{
				if (Now->Conditions.IsEmpty())
				{
					if (!Fallback)
					{
						Fallback = Now;
					}
				}
				else if (!ConditionalMatched && ProgressSubsystem->EvaluateConditions(Now->Conditions))
				{
					ConditionalMatched = Now;
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
	if (ConditionalMatched) return ConditionalMatched;
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
	
	// if (UActivitySubsystem* ActivitySubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	// {
	// 	int32 NowMaxAp = ActivitySubsystem->GetMaxAP();
	// 	
	// 	if (NowMaxAp > 0 && ActivitySubsystem->GetCurrentAP() <= 0)
	// 	{
	// 		if (UTimeSubsystem* TimeSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	// 		{
	// 			TimeSubsystem->SpendSchedule();
	// 		}
	// 	}
	// }

	
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