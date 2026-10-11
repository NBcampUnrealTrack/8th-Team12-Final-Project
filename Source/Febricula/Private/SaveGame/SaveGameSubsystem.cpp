// Fill out your copyright notice in the Description page of Project Settings.

#include "SaveGame/SaveGameSubsystem.h"
#include "SaveGame/FebriculaSaveGame.h"

#include "Kismet/GameplayStatics.h"
#include "Progress/GameProgressSubsystem.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"
#include "GameMode/TimeSubsystem.h"
#include "Activity/ActivitySubsystem.h"
#include "Dialogue/DialogueSubsystem.h"
#include "NPC/MainNpc/MainNpcSpawner.h"

FString USaveGameSubsystem::GetSlotName(int32 SlotIndex)
{
	return FString::Printf(TEXT("SaveSlot%d"), SlotIndex);
}

bool USaveGameSubsystem::DoesSlotExist(int32 SlotIndex) const
{
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(SlotIndex), 0);
}

bool USaveGameSubsystem::SaveToSlot(int32 SlotIndex)
{
	UFebriculaSaveGame* Save = Cast<UFebriculaSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UFebriculaSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}

	Save->SavedAt = FDateTime::Now();

	const UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveGameSubsystem::SaveToSlot: World가 없어 저장을 취소합니다."));
		return false;
	}

	// 레벨 종속 값: 현재 레벨 + 플레이어 트랜스폼
	Save->LevelName = FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));

	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			Save->PlayerLocation = Pawn->GetActorLocation();
			Save->PlayerRotation = Pawn->GetActorRotation();
		}
	}

	// GameInstance급 값
	if (const UTimeSubsystem* Time = GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		Save->TimeData = Time->GetSaveData();
	}
	if (const UGameProgressSubsystem* Progress = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		Save->ProgressData = Progress->GetSaveData();
		Save->MoveHistory = Progress->GetMoveHistory();
		
	}
	if (const UNpcAffinitySubsystem* Relationship = GetGameInstance()->GetSubsystem<UNpcAffinitySubsystem>())
	{
		Save->RelationshipData = Relationship->GetSaveData();
	}
	if (const UActivitySubsystem* Activity = GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		Save->CurrentAP = Activity->GetCurrentAP();
	}
	

	// 대화 백로그 (월드 서브시스템이라 지금 레벨에 있을 때만 가져올 수 있다)
	if (const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
	{
		Save->DialogueLog = Dialogue->GetDialogueLog();
	}
	
	if (UGameProgressSubsystem* GameProgressSubsystem = World->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		Save->LevelSeed =  GameProgressSubsystem->GetSpawnerSeed();
	}

	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, GetSlotName(SlotIndex), 0);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveGameSubsystem::SaveToSlot: 슬롯 %d 저장 실패"), SlotIndex);
	}
	return bSaved;
}

bool USaveGameSubsystem::LoadFromSlot(int32 SlotIndex)
{
	if (!DoesSlotExist(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveGameSubsystem::LoadFromSlot: 슬롯 %d가 존재하지 않습니다."), SlotIndex);
		return false;
	}

	UFebriculaSaveGame* Save = Cast<UFebriculaSaveGame>(
		UGameplayStatics::LoadGameFromSlot(GetSlotName(SlotIndex), 0));
	if (!Save)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveGameSubsystem::LoadFromSlot: 슬롯 %d 로드 실패"), SlotIndex);
		return false;
	}

	// 1) 레벨과 무관한 값은 지금 바로 반영한다.
	ApplyGameInstanceData(Save);

	// 2) 레벨이 있어야 적용 가능한 값은 대기시킨다.
	PendingPlayerLocation = Save->PlayerLocation;
	PendingPlayerRotation = Save->PlayerRotation;
	PendingDialogueLog = Save->DialogueLog;
	bHasPendingLoad = true;

	const UWorld* World = GetWorld();
	const FName CurrentLevel = World
		? FName(*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()))
		: NAME_None;

	if (!Save->LevelName.IsNone() && Save->LevelName != CurrentLevel)
	{
		// 레벨 이동이 필요하다. 새 레벨의 AFebriculaGameMode::BeginPlay에서
		// ApplyPendingLoadIfAny()를 호출해줘야 2)가 실제로 반영된다.
		UGameplayStatics::OpenLevel(this, Save->LevelName);
	}
	else
	{
		// 이미 같은 레벨이면 레벨 전환이 없으니 여기서 바로 마저 적용한다.
		ApplyPendingLoadIfAny();
	}
	
	if (UGameProgressSubsystem* GameProgress = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		GameProgress->SetSpawnerSeed(Save->LevelSeed);
		for (FName History : Save->MoveHistory)
		{
			GameProgress->AddMoveHistory(History);
		}
	}

	TArray<AActor*> Spawners;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMainNpcSpawner::StaticClass(), Spawners);
	if (Spawners.Num() > 0)
	{
		if (AMainNpcSpawner* Spawner = Cast<AMainNpcSpawner>(Spawners[0]))
		{
				
			Spawner->RequestRefresh();
		}
	}

	return true;
}

void USaveGameSubsystem::ApplyGameInstanceData(const UFebriculaSaveGame* Save)
{
	if (!Save || !GetGameInstance())
	{
		return;
	}

	if (UTimeSubsystem* Time = GetGameInstance()->GetSubsystem<UTimeSubsystem>())
	{
		Time->LoadSaveData(Save->TimeData);
	}
	if (UGameProgressSubsystem* Progress = GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		Progress->LoadSaveData(Save->ProgressData);
	}
	if (UNpcAffinitySubsystem* Relationship = GetGameInstance()->GetSubsystem<UNpcAffinitySubsystem>())
	{
		Relationship->LoadSaveData(Save->RelationshipData);
	}
	if (UActivitySubsystem* Activity = GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		Activity->SetCurrentAP(Save->CurrentAP);
	}
}

void USaveGameSubsystem::ApplyPendingLoadIfAny()
{
	if (!bHasPendingLoad)
	{
		return;
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocationAndRotation(
				PendingPlayerLocation,
				PendingPlayerRotation,
				/*bSweep=*/false,
				/*OutSweepHitResult=*/nullptr,
				ETeleportType::TeleportPhysics);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
		{
			Dialogue->SetDialogueLog(PendingDialogueLog);
		}
	}

	bHasPendingLoad = false;
	PendingDialogueLog.Empty();
}
