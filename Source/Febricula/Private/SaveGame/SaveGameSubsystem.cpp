// Fill out your copyright notice in the Description page of Project Settings.

#include "SaveGame/SaveGameSubsystem.h"
#include "SaveGame/FebriculaSaveGame.h"

#include "Kismet/GameplayStatics.h"
#include "Progress/GameProgressSubsystem.h"
#include "NPC/MainNpc/NpcAffinitySubsystem.h"
#include "GameMode/TimeSubsystem.h"
#include "Activity/ActivitySubsystem.h"
#include "Dialogue/DialogueLogSubsystem.h"
#include "Dialogue/DialogueSubsystem.h"

// ---------------------------------------------------------------------------
// 내부 헬퍼
// ---------------------------------------------------------------------------

bool USaveGameSubsystem::IsValidSlot(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < MaxSlots;
}

FString USaveGameSubsystem::GetSlotName(int32 SlotIndex)
{
	return FString::Printf(TEXT("SaveSlot%d"), SlotIndex);
}

UFebriculaSaveGame* USaveGameSubsystem::LoadSlotData(int32 SlotIndex) const
{
	if (!IsValidSlot(SlotIndex))
	{
		return nullptr;
	}
	return Cast<UFebriculaSaveGame>(
		UGameplayStatics::LoadGameFromSlot(GetSlotName(SlotIndex), 0));
}

// ---------------------------------------------------------------------------
// 공개 API
// ---------------------------------------------------------------------------

bool USaveGameSubsystem::DoesSlotExist(int32 SlotIndex) const
{
	if (!IsValidSlot(SlotIndex))
	{
		return false;
	}
	return UGameplayStatics::DoesSaveGameExist(GetSlotName(SlotIndex), 0);
}

bool USaveGameSubsystem::SaveToSlot(int32 SlotIndex)
{
	if (!IsValidSlot(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("USaveGameSubsystem::SaveToSlot: 유효하지 않은 슬롯 인덱스 %d"), SlotIndex);
		return false;
	}

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
		UE_LOG(LogTemp, Warning, TEXT("USaveGameSubsystem::SaveToSlot: World가 없어 저장을 취소합니다."));
		return false;
	}

	// 대화 진행 중 저장을 막는다: 대화 자체(현재 노드, 선택지 대기 등)는 저장 대상이 아니라서,
	// 대화 도중 저장했다가 불러오면 "대사는 계속 진행되는데 백로그만 과거로 되돌아가는" 모순이 생긴다.
	if (const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>())
	{
		if (Dialogue->IsDialogueActive())
		{
			UE_LOG(LogTemp, Warning, TEXT("SaveGameSubsystem::SaveToSlot: 대화 중에는 저장할 수 없습니다."));
			return false;
		}
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
	}
	if (const UNpcAffinitySubsystem* Relationship = GetGameInstance()->GetSubsystem<UNpcAffinitySubsystem>())
	{
		Save->RelationshipData = Relationship->GetSaveData();
	}
	if (const UActivitySubsystem* Activity = GetGameInstance()->GetSubsystem<UActivitySubsystem>())
	{
		Save->CurrentAP = Activity->GetCurrentAP();
	}

	// 대화 백로그 (GameInstanceSubsystem이라 레벨과 무관하게 가져올 수 있다)
	if (const UDialogueLogSubsystem* Log = GetGameInstance()->GetSubsystem<UDialogueLogSubsystem>())
	{
		Save->DialogueLog = Log->GetDialogueLog();
	}

	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, GetSlotName(SlotIndex), 0);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("USaveGameSubsystem::SaveToSlot: 슬롯 %d 저장 실패"), SlotIndex);
	}
	return bSaved;
}

bool USaveGameSubsystem::LoadFromSlot(int32 SlotIndex)
{
	if (!DoesSlotExist(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("USaveGameSubsystem::LoadFromSlot: 슬롯 %d가 존재하지 않습니다."), SlotIndex);
		return false;
	}

	// 저장과 같은 이유로, 대화 중에는 불러오기도 막는다.
	if (const UWorld* CurrentWorld = GetWorld())
	{
		if (const UDialogueSubsystem* Dialogue = CurrentWorld->GetSubsystem<UDialogueSubsystem>())
		{
			if (Dialogue->IsDialogueActive())
			{
				UE_LOG(LogTemp, Warning, TEXT("SaveGameSubsystem::LoadFromSlot: 대화 중에는 불러올 수 없습니다."));
				return false;
			}
		}
	}

	UFebriculaSaveGame* Save = Cast<UFebriculaSaveGame>(
		UGameplayStatics::LoadGameFromSlot(GetSlotName(SlotIndex), 0));
	if (!Save)
	{
		UE_LOG(LogTemp, Error, TEXT("USaveGameSubsystem::LoadFromSlot: 슬롯 %d 로드 실패"), SlotIndex);
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

	if (UDialogueLogSubsystem* Log = GetGameInstance()->GetSubsystem<UDialogueLogSubsystem>())
	{
		Log->LoadDialogueLog(PendingDialogueLog);
	}

	bHasPendingLoad = false;
	PendingDialogueLog.Empty();
}

FDateTime USaveGameSubsystem::GetSlotSaveTime(int32 SlotIndex) const
{
	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	return SaveData ? SaveData->SavedAt : FDateTime();
}

bool USaveGameSubsystem::GetSlotGameDate(int32 SlotIndex, int32& OutMonth, int32& OutDay, int32& OutAbsoluteDay) const
{
	OutMonth = 0;
	OutDay = 0;
	OutAbsoluteDay = 0;

	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	if (!SaveData)
	{
		return false;
	}

	OutMonth = SaveData->TimeData.NowMonth;
	OutDay = SaveData->TimeData.NowDay;
	OutAbsoluteDay = SaveData->TimeData.AbsoluteDay;
	return true;
}

void USaveGameSubsystem::GetSlotDisplayInfo(int32 SlotIndex, bool& bExists, FText& SaveTimeText, FText& GameDateText) const
{
	const UFebriculaSaveGame* SaveData = LoadSlotData(SlotIndex);
	if (!SaveData)
	{
		bExists       = false;
		SaveTimeText  = NSLOCTEXT("SaveGame", "EmptySlot", "빈 슬롯");
		GameDateText  = FText::GetEmpty();
		return;
	}

	bExists = true;

	// "YYYY-MM-DD HH:MM"
	const FDateTime& T = SaveData->SavedAt;
	SaveTimeText = FText::FromString(
		FString::Printf(TEXT("%04d-%02d-%02d %02d:%02d"),
			T.GetYear(), T.GetMonth(), T.GetDay(),
			T.GetHour(), T.GetMinute()));

	// "N월 N일"
	GameDateText = FText::FromString(
		FString::Printf(TEXT("%d월 %d일"), SaveData->TimeData.NowMonth, SaveData->TimeData.NowDay));
}
