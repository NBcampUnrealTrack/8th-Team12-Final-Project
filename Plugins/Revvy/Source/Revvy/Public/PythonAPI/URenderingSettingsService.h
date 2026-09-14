// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "URenderingSettingsService.generated.h"

/**
 * Rendering settings service exposed directly to Python for runtime
 * console-variable (CVar) and scalability toggles. Provides typed
 * convenience wrappers over the most common rendering knobs (Lumen,
 * Nanite, anti-aliasing, shadow / post-process quality, view distance,
 * scalability presets).
 *
 * Mirrors the ULightingService / USequencerService / UPerformanceService
 * pattern: all UFUNCTIONs are static + BlueprintCallable so they are
 * reachable from Python as
 *   `unreal.RenderingSettingsService.<snake_case_name>(...)`.
 *
 * CVar changes are runtime-only editor state (no asset mutation), so no
 * FScopedTransaction is used. For generic "set any CVar by name" access
 * see UEngineSettingsService::SetConsoleVariable — this service focuses
 * on strongly-typed rendering toggles.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.RenderingSettingsService
 *
 *   svc.set_lumen_enabled(True)
 *   svc.set_nanite_enabled(True)
 *   svc.set_anti_aliasing_method("TSR")
 *   svc.set_shadow_quality(3)
 *   svc.set_post_process_quality(3)
 *   svc.set_view_distance_scale(1.5)
 *
 *   settings = json.loads(svc.get_current_rendering_settings())
 *   svc.restore_scalability_defaults("Epic")
 *
 * @note All 10 rendering operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API URenderingSettingsService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Global Illumination / Lumen
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Toggle Lumen dynamic global illumination + reflections.
	 *
	 * When enabled, sets r.DynamicGlobalIlluminationMethod = 1 (Lumen) and
	 * r.ReflectionMethod = 1 (Lumen). When disabled, both are set to 0
	 * (None / SSR fallback).
	 *
	 * @param bEnabled True to enable Lumen GI + reflections, false to disable.
	 * @return true on success, false if the CVars could not be found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Lumen")
	static bool SetLumenEnabled(bool bEnabled);

	/**
	 * Toggle Lumen reflections independently of GI.
	 *
	 * Sets r.ReflectionMethod = 1 (Lumen) or 0 (SSR).
	 *
	 * @param bEnabled True to enable Lumen reflections, false to use SSR.
	 * @return true on success, false if the CVar could not be found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Lumen")
	static bool SetLumenReflectionsEnabled(bool bEnabled);

	/**
	 * Set the global illumination method by name.
	 *
	 * Accepted values (case-insensitive):
	 *   "None"     → r.DynamicGlobalIlluminationMethod = 0
	 *   "Lumen"    → r.DynamicGlobalIlluminationMethod = 1
	 *   "SSGI"     → r.DynamicGlobalIlluminationMethod = 2
	 *   "Plugin"   → r.DynamicGlobalIlluminationMethod = 3
	 *
	 * @param MethodStr Method name.
	 * @return true on success, false on unknown method or null CVar.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Lumen")
	static bool SetGlobalIlluminationMethod(const FString& MethodStr);

	// ═══════════════════════════════════════════════════════════════════
	// Nanite
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Toggle Nanite virtualized geometry. Sets r.Nanite = 1 or 0.
	 *
	 * @param bEnabled True to enable Nanite, false to disable.
	 * @return true on success, false if the CVar could not be found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Nanite")
	static bool SetNaniteEnabled(bool bEnabled);

	// ═══════════════════════════════════════════════════════════════════
	// Anti-Aliasing
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the anti-aliasing method by name.
	 *
	 * Accepted values (case-insensitive):
	 *   "None" → 0
	 *   "FXAA" → 1
	 *   "TAA"  → 2
	 *   "MSAA" → 3
	 *   "TSR"  → 4
	 *
	 * @param MethodStr AA method name.
	 * @return true on success, false on unknown method or null CVar.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|AA")
	static bool SetAntiAliasingMethod(const FString& MethodStr);

	// ═══════════════════════════════════════════════════════════════════
	// Scalability
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the shadow scalability bucket. Sets sg.ShadowQuality = Quality.
	 *
	 * @param Quality 0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic.
	 * @return true on success, false if the CVar could not be found or
	 *         Quality is out of range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Scalability")
	static bool SetShadowQuality(int32 Quality);

	/**
	 * Set the post-process scalability bucket. Sets sg.PostProcessQuality.
	 *
	 * @param Quality 0=Low, 1=Medium, 2=High, 3=Epic, 4=Cinematic.
	 * @return true on success, false if the CVar could not be found or
	 *         Quality is out of range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Scalability")
	static bool SetPostProcessQuality(int32 Quality);

	/**
	 * Set the view distance multiplier. Sets r.ViewDistanceScale.
	 *
	 * @param Scale Multiplier (>0). Values around 1.0 are typical.
	 * @return true on success, false if the CVar could not be found or
	 *         Scale is not positive.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Scalability")
	static bool SetViewDistanceScale(float Scale);

	// ═══════════════════════════════════════════════════════════════════
	// Query / Reset
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Read the current rendering-related CVar state and return as JSON.
	 *
	 * Returned JSON fields:
	 *   gi_method, reflection_method, nanite, aa_method,
	 *   shadow_quality, post_process_quality, view_distance_scale.
	 *
	 * @return JSON string. Empty string on early error (a warning is logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Query")
	static FString GetCurrentRenderingSettings();

	/**
	 * Apply a named scalability preset via Scalability::FQualityLevels.
	 *
	 * Accepted preset names (case-insensitive):
	 *   "Low"       → level 0
	 *   "Medium"    → level 1
	 *   "High"      → level 2
	 *   "Epic"      → level 3
	 *   "Cinematic" → level 4
	 *
	 * Falls back to `sg.QualityLevel <n>` via GEngine->Exec when the
	 * Scalability module is unavailable.
	 *
	 * @param PresetName Preset name.
	 * @return true on success, false on unknown preset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Rendering|Query")
	static bool RestoreScalabilityDefaults(const FString& PresetName);
};
