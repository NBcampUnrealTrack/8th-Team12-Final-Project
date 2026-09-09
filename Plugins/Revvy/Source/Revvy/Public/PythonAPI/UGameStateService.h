// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UGameStateService.generated.h"

/**
 * Game state service exposed directly to Python for runtime queries against
 * the current AGameStateBase / APlayerState / AGameMode instances hosted by
 * the active editor world (typically the PIE world when a session is live).
 *
 * Mirrors the UPerformanceService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.GameStateService.<snake_case_name>(...)`.
 *
 * This is a read-only introspection service — no FScopedTransaction is
 * required because no assets, actors, or replicated state is mutated.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.GameStateService
 *
 *   # Overall game state snapshot
 *   info = json.loads(svc.get_game_state_info())
 *
 *   # Player state listing / lookup
 *   players = json.loads(svc.list_player_states())
 *   ps      = json.loads(svc.get_player_state_info("Player0"))
 *
 *   # Match flow query (requires AGameMode-derived mode)
 *   in_progress = svc.is_match_in_progress()
 */
UCLASS(BlueprintType)
class REVVY_API UGameStateService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Game State Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Snapshot the currently active AGameStateBase.
	 *
	 * Returned JSON fields:
	 *   class, is_match_in_progress, elapsed_time,
	 *   replicated_world_time_seconds, player_state_count.
	 *
	 * @return JSON string. {"error":"..."} when no world or no game state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameState|Query")
	static FString GetGameStateInfo();

	/**
	 * List every APlayerState currently registered with the game state.
	 *
	 * @return JSON array of {player_name, player_id, score, ping, is_bot}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameState|Query")
	static FString ListPlayerStates();

	/**
	 * Look up a single APlayerState by player-visible name, numeric player id,
	 * or the owning actor's path / label.
	 *
	 * @param PlayerActorNameOrPathOrPlayerName  Name, id (as string), or path.
	 * @return JSON {player_name, id, score, ping, is_bot, pawn_class, pawn_label}
	 *         or {"error":"..."} when no match is found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameState|Query")
	static FString GetPlayerStateInfo(const FString& PlayerActorNameOrPathOrPlayerName);

	/**
	 * Query whether the authoritative game mode reports a match in progress.
	 *
	 * Only AGameMode subclasses expose IsMatchInProgress(); the base
	 * AGameModeBase always returns false from this helper.
	 *
	 * @return True when the auth AGameMode reports the match is running.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameState|Query")
	static bool IsMatchInProgress();
};
