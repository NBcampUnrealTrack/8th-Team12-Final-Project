// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UMediaService.generated.h"

/**
 * MediaFramework video-playback service exposed directly to Python.
 *
 * Mirrors the UActorPoolService / UInputBufferService pattern: all UFUNCTIONs
 * are static and BlueprintCallable so they are reachable from Python as
 * `unreal.MediaService.<snake_case_name>(...)`.
 *
 * State model:
 *   A named-player registry (TMap<FString, TStrongObjectPtr<UMediaPlayer>>)
 *   is held in the .cpp anonymous namespace. TStrongObjectPtr keeps each
 *   UMediaPlayer alive against GC for as long as the registry retains it.
 *
 *   No FScopedTransaction is used — media-player state is runtime only.
 *
 * Threading:
 *   GameThread only. Registry access is not guarded.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.MediaService
 *
 *   svc.play_media_source("/Game/Movies/MyMovie.MyMovie", "Cutscene1")
 *   svc.pause_media_player("Cutscene1")
 *   svc.seek_media_player("Cutscene1", 3.5)
 *   print(svc.get_media_player_state("Cutscene1"))
 *   svc.stop_media_player("Cutscene1")
 */
UCLASS(BlueprintType)
class REVVY_API UMediaService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Playback
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Load a UMediaSource by asset path, register or reuse a UMediaPlayer under
	 * PlayerName in the registry, open the source on that player, and start
	 * playback.
	 *
	 * @param MediaSourcePath - Full asset path to a UMediaSource (or subclass
	 *                         such as UFileMediaSource).
	 * @param PlayerName      - Registry key. Empty → rejected. If the key
	 *                         already exists, the existing player is reused
	 *                         (its current source is closed first).
	 * @return PlayerName on success, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Media|Play")
	static FString PlayMediaSource(const FString& MediaSourcePath, const FString& PlayerName);

	// ═══════════════════════════════════════════════════════════════════
	// Control
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Pause the named media player. No-op if the player is already paused.
	 *
	 * @param PlayerName - Registry key.
	 * @return True on success, false if the player is not registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Media|Control")
	static bool PauseMediaPlayer(const FString& PlayerName);

	/**
	 * Close (stop) the named media player. Any open source is released. The
	 * player entry stays in the registry so it can be reused; call
	 * PlayMediaSource again to reopen a source.
	 *
	 * @param PlayerName - Registry key.
	 * @return True on success, false if the player is not registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Media|Control")
	static bool StopMediaPlayer(const FString& PlayerName);

	/**
	 * Seek the named media player to TimeSeconds from the start of the source.
	 *
	 * @param PlayerName  - Registry key.
	 * @param TimeSeconds - Target time in seconds (>= 0.0).
	 * @return True on success, false if the player is not registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Media|Control")
	static bool SeekMediaPlayer(const FString& PlayerName, float TimeSeconds);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a compact JSON snapshot of the player's state:
	 *   { is_playing, is_paused, current_time, duration, is_ready, is_looping }
	 *
	 * @param PlayerName - Registry key.
	 * @return JSON string, or empty string if the player is not registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Media|Query")
	static FString GetMediaPlayerState(const FString& PlayerName);
};
