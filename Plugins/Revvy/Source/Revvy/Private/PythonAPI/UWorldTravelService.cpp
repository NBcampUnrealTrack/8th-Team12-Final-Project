// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UWorldTravelService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyWorldTravel, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// Gameplay / travel
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/EngineBaseTypes.h"

// Editor actor helpers
#include "Subsystems/EditorActorSubsystem.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/**
	 * Parse a case-insensitive travel type token. Returns true on success.
	 */
	bool ParseTravelType(const FString& InStr, ETravelType& OutType)
	{
		const FString Lower = InStr.ToLower();
		if (Lower == TEXT("absolute"))
		{
			OutType = TRAVEL_Absolute;
			return true;
		}
		if (Lower == TEXT("partial"))
		{
			OutType = TRAVEL_Partial;
			return true;
		}
		if (Lower == TEXT("relative"))
		{
			OutType = TRAVEL_Relative;
			return true;
		}
		return false;
	}
}

// =================================================================
// UWorldTravelService
// =================================================================

bool UWorldTravelService::OpenLevel(const FString& MapAssetPath, bool bAbsolute, const FString& Options)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("OpenLevel: no resolvable world"));
		return false;
	}

	if (MapAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("OpenLevel: empty MapAssetPath"));
		return false;
	}

	UE_LOG(LogRevvyWorldTravel, Log,
		TEXT("OpenLevel: dispatching to '%s' (absolute=%s, options='%s')"),
		*MapAssetPath, bAbsolute ? TEXT("true") : TEXT("false"), *Options);

	// OpenLevel is async — a true return here means the request was accepted,
	// not that the transition has completed.
	UGameplayStatics::OpenLevel(World, FName(*MapAssetPath), bAbsolute, Options);
	return true;
}

FString UWorldTravelService::GetCurrentLevelName(bool bRemovePrefix)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("GetCurrentLevelName: no resolvable world"));
		return FString();
	}

	return UGameplayStatics::GetCurrentLevelName(World, bRemovePrefix);
}

bool UWorldTravelService::ServerTravel(const FString& MapAssetPath)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("ServerTravel: no resolvable world"));
		return false;
	}

	if (MapAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("ServerTravel: empty MapAssetPath"));
		return false;
	}

	UE_LOG(LogRevvyWorldTravel, Log, TEXT("ServerTravel: '%s'"), *MapAssetPath);
	return World->ServerTravel(MapAssetPath);
}

bool UWorldTravelService::ClientTravel(const FString& PlayerControllerActorNameOrPath, const FString& URL, const FString& TravelTypeStr)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyWorldTravel, Warning,
			TEXT("ClientTravel: no player controller matches '%s'"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	ETravelType TravelType = TRAVEL_Absolute;
	if (!ParseTravelType(TravelTypeStr, TravelType))
	{
		UE_LOG(LogRevvyWorldTravel, Warning,
			TEXT("ClientTravel: unknown TravelTypeStr '%s' (expected Absolute|Partial|Relative)"),
			*TravelTypeStr);
		return false;
	}

	if (URL.IsEmpty())
	{
		UE_LOG(LogRevvyWorldTravel, Warning, TEXT("ClientTravel: empty URL"));
		return false;
	}

	UE_LOG(LogRevvyWorldTravel, Log,
		TEXT("ClientTravel: PC='%s' URL='%s' Type='%s'"),
		*PC->GetName(), *URL, *TravelTypeStr);

	PC->ClientTravel(URL, TravelType);
	return true;
}
