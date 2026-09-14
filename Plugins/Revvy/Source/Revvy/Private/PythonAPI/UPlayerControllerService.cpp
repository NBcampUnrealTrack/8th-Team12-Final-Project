// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPlayerControllerService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPlayerController, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

// PlayerController + Pawn
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

// Editor actor subsystem (label helpers)
#include "Subsystems/EditorActorSubsystem.h"

// Asset loading helpers (for actor path resolution)
#include "UObject/SoftObjectPath.h"
#include "EditorAssetLibrary.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Return the current editor world (PIE-preferred if running). */
	UWorld* PlayerControllerGetEditorWorldLocal()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		// Prefer PIE world when running so player controllers are alive.
		if (UWorld* PIEWorld = GEditor->PlayWorld)
		{
			return PIEWorld;
		}

		return GEditor->GetEditorWorldContext().World();
	}

	/**
	 * Actor lookup helper mirroring UCameraShakeService::FindActorByNameOrPathLocal.
	 * Handles label match, name match, soft object path resolve, package
	 * path load, and contains-fallback.
	 */
	AActor* FindActorByNameOrPathLocal(const FString& Query)
	{
		if (Query.IsEmpty())
		{
			return nullptr;
		}

		UWorld* World = PlayerControllerGetEditorWorldLocal();
		if (!World)
		{
			return nullptr;
		}

		const FString LowerSearch = Query.ToLower();

		// 1. Exact label / name match (case-insensitive)
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) { continue; }

			if (Actor->GetActorLabel().ToLower() == LowerSearch ||
				Actor->GetName().ToLower() == LowerSearch)
			{
				return Actor;
			}
		}

		// 2. FSoftObjectPath resolve
		{
			FSoftObjectPath SoftPath(Query);
			if (SoftPath.IsValid())
			{
				if (UObject* Resolved = SoftPath.ResolveObject())
				{
					if (AActor* AsActor = Cast<AActor>(Resolved))
					{
						return AsActor;
					}
				}
			}
		}

		// 3. Package path via UEditorAssetLibrary
		if (Query.StartsWith(TEXT("/")))
		{
			if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(Query))
			{
				if (AActor* AsActor = Cast<AActor>(Loaded))
				{
					return AsActor;
				}
			}
		}

		// 4. Contains match as last resort
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) { continue; }

			if (Actor->GetActorLabel().ToLower().Contains(LowerSearch) ||
				Actor->GetName().ToLower().Contains(LowerSearch))
			{
				return Actor;
			}
		}

		return nullptr;
	}

	/** Find a pawn by actor lookup (label / name / path). */
	APawn* FindPawnByNameOrPath(const FString& Query)
	{
		if (AActor* Found = FindActorByNameOrPathLocal(Query))
		{
			return Cast<APawn>(Found);
		}
		return nullptr;
	}
}

// =================================================================
// Possession
// =================================================================

bool UPlayerControllerService::PossessPawn(
	const FString& PlayerControllerActorNameOrPath,
	const FString& PawnActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyPlayerController, Warning,
			TEXT("PossessPawn: PlayerController '%s' not found (need active PIE?)"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	APawn* Pawn = FindPawnByNameOrPath(PawnActorNameOrPath);
	if (!Pawn)
	{
		UE_LOG(LogRevvyPlayerController, Warning,
			TEXT("PossessPawn: Pawn '%s' not found or not an APawn"),
			*PawnActorNameOrPath);
		return false;
	}

	PC->Possess(Pawn);
	return true;
}

bool UPlayerControllerService::UnpossessPawn(const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyPlayerController, Warning,
			TEXT("UnpossessPawn: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	PC->UnPossess();
	return true;
}

// =================================================================
// Query
// =================================================================

FString UPlayerControllerService::GetControlledPawn(const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyPlayerController, Warning,
			TEXT("GetControlledPawn: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return FString();
	}

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		return FString();
	}

	return Pawn->GetActorLabel();
}

// =================================================================
// Input
// =================================================================

bool UPlayerControllerService::EnablePlayerInput(
	const FString& PlayerControllerActorNameOrPath,
	bool bEnable)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyPlayerController, Warning,
			TEXT("EnablePlayerInput: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	if (bEnable)
	{
		PC->EnableInput(PC);
	}
	else
	{
		PC->DisableInput(PC);
	}
	return true;
}
