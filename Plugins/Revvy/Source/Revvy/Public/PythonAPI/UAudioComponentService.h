// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UAudioComponentService.generated.h"

/**
 * Runtime audio component service exposed directly to Python for spawning
 * one-shot / attached audio into the current editor world and driving the
 * live UAudioComponent's spatial parameters at runtime.
 *
 * Distinct from:
 *   - UMetaSoundService  (asset-authoring: MetaSound graph construction on
 *                         the .uasset — nodes, connections, settings)
 *   - USoundCueService   (asset-authoring: SoundCue graph construction on
 *                         the .uasset — cue nodes, root, settings)
 *
 * This service targets the *runtime component* side: it wraps
 * UGameplayStatics::SpawnSound{AtLocation,Attached} and UAudioComponent
 * accessors so Python can play spatial one-shots, drive volume / pitch /
 * bool params, and stop live components — no editor transaction is created
 * (runtime spawn).
 *
 * Mirrors the UNiagaraComponentService sibling: all UFUNCTIONs are static
 * and BlueprintCallable so they are reachable from Python as
 * `unreal.AudioComponentService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.AudioComponentService
 *
 *   # One-shot spatial spawn at a world location
 *   label = svc.spawn_sound_at_location(
 *       "/Game/Audio/SW_Impact",
 *       unreal.Vector(0, 0, 100),
 *       1.0,    # volume multiplier
 *       1.0)    # pitch multiplier
 *
 *   # Attached to a target actor / socket
 *   svc.spawn_sound_attached(
 *       "/Game/Audio/SW_EngineLoop",
 *       "Hero", "EngineSocket", 0.8, 1.2)
 *
 *   # Live parameter tweaks
 *   svc.set_audio_component_volume(label, 0.5)
 *   svc.set_audio_component_pitch(label, 1.3)
 *   svc.set_audio_component_bool_param(label, "bMuffled", True)
 *
 *   # Stop with optional fade
 *   svc.stop_audio_component(label, True, 0.5)
 */
UCLASS(BlueprintType)
class REVVY_API UAudioComponentService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Spawn
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a USoundBase (SoundWave / SoundCue / MetaSoundSource) at a
	 * world-space location, spatialized. A transient AActor is created to
	 * own the UAudioComponent so callers can drive it by label afterwards.
	 * The component is set to bAutoDestroy = true so the owning actor is
	 * cleaned up when playback ends.
	 *
	 * @param SoundAssetPath    - Package path to a USoundBase asset
	 *                            (e.g. "/Game/Audio/SW_Impact").
	 * @param Location          - World-space spawn location.
	 * @param VolumeMultiplier  - Playback volume multiplier (1.0 = default).
	 * @param PitchMultiplier   - Playback pitch multiplier (1.0 = default).
	 * @return Actor label of the owning transient AActor, or empty string
	 *         on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Spawn")
	static FString SpawnSoundAtLocation(
		const FString& SoundAssetPath,
		FVector Location,
		float VolumeMultiplier,
		float PitchMultiplier);

	/**
	 * Spawn a USoundBase attached to a target actor's root component (or
	 * to a named socket on the root component).
	 *
	 * @param SoundAssetPath         - Package path to a USoundBase asset.
	 * @param TargetActorNameOrPath  - Label / name / soft-object-path of
	 *                                 the actor to attach the sound to.
	 * @param SocketName             - Socket name on the target component
	 *                                 (blank attaches at the component root).
	 * @param VolumeMultiplier       - Playback volume multiplier.
	 * @param PitchMultiplier        - Playback pitch multiplier.
	 * @return Actor label of the UAudioComponent's owning actor (the target
	 *         actor when the engine parents the component under it), or
	 *         empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Spawn")
	static FString SpawnSoundAttached(
		const FString& SoundAssetPath,
		const FString& TargetActorNameOrPath,
		const FString& SocketName,
		float VolumeMultiplier,
		float PitchMultiplier);

	// ═══════════════════════════════════════════════════════════════════
	// Params
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the volume multiplier on the UAudioComponent owned by the target
	 * actor.
	 *
	 * @param ActorNameOrPath    - Label / name / soft-object-path of the
	 *                             actor hosting the UAudioComponent.
	 * @param VolumeMultiplier   - New volume multiplier (0.0 = silent).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Params")
	static bool SetAudioComponentVolume(
		const FString& ActorNameOrPath,
		float VolumeMultiplier);

	/**
	 * Set the pitch multiplier on the UAudioComponent owned by the target
	 * actor.
	 *
	 * @param ActorNameOrPath   - Label / name / soft-object-path of the actor.
	 * @param PitchMultiplier   - New pitch multiplier (1.0 = default).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Params")
	static bool SetAudioComponentPitch(
		const FString& ActorNameOrPath,
		float PitchMultiplier);

	/**
	 * Set a legacy SoundCue bool parameter on the UAudioComponent owned by
	 * the target actor via SetBoolParameter. Consumed by SoundCue Branch /
	 * ParamBranch nodes.
	 *
	 * Note: MetaSoundSource parameter binding is a separate API
	 * (FMetaSoundOutput / trigger dispatchers) — SetBoolParameter is a
	 * legacy SoundCue-facing hook and is not observed by MetaSound graphs.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @param ParamName       - Parameter name to bind.
	 * @param Value           - Bool value to bind.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Params")
	static bool SetAudioComponentBoolParam(
		const FString& ActorNameOrPath,
		const FString& ParamName,
		bool Value);

	// ═══════════════════════════════════════════════════════════════════
	// Control
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Stop the UAudioComponent owned by the target actor, optionally
	 * fading out over FadeDuration.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @param bFadeOut        - If true, FadeOut(FadeDuration, 0.0f);
	 *                          otherwise Stop() immediately.
	 * @param FadeDuration    - Fade duration in seconds (only used when
	 *                          bFadeOut is true).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AudioComponent|Control")
	static bool StopAudioComponent(
		const FString& ActorNameOrPath,
		bool bFadeOut,
		float FadeDuration);
};
