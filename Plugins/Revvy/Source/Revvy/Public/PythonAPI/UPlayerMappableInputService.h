// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPlayerMappableInputService.generated.h"

/**
 * Player Mappable Input service exposed directly to Python for runtime
 * key rebinding via the UE 5.5+ Enhanced Input User Settings API.
 *
 * Mirrors the ULightingService / UPerformanceService pattern: all
 * UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.PlayerMappableInputService.<snake_case_name>(...)`.
 *
 * Backing API (UE 5.8):
 *   UEnhancedInputLocalPlayerSubsystem::GetUserSettings()
 *     -> UEnhancedInputUserSettings
 *          -> GetActiveKeyProfile() -> UEnhancedPlayerMappableKeyProfile
 *               -> GetPlayerMappingRows() : TMap<FName, FKeyMappingRow>
 *          -> MapPlayerKey / UnMapPlayerKey / ResetKeyProfileIdToDefault
 *          -> SaveSettings()
 *
 * This is a runtime state service (per-LocalPlayer key mappings). No
 * FScopedTransaction is used because we mutate runtime user settings,
 * not editor assets.
 *
 * The active LocalPlayer is resolved from the current world's
 * UGameInstance::GetFirstGamePlayer(). The PIE world is preferred if
 * available, otherwise the editor world (which has no GameInstance and
 * will therefore return a clear error).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.PlayerMappableInputService
 *
 *   # Introspect currently bound mappings on the active profile
 *   rows = json.loads(svc.list_player_key_mappings())
 *   prof = json.loads(svc.get_active_key_profile())
 *
 *   # Rebind the "Jump" mapping's first slot to Space
 *   ok = svc.rebind_player_key("Jump", "SpaceBar", 0)
 *   assert ok
 *
 *   # Reset a single mapping (unmap the current player-bound key)
 *   svc.reset_player_key("Jump", 0)
 *
 *   # Reset every player-bound key on the active profile
 *   svc.reset_all_player_keys()
 *
 *   # Persist to config
 *   svc.save_player_key_mappings()
 *
 * @note 6 rebinding operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UPlayerMappableInputService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List every player key mapping row on the active key profile.
	 *
	 * Returned JSON is an array of objects with fields:
	 *   mapping_name  : FName of the mapping
	 *   action_path   : Soft object path of the associated UInputAction (may be empty)
	 *   key_name      : Current key (player-mapped if set, otherwise the IMC default)
	 *   slot          : EPlayerMappableKeySlot as int (0-6, 7=Unspecified)
	 *   current_key   : Same as key_name — canonical FKey name
	 *   default_key   : Default FKey name from the source IMC (may differ if customized)
	 *   is_customized : True if the player has overridden this mapping
	 *
	 * @return JSON array string. Empty string on early error (a warning is logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Query")
	static FString ListPlayerKeyMappings();

	/**
	 * Report the currently active key profile.
	 *
	 * @return JSON {profile_id, display_name, row_count}. Empty string on early error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Query")
	static FString GetActiveKeyProfile();

	// ═══════════════════════════════════════════════════════════════════
	// Rebind
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Rebind a single mapping row's slot to a new FKey.
	 *
	 * @param MappingName  Mapping row name (FName in the active profile).
	 * @param NewKeyName   FKey string name (e.g. "SpaceBar", "Gamepad_FaceButton_Bottom").
	 * @param Slot         EPlayerMappableKeySlot index (0=First .. 6=Seventh).
	 * @return True if the key was accepted and MapPlayerKey reported no failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Rebind")
	static bool RebindPlayerKey(const FString& MappingName, const FString& NewKeyName, int32 Slot);

	/**
	 * Unmap a single player-bound key. Any registered IMC default remains,
	 * but the player's override for this MappingName/Slot is removed.
	 *
	 * @param MappingName  Mapping row name (FName in the active profile).
	 * @param Slot         EPlayerMappableKeySlot index (0=First .. 6=Seventh).
	 * @return True if UnMapPlayerKey reported no failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Rebind")
	static bool ResetPlayerKey(const FString& MappingName, int32 Slot);

	/**
	 * Reset every player-bound key on the active profile to the IMC defaults.
	 *
	 * @return True if the active profile was reset without a reported failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Rebind")
	static bool ResetAllPlayerKeys();

	// ═══════════════════════════════════════════════════════════════════
	// Persist
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Synchronously write the current UEnhancedInputUserSettings to its
	 * hardcoded save-game slot. Wraps UEnhancedInputUserSettings::SaveSettings().
	 *
	 * @return True if the save call was dispatched (no null in the LocalPlayer
	 *         -> subsystem -> UserSettings chain).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerMappable|Persist")
	static bool SavePlayerKeyMappings();
};
