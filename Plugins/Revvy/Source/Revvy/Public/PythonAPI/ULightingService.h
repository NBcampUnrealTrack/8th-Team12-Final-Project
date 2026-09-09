// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ULightingService.generated.h"

/**
 * Lighting service exposed directly to Python for spawning and tuning
 * light actors, sky lighting, post-process volumes, fog, reflection
 * captures and lightmass volumes in the current editor level.
 *
 * Mirrors the UActorService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.LightingService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.LightingService
 *
 *   # Directional / Sun
 *   svc.spawn_directional_light("Sun", (0, 0, 500), (-45, 0, 0),
 *                               unreal.LinearColor(1, 1, 1, 1), 10.0)
 *   svc.set_directional_light_as_atmosphere_sun("Sun", 0)
 *
 *   # Sky Light
 *   svc.spawn_sky_light("Sky", (0, 0, 300))
 *   svc.set_sky_light_real_time_capture("Sky", True)
 *   svc.recapture_sky("Sky")
 *
 *   # Point / Spot / Rect
 *   svc.spawn_point_light("PL1", (0, 0, 300),
 *                         unreal.LinearColor(1, 1, 1, 1), 5000.0, 1000.0)
 *   svc.spawn_spot_light("SL1", (0, 0, 300), (-45, 0, 0),
 *                        unreal.LinearColor(1, 1, 1, 1), 5000.0, 30.0, 45.0)
 *   svc.spawn_rect_light("RL1", (0, 0, 300), (0, 0, 0),
 *                        unreal.LinearColor(1, 1, 1, 1), 5000.0, 200.0, 100.0)
 *   svc.set_rect_light_source_texture("RL1", "/Game/Textures/T_LightPattern")
 *
 *   # Post Process
 *   svc.spawn_post_process_volume("PP1", (0, 0, 0), (500, 500, 500), True)
 *   svc.set_post_process_exposure_bias("PP1", 1.5)
 *   svc.set_post_process_bloom_intensity("PP1", 0.8)
 *
 *   # Fog
 *   svc.spawn_exponential_height_fog("Fog", (0, 0, 0), 0.02, 0.2)
 *   svc.spawn_local_fog_volume("LFog", (0, 0, 0), 0.05)
 *
 *   # Reflection Capture
 *   svc.spawn_sphere_reflection_capture("Refl", (0, 0, 300), 3000.0)
 *   svc.update_reflection_capture_contents()
 *
 *   # Lightmass
 *   svc.spawn_lightmass_importance_volume("LMImportance", (0, 0, 0),
 *                                         (4000, 4000, 2000))
 *
 * @note All 17 lighting operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API ULightingService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Directional / Sun Lighting
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a directional light actor in the current editor world.
	 *
	 * @param Name       - Display label to assign to the new actor (blank keeps default)
	 * @param Location   - World-space spawn location
	 * @param Rotation   - World-space rotation (sun direction)
	 * @param Color      - Light color
	 * @param Intensity  - Light intensity in lux (cd/m^2 in physical units)
	 * @return Actor label of the spawned light, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Directional")
	static FString SpawnDirectionalLight(
		const FString& Name,
		FVector Location,
		FRotator Rotation,
		FLinearColor Color,
		float Intensity);

	/**
	 * Mark an existing directional light as the atmospheric sun light.
	 *
	 * @param ActorNameOrPath - Actor label, name, or soft object path of the light
	 * @param SunIndex        - Sun index (0 = primary sun, 1 = secondary)
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Directional")
	static bool SetDirectionalLightAsAtmosphereSun(const FString& ActorNameOrPath, int32 SunIndex);

	// ═══════════════════════════════════════════════════════════════════
	// Sky Light
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a sky light actor in the current editor world.
	 *
	 * @param Name     - Display label to assign to the new actor
	 * @param Location - World-space spawn location
	 * @return Actor label of the spawned sky light, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Sky")
	static FString SpawnSkyLight(const FString& Name, FVector Location);

	/**
	 * Trigger a sky recapture on the specified sky light.
	 *
	 * @param ActorNameOrPath - Actor label, name, or soft object path of the sky light
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Sky")
	static bool RecaptureSky(const FString& ActorNameOrPath);

	/**
	 * Enable or disable real-time capture on a sky light.
	 *
	 * @param ActorNameOrPath - Actor label, name, or soft object path of the sky light
	 * @param bEnabled        - True to enable real-time capture
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Sky")
	static bool SetSkyLightRealTimeCapture(const FString& ActorNameOrPath, bool bEnabled);

	// ═══════════════════════════════════════════════════════════════════
	// Point / Spot / Rect Lights
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a point light actor in the current editor world.
	 *
	 * @param Name              - Display label to assign to the new actor
	 * @param Location          - World-space spawn location
	 * @param Color             - Light color
	 * @param Intensity         - Light intensity
	 * @param AttenuationRadius - Distance at which the light falls off to zero
	 * @return Actor label of the spawned light, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Point")
	static FString SpawnPointLight(
		const FString& Name,
		FVector Location,
		FLinearColor Color,
		float Intensity,
		float AttenuationRadius);

	/**
	 * Spawn a spot light actor in the current editor world.
	 *
	 * @param Name            - Display label to assign to the new actor
	 * @param Location        - World-space spawn location
	 * @param Rotation        - World-space rotation (cone direction)
	 * @param Color           - Light color
	 * @param Intensity       - Light intensity
	 * @param InnerConeAngle  - Inner cone half-angle in degrees
	 * @param OuterConeAngle  - Outer cone half-angle in degrees
	 * @return Actor label of the spawned light, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Spot")
	static FString SpawnSpotLight(
		const FString& Name,
		FVector Location,
		FRotator Rotation,
		FLinearColor Color,
		float Intensity,
		float InnerConeAngle,
		float OuterConeAngle);

	/**
	 * Spawn a rect light actor in the current editor world.
	 *
	 * @param Name         - Display label to assign to the new actor
	 * @param Location     - World-space spawn location
	 * @param Rotation     - World-space rotation (surface normal points along +X)
	 * @param Color        - Light color
	 * @param Intensity    - Light intensity
	 * @param SourceWidth  - Emissive rectangle width in cm
	 * @param SourceHeight - Emissive rectangle height in cm
	 * @return Actor label of the spawned light, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Rect")
	static FString SpawnRectLight(
		const FString& Name,
		FVector Location,
		FRotator Rotation,
		FLinearColor Color,
		float Intensity,
		float SourceWidth,
		float SourceHeight);

	/**
	 * Set the source texture (light-cookie) on a rect light.
	 *
	 * @param ActorNameOrPath   - Actor label, name, or soft object path of the rect light
	 * @param TextureAssetPath  - Package path to a UTexture asset (e.g. "/Game/Textures/T_Cookie")
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Rect")
	static bool SetRectLightSourceTexture(const FString& ActorNameOrPath, const FString& TextureAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Post Process
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a post-process volume in the current editor world.
	 *
	 * @param Name     - Display label to assign to the new actor
	 * @param Location - World-space center of the volume
	 * @param Extent   - Half-extent of the box in world units (converted to actor scale)
	 * @param bUnbound - If true the volume affects the entire world regardless of extent
	 * @return Actor label of the spawned volume, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|PostProcess")
	static FString SpawnPostProcessVolume(
		const FString& Name,
		FVector Location,
		FVector Extent,
		bool bUnbound);

	/**
	 * Enable and set the auto-exposure bias on a post-process volume.
	 *
	 * @param ActorNameOrPath - Actor label, name, or soft object path of the volume
	 * @param ExposureBias    - Exposure compensation in EV stops
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|PostProcess")
	static bool SetPostProcessExposureBias(const FString& ActorNameOrPath, float ExposureBias);

	/**
	 * Enable and set the bloom intensity on a post-process volume.
	 *
	 * @param ActorNameOrPath - Actor label, name, or soft object path of the volume
	 * @param BloomIntensity  - Bloom scalar (0 = disabled)
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|PostProcess")
	static bool SetPostProcessBloomIntensity(const FString& ActorNameOrPath, float BloomIntensity);

	// ═══════════════════════════════════════════════════════════════════
	// Fog
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn an exponential height fog actor.
	 *
	 * @param Name          - Display label to assign to the new actor
	 * @param Location      - World-space spawn location
	 * @param FogDensity    - Fog density (0..1 range typical)
	 * @param HeightFalloff - Height falloff rate
	 * @return Actor label of the spawned fog, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Fog")
	static FString SpawnExponentialHeightFog(
		const FString& Name,
		FVector Location,
		float FogDensity,
		float HeightFalloff);

	/**
	 * Spawn a local fog volume actor (UE 5.5+). On older engine versions this
	 * logs a warning and returns an empty string.
	 *
	 * @param Name       - Display label to assign to the new actor
	 * @param Location   - World-space spawn location
	 * @param FogDensity - Fog extinction / density scalar
	 * @return Actor label of the spawned volume, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Fog")
	static FString SpawnLocalFogVolume(const FString& Name, FVector Location, float FogDensity);

	// ═══════════════════════════════════════════════════════════════════
	// Reflection Capture
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a sphere reflection capture actor.
	 *
	 * @param Name            - Display label to assign to the new actor
	 * @param Location        - World-space spawn location
	 * @param InfluenceRadius - Radius of the reflection capture in world units
	 * @return Actor label of the spawned capture, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Reflection")
	static FString SpawnSphereReflectionCapture(
		const FString& Name,
		FVector Location,
		float InfluenceRadius);

	/**
	 * Update / rebuild every reflection capture in the current editor world.
	 *
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Reflection")
	static bool UpdateReflectionCaptureContents();

	// ═══════════════════════════════════════════════════════════════════
	// Lightmass
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a lightmass importance volume in the current editor world.
	 *
	 * @param Name     - Display label to assign to the new actor
	 * @param Location - World-space center of the volume
	 * @param Extent   - Half-extent in world units (converted to actor scale)
	 * @return Actor label of the spawned volume, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Lighting|Lightmass")
	static FString SpawnLightmassImportanceVolume(
		const FString& Name,
		FVector Location,
		FVector Extent);

private:
	/**
	 * Apply an actor display label if non-empty and return the final label,
	 * so spawn methods can return a stable identifier back to Python.
	 */
	static FString ApplyActorLabel(AActor* Actor, const FString& DesiredName);
};
