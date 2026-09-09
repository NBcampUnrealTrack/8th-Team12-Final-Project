// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UWorldTravelService.generated.h"

/**
 * Level transition service exposed directly to Python for driving the
 * traditional (non-World-Partition) travel / seamless-travel flow from the
 * editor.
 *
 * Mirrors the UGameStateService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.WorldTravelService.<snake_case_name>(...)`.
 *
 * This is a runtime dispatch service — it triggers async travel requests
 * and reads level identity, so no FScopedTransaction is required.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.WorldTravelService
 *
 *   # Fire an async OpenLevel from the editor (PIE)
 *   svc.open_level("/Game/Maps/L_MainMenu", False, "")
 *
 *   # Query the active level
 *   name = svc.get_current_level_name(True)
 *
 *   # Server-side seamless travel (auth server only)
 *   svc.server_travel("/Game/Maps/L_Arena")
 *
 *   # Push a specific player controller through client travel
 *   svc.client_travel("PlayerController_0", "127.0.0.1", "Absolute")
 */
UCLASS(BlueprintType)
class REVVY_API UWorldTravelService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// OpenLevel
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Fire UGameplayStatics::OpenLevel against the resolved world.
	 *
	 * OpenLevel is asynchronous — this call only records that the request
	 * was accepted by the engine, not that the transition has completed.
	 *
	 * @param MapAssetPath  Package / short name of the target map.
	 * @param bAbsolute     True = full URL replace, false = keep host URL.
	 * @param Options       Extra URL options string (may be empty).
	 * @return True when the request was dispatched, false when the world
	 *         could not be resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTravel|OpenLevel")
	static bool OpenLevel(const FString& MapAssetPath, bool bAbsolute, const FString& Options);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Wrap UGameplayStatics::GetCurrentLevelName for the resolved world.
	 *
	 * @param bRemovePrefix True to strip the `UEDPIE_*` PIE prefix.
	 * @return Level name (empty when the world cannot be resolved).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTravel|Query")
	static FString GetCurrentLevelName(bool bRemovePrefix);

	// ═══════════════════════════════════════════════════════════════════
	// Server travel
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Trigger UWorld::ServerTravel for the resolved world. Only meaningful
	 * on the authoritative server.
	 *
	 * @param MapAssetPath Map URL to travel to.
	 * @return True when the engine accepted the request.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTravel|Server")
	static bool ServerTravel(const FString& MapAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Client travel
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Push a specific APlayerController through APlayerController::ClientTravel.
	 *
	 * @param PlayerControllerActorNameOrPath  Label, name, or path used to
	 *        locate the target player controller in the resolved world.
	 * @param URL          Destination URL passed to the controller.
	 * @param TravelTypeStr One of "Absolute", "Partial", or "Relative"
	 *        (case-insensitive). Maps to ETravelType.
	 * @return True when the controller was found and the call was issued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTravel|Client")
	static bool ClientTravel(const FString& PlayerControllerActorNameOrPath, const FString& URL, const FString& TravelTypeStr);
};
