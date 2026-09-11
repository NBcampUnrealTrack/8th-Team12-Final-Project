// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UGameFeatureService.generated.h"

/**
 * Game Feature service exposed directly to Python for Lyra-style GameFeature
 * plugin control (list plugins, inspect their state / actions, and drive the
 * runtime state machine transitions between Installed / Registered / Loaded /
 * Active / Unloaded).
 *
 * Mirrors the ULightingService / UNavigationService pattern: all UFUNCTIONs
 * are static and BlueprintCallable so they are reachable from Python as
 * `unreal.GameFeatureService.<snake_case_name>(...)`.
 *
 * State machine reminder (see GameFeatureTypes.h GAME_FEATURE_PLUGIN_STATE_LIST):
 *   Uninitialized -> UnknownStatus -> StatusKnown -> Installed -> Registered
 *   -> Loaded -> Active
 * Deactivation reverses Active -> Loaded, Unload reverses Loaded -> Registered
 * (or Installed when bKeepRegistered = false).
 *
 * IMPORTANT: All state transition functions return `true` when the *request*
 * was accepted by UGameFeaturesSubsystem (URL resolved, subsystem valid, and
 * the async operation was kicked off). They do NOT block until the target
 * state is reached. Callers should poll GetGameFeatureState() if they need to
 * observe completion.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.GameFeatureService
 *
 *   # Query
 *   plugins = json.loads(svc.list_game_feature_plugins())
 *   info    = json.loads(svc.get_game_feature_state("MyFeature"))
 *   actions = json.loads(svc.list_game_feature_actions("MyFeature"))
 *
 *   # Transitions (async request kickoff - poll get_game_feature_state to confirm)
 *   svc.load_game_feature_to_registered("MyFeature")
 *   svc.load_game_feature_to_loaded("MyFeature")
 *   svc.load_game_feature_to_active("MyFeature")
 *   svc.deactivate_game_feature("MyFeature")
 *   svc.unload_game_feature("MyFeature")
 */
UCLASS(BlueprintType)
class REVVY_API UGameFeatureService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enumerate every game feature plugin currently known to the
	 * UGameFeaturesSubsystem (each plugin is visited via ForEachGameFeature).
	 *
	 * @return JSON array: [{"name": string, "url": string, "state": string}, ...]
	 *         "state" uses UE::GameFeatures::ToString(EGameFeaturePluginState).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Query")
	static FString ListGameFeaturePlugins();

	/**
	 * Get the current state of a single game feature plugin, resolving the
	 * plugin URL from its plugin name (uplugin filename without extension).
	 *
	 * URL resolution tries GetPluginURLByName first, then falls back to
	 * GetPluginURL_FileProtocol via IPluginManager for locally installed
	 * plugins that have not yet been registered as GFPs.
	 *
	 * @param PluginName - Bare plugin name (e.g. "ShooterCore"), not a URL.
	 * @return JSON object:
	 *   { "success": bool,
	 *     "name": string,
	 *     "url": string,
	 *     "state": string,      // EGameFeaturePluginState via ToString
	 *     "progress": float }   // 0..1 install progress if available
	 *   On failure returns { "success": false, "error": string }.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Query")
	static FString GetGameFeatureState(const FString& PluginName);

	/**
	 * List the UGameFeatureAction entries authored on the plugin's
	 * UGameFeatureData asset. Requires the plugin to be at least Registered
	 * (otherwise the GameFeatureData is not yet resolved and an empty array
	 * plus a warning is returned).
	 *
	 * @param PluginName - Bare plugin name (e.g. "ShooterCore"), not a URL.
	 * @return JSON array: [{"action_class": string, "class_path": string}, ...]
	 *         On failure returns { "success": false, "error": string }.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Query")
	static FString ListGameFeatureActions(const FString& PluginName);

	// ═══════════════════════════════════════════════════════════════════
	// State Transitions
	//
	// NOTE: These wrap UGameFeaturesSubsystem::ChangeGameFeatureTargetState
	// (async). Return value is whether the request was accepted, not whether
	// the plugin has reached the target state.
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Drive the plugin toward EGameFeatureTargetState::Registered.
	 * Async: request-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Transition")
	static bool LoadGameFeatureToRegistered(const FString& PluginName);

	/**
	 * Drive the plugin toward EGameFeatureTargetState::Loaded.
	 * Async: request-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Transition")
	static bool LoadGameFeatureToLoaded(const FString& PluginName);

	/**
	 * Drive the plugin toward EGameFeatureTargetState::Active
	 * (mounts, registers, loads and activates as needed).
	 * Async: request-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Transition")
	static bool LoadGameFeatureToActive(const FString& PluginName);

	/**
	 * Deactivate an active plugin (Active -> Loaded). Wraps
	 * UGameFeaturesSubsystem::DeactivateGameFeaturePlugin.
	 * Async: request-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Transition")
	static bool DeactivateGameFeature(const FString& PluginName);

	/**
	 * Unload a plugin (Loaded -> Registered/Installed). Wraps
	 * UGameFeaturesSubsystem::UnloadGameFeaturePlugin with bKeepRegistered = false.
	 * Async: request-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameFeature|Transition")
	static bool UnloadGameFeature(const FString& PluginName);
};
