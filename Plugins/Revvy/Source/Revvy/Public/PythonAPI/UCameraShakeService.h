// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UCameraShakeService.generated.h"

/**
 * Camera Shake service exposed directly to Python for runtime playback
 * and management of UCameraShakeBase effects on APlayerController /
 * APlayerCameraManager plus spatial UCameraShakeSourceComponent actors.
 *
 * Mirrors the ULightingService / UPerformanceService pattern: all
 * UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.CameraShakeService.<snake_case_name>(...)`.
 *
 * This is a runtime state service — no FScopedTransaction is required
 * because camera shakes are transient runtime effects (not persisted
 * asset / actor state).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.CameraShakeService
 *
 *   svc.play_camera_shake("PC0", "/Game/FX/CS_Explosion.CS_Explosion_C", 1.0)
 *   svc.stop_camera_shake_by_class("PC0", "/Game/FX/CS_Explosion.CS_Explosion_C")
 *   svc.stop_all_camera_shakes("PC0")
 *
 *   label = svc.spawn_camera_shake_source_actor(
 *       "ShakeSrc_Boss", unreal.Vector(0, 0, 200),
 *       "/Game/FX/CS_Explosion.CS_Explosion_C",
 *       1.0, 1500.0)  # intensity, outer_radius
 *
 *   active = json.loads(svc.list_active_camera_shakes("PC0"))
 *
 * @note All 5 camera shake operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UCameraShakeService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Playback
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Play a UCameraShakeBase subclass through the target
	 * APlayerController's APlayerCameraManager.
	 *
	 * @param PlayerControllerActorNameOrPath  Actor label / name / path of
	 *                                         the target APlayerController.
	 * @param ShakeClassPath                   Full path to the shake class
	 *                                         (e.g. "/Game/FX/CS_Foo.CS_Foo_C").
	 *                                         Must be a UCameraShakeBase subclass.
	 * @param Scale                            Scale to apply to the shake amplitude.
	 * @return True if the manager returned a valid shake instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CameraShake|Play")
	static bool PlayCameraShake(
		const FString& PlayerControllerActorNameOrPath,
		const FString& ShakeClassPath,
		float Scale);

	// ═══════════════════════════════════════════════════════════════════
	// Stop
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Stop all active instances of a specific shake class on the target
	 * player controller's camera manager.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param ShakeClassPath                   Full path to the shake class.
	 * @return True on success (class resolved and stop request dispatched).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CameraShake|Stop")
	static bool StopCameraShakeByClass(
		const FString& PlayerControllerActorNameOrPath,
		const FString& ShakeClassPath);

	/**
	 * Stop every active camera shake on the target player controller's
	 * camera manager.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return True if the request was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CameraShake|Stop")
	static bool StopAllCameraShakes(const FString& PlayerControllerActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Spatial Source
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn an actor in the current editor world carrying a
	 * UCameraShakeSourceComponent so nearby player cameras receive a
	 * distance-attenuated shake when playing shakes from that source.
	 *
	 * @param Name             Display label to assign to the new actor.
	 * @param Location         World-space spawn location.
	 * @param ShakeClassPath   Full path to a UCameraShakeBase subclass.
	 * @param Intensity        Intensity multiplier applied by the source.
	 * @param OuterRadius      Outer attenuation radius (cm).
	 * @return Actor label of the spawned source, or empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CameraShake|Source")
	static FString SpawnCameraShakeSourceActor(
		const FString& Name,
		FVector Location,
		const FString& ShakeClassPath,
		float Intensity,
		float OuterRadius);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List currently active camera shakes on the target player
	 * controller's camera manager.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return JSON array of {class_name}. Returns a JSON object with a
	 *         "note" field explaining unavailability if the underlying
	 *         active shake list is not accessible in this build.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CameraShake|Query")
	static FString ListActiveCameraShakes(const FString& PlayerControllerActorNameOrPath);
};
