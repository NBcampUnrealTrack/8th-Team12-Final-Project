// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "USaveGameService.generated.h"

/**
 * SaveGame service exposed directly to Python for managing USaveGame
 * slot lifecycle: create-in-memory, persist-to-slot, load-from-slot,
 * async-persist, existence probe, delete, enumerate, and inspect.
 *
 * Mirrors the UPerformanceService / UPlayerMappableInputService pattern:
 * all UFUNCTIONs are static + BlueprintCallable so they are reachable
 * from Python as `unreal.SaveGameService.<snake_case_name>(...)`.
 *
 * State model
 * -----------
 * A USaveGame instance is a transient UObject that must survive across
 * multiple Python calls (create -> populate -> persist, or load -> read).
 * We keep those live instances alive via a static
 *   TMap<FString, TStrongObjectPtr<USaveGame>>
 * registry keyed by a caller-supplied Handle string. The strong pointer
 * blocks GC while the handle is registered. Handles are user-scoped
 * (Python is expected to generate/track them, e.g. UUIDs).
 *
 * Handle collision behaviour: if a Handle is registered again (either via
 * CreateSaveGameObject or LoadGameFromSlot), the existing entry is
 * overwritten and a Warning is logged. This is documented, not a bug —
 * it means "reuse this slot in the registry".
 *
 * This is a runtime state service — no FScopedTransaction is used
 * because USaveGame objects are transient, not editor assets.
 *
 * Backing API (UE 5.8):
 *   UGameplayStatics::CreateSaveGameObject(UClass*)         -> USaveGame*
 *   UGameplayStatics::SaveGameToSlot(USaveGame*, Slot, UI)  -> bool
 *   UGameplayStatics::AsyncSaveGameToSlot(...)              -> void (delegate)
 *   UGameplayStatics::LoadGameFromSlot(Slot, UI)            -> USaveGame*
 *   UGameplayStatics::DoesSaveGameExist(Slot, UI)           -> bool
 *   UGameplayStatics::DeleteGameInSlot(Slot, UI)            -> bool
 *   IPlatformFeaturesModule::Get().GetSaveGameSystem()      -> ISaveGameSystem
 *     -> GetSaveGameNames(TArray<FString>&, UI)             (5.4+, best-effort)
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.SaveGameService
 *
 *   # Create + persist
 *   h = svc.create_save_game_object("/Script/MyGame.MySaveGame", "player-1")
 *   assert h == "player-1"
 *   ok = svc.save_game_to_slot("player-1", "Slot0", 0)
 *
 *   # Load + inspect
 *   h2 = svc.load_game_from_slot("Slot0", 0, "player-1-loaded")
 *   props = json.loads(svc.get_save_game_properties_json("player-1-loaded"))
 *
 *   # Enumerate + query + delete
 *   slots  = json.loads(svc.list_save_slots(0))
 *   exists = svc.does_save_game_exist("Slot0", 0)
 *   svc.delete_save_slot("Slot0", 0)
 *
 * @note 8 SaveGame operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API USaveGameService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Instantiate a USaveGame-derived UObject in memory and register it
	 * under Handle. The instance is kept alive by a strong pointer inside
	 * the service's internal registry until it is overwritten or the
	 * process exits.
	 *
	 * @param SaveGameClassPath  Full class path, e.g.
	 *                           "/Script/MyGame.MySaveGame" or a Blueprint
	 *                           class path ending with "_C".
	 * @param Handle             Caller-owned key used by every subsequent
	 *                           UFUNCTION on this instance. If it collides
	 *                           with an existing handle the previous entry
	 *                           is overwritten and a Warning is logged.
	 * @return The Handle on success; empty string if the class could not
	 *         be resolved, is not a USaveGame subclass, or instantiation
	 *         failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Create")
	static FString CreateSaveGameObject(const FString& SaveGameClassPath, const FString& Handle);

	// ═══════════════════════════════════════════════════════════════════
	// Persist
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Synchronously serialize the registered USaveGame at Handle to the
	 * given save slot. Wraps UGameplayStatics::SaveGameToSlot.
	 *
	 * @param Handle     Registry key returned by CreateSaveGameObject or
	 *                   LoadGameFromSlot.
	 * @param SlotName   Platform save slot name (no extension).
	 * @param UserIndex  Local user index (0 for single-player).
	 * @return True if the underlying save call reported success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Persist")
	static bool SaveGameToSlot(const FString& Handle, const FString& SlotName, int32 UserIndex);

	/**
	 * Load the USaveGame stored at SlotName / UserIndex and register the
	 * resulting instance under Handle.
	 *
	 * @param SlotName   Platform save slot name.
	 * @param UserIndex  Local user index.
	 * @param Handle     Registry key to bind the loaded instance to. If
	 *                   the handle already exists it is overwritten and a
	 *                   Warning is logged.
	 * @return Handle on success; empty string if the slot does not exist
	 *         or the load call returned nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Persist")
	static FString LoadGameFromSlot(const FString& SlotName, int32 UserIndex, const FString& Handle);

	/**
	 * Request an asynchronous save of the registered USaveGame at Handle
	 * to SlotName / UserIndex. Completion is currently logged only —
	 * Python callers who need the completion result should poll with
	 * DoesSaveGameExist or re-load the slot.
	 *
	 * @param Handle     Registry key.
	 * @param SlotName   Platform save slot name.
	 * @param UserIndex  Local user index.
	 * @return True if the request was accepted (i.e. Handle resolved and
	 *         the async subsystem enqueued it).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Persist")
	static bool AsyncSaveGameToSlot(const FString& Handle, const FString& SlotName, int32 UserIndex);

	/**
	 * Delete the save slot at (SlotName, UserIndex). Does not touch the
	 * in-memory registry — Python is responsible for discarding stale
	 * handles.
	 *
	 * @return True on delete success (or platform-defined "considered
	 *         deleted" result).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Persist")
	static bool DeleteSaveSlot(const FString& SlotName, int32 UserIndex);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Test whether a save slot exists for the given user.
	 * Direct wrapper over UGameplayStatics::DoesSaveGameExist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Query")
	static bool DoesSaveGameExist(const FString& SlotName, int32 UserIndex);

	/**
	 * Enumerate every save slot known to the platform's ISaveGameSystem
	 * for the given user index.
	 *
	 * Uses IPlatformFeaturesModule::GetSaveGameSystem()->GetSaveGameNames
	 * (UE 5.4+). On platforms whose ISaveGameSystem cannot enumerate the
	 * call logs a Warning and returns an empty JSON array with a note
	 * field explaining why.
	 *
	 * @return JSON. Success shape: [ "SlotA", "SlotB", ... ].
	 *         Unsupported shape:   { "supported": false, "note": "..." }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Query")
	static FString ListSaveSlots(int32 UserIndex);

	/**
	 * Serialize every BlueprintReadWrite / BlueprintReadOnly UPROPERTY on
	 * the registered USaveGame at Handle to a JSON object. Useful for
	 * inspecting save state without a full editor UI.
	 *
	 * Implementation uses FJsonObjectConverter::UStructToJsonObject over
	 * the SaveGame's UClass, which produces a canonical UPROPERTY-shaped
	 * JSON payload.
	 *
	 * @return JSON object string. Empty string if Handle is not registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|SaveGame|Query")
	static FString GetSaveGamePropertiesJson(const FString& Handle);
};
