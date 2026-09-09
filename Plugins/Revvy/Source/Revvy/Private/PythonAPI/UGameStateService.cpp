// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGameStateService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGameState, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"

// Game framework
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/Pawn.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Shared Revvy helpers (world access)
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString GameStateJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}

	/** Serialize a JSON array to a compact FString. */
	FString GameStateJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Convert an FString error message to a {"error":"..."} JSON payload. */
	FString GameStateMakeErrorJson(const FString& Msg)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("error"), Msg);
		return GameStateJsonObjectToString(Obj);
	}

	/** Pull the authoritative AGameStateBase for the resolved world. */
	AGameStateBase* ResolveGameState()
	{
		UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
		if (!World)
		{
			return nullptr;
		}
		return World->GetGameState();
	}

	/** Serialize a single APlayerState into a JSON object. */
	TSharedPtr<FJsonObject> PlayerStateToJson(const APlayerState* PS)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!PS)
		{
			return Obj;
		}
		Obj->SetStringField(TEXT("player_name"), PS->GetPlayerName());
		Obj->SetNumberField(TEXT("player_id"), PS->GetPlayerId());
		Obj->SetNumberField(TEXT("score"), PS->GetScore());
		Obj->SetNumberField(TEXT("ping"), static_cast<double>(PS->GetPingInMilliseconds()));
		Obj->SetBoolField(TEXT("is_bot"), PS->IsABot());
		return Obj;
	}
}

// =================================================================
// UGameStateService
// =================================================================

FString UGameStateService::GetGameStateInfo()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("GetGameStateInfo: no resolvable world"));
		return GameStateMakeErrorJson(TEXT("no world"));
	}

	AGameStateBase* GS = World->GetGameState();
	if (!GS)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("GetGameStateInfo: world has no game state"));
		return GameStateMakeErrorJson(TEXT("no game state"));
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("class"), GS->GetClass() ? GS->GetClass()->GetName() : FString());

	// AGameState-derived tracks explicit MatchState; base AGameStateBase does not.
	bool bMatchInProgress = false;
	int32 ElapsedTime = 0;
	if (const AGameState* FullGS = Cast<const AGameState>(GS))
	{
		bMatchInProgress = FullGS->HasMatchStarted() && !FullGS->HasMatchEnded();
		ElapsedTime = FullGS->ElapsedTime;
	}
	Obj->SetBoolField(TEXT("is_match_in_progress"), bMatchInProgress);
	Obj->SetNumberField(TEXT("elapsed_time"), ElapsedTime);
	Obj->SetNumberField(TEXT("replicated_world_time_seconds"), GS->GetServerWorldTimeSeconds());
	Obj->SetNumberField(TEXT("player_state_count"), GS->PlayerArray.Num());

	return GameStateJsonObjectToString(Obj);
}

FString UGameStateService::ListPlayerStates()
{
	TArray<TSharedPtr<FJsonValue>> Arr;

	AGameStateBase* GS = ResolveGameState();
	if (!GS)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("ListPlayerStates: no game state"));
		return GameStateJsonArrayToString(Arr);
	}

	Arr.Reserve(GS->PlayerArray.Num());
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (!PS)
		{
			continue;
		}
		Arr.Add(MakeShared<FJsonValueObject>(PlayerStateToJson(PS)));
	}
	return GameStateJsonArrayToString(Arr);
}

FString UGameStateService::GetPlayerStateInfo(const FString& PlayerActorNameOrPathOrPlayerName)
{
	AGameStateBase* GS = ResolveGameState();
	if (!GS)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("GetPlayerStateInfo: no game state"));
		return GameStateMakeErrorJson(TEXT("no game state"));
	}

	const FString& Query = PlayerActorNameOrPathOrPlayerName;
	const bool bQueryIsInt = !Query.IsEmpty() && Query.IsNumeric();
	const int32 QueryInt = bQueryIsInt ? FCString::Atoi(*Query) : INDEX_NONE;

	const APlayerState* Found = nullptr;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (!PS)
		{
			continue;
		}
		if (PS->GetPlayerName().Equals(Query, ESearchCase::IgnoreCase))
		{
			Found = PS;
			break;
		}
		if (bQueryIsInt && PS->GetPlayerId() == QueryInt)
		{
			Found = PS;
			break;
		}
		if (PS->GetPathName().Equals(Query, ESearchCase::IgnoreCase) ||
			PS->GetName().Equals(Query, ESearchCase::IgnoreCase) ||
			PS->GetActorLabel().Equals(Query, ESearchCase::IgnoreCase))
		{
			Found = PS;
			break;
		}
	}

	if (!Found)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("GetPlayerStateInfo: no match for '%s'"), *Query);
		return GameStateMakeErrorJson(TEXT("player state not found"));
	}

	TSharedPtr<FJsonObject> Obj = PlayerStateToJson(Found);
	// Rename player_id -> id per the schema on this call
	Obj->RemoveField(TEXT("player_id"));
	Obj->SetNumberField(TEXT("id"), Found->GetPlayerId());

	// Owning pawn info
	const APawn* Pawn = Found->GetPawn();
	Obj->SetStringField(TEXT("pawn_class"), Pawn && Pawn->GetClass() ? Pawn->GetClass()->GetName() : FString());
	Obj->SetStringField(TEXT("pawn_label"), Pawn ? Pawn->GetActorLabel() : FString());

	return GameStateJsonObjectToString(Obj);
}

bool UGameStateService::IsMatchInProgress()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyGameState, Warning, TEXT("IsMatchInProgress: no world"));
		return false;
	}

	AGameModeBase* GMBase = World->GetAuthGameMode();
	if (!GMBase)
	{
		UE_LOG(LogRevvyGameState, Verbose, TEXT("IsMatchInProgress: no auth game mode"));
		return false;
	}

	if (AGameMode* GM = Cast<AGameMode>(GMBase))
	{
		return GM->IsMatchInProgress();
	}

	UE_LOG(LogRevvyGameState, Verbose,
		TEXT("IsMatchInProgress: auth game mode '%s' is not AGameMode-derived — returning false"),
		*GMBase->GetClass()->GetName());
	return false;
}
