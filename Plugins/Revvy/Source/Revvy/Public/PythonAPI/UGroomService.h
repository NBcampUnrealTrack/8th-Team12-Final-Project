// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UGroomService.generated.h"

/**
 * Groom (hair / fur) authoring + attachment service exposed directly to Python
 * for creating UGroomBindingAsset assets against USkeletalMesh targets and for
 * attaching UGroomComponent instances to actors at runtime in the editor world.
 *
 * Mirrors the UChaosClothService sibling: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.GroomService.<snake_case_name>(...)`.
 *
 * Engine coverage:
 *  - Requires the HairStrands plugin (default-enabled in UE 5.8). Every
 *    UFUNCTION performs a runtime module-availability check via
 *    FModuleManager::IsModuleLoaded("HairStrandsCore") and gracefully returns
 *    "" / false if the plugin is not present, logging a Warning to
 *    LogRevvyGroom.
 *  - UGroomAsset / UGroomBindingAsset / UGroomComponent live in the
 *    HairStrandsCore module. Editor-only binding build helpers live in
 *    HairStrandsEditor.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.GroomService
 *
 *   # Create a binding of a groom against a target skeletal mesh
 *   svc.create_groom_binding_asset(
 *       "/Game/Chars/Duck/GB_Duck_Feathers",
 *       "/Game/Chars/Duck/GA_Duck_Feathers",
 *       "/Game/Chars/Duck/SK_Duck")
 *
 *   # Attach a groom to an actor at a socket
 *   comp_name = svc.attach_groom_to_actor(
 *       "Hero",
 *       "/Game/Chars/Duck/GA_Duck_Feathers",
 *       "/Game/Chars/Duck/GB_Duck_Feathers",
 *       "HeadSocket")
 *
 *   # Toggle visibility of a specific groom component on the actor
 *   svc.set_groom_enabled("Hero", 0, False)
 *
 *   # Enumerate all groom components on an actor
 *   print(svc.list_groom_components("Hero"))
 *
 * @note 4 groom authoring / attachment operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UGroomService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new UGroomBindingAsset that binds the given UGroomAsset to the
	 * given target USkeletalMesh. Prefers UGroomBindingAssetFactory via
	 * IAssetTools when available; falls back to CreatePackage + NewObject.
	 *
	 * @param OutputAssetPath   - Target package path for the new binding
	 *                            (e.g. "/Game/Chars/Duck/GB_Duck_Feathers").
	 * @param GroomAssetPath    - UGroomAsset package path to bind.
	 * @param TargetSkelMeshPath - USkeletalMesh package path to bind against.
	 * @return Package path of the created UGroomBindingAsset on success,
	 *         empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Groom|Create")
	static FString CreateGroomBindingAsset(
		const FString& OutputAssetPath,
		const FString& GroomAssetPath,
		const FString& TargetSkelMeshPath);

	// ═══════════════════════════════════════════════════════════════════
	// Attach
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Instantiate a new UGroomComponent on the given actor with the given
	 * UGroomAsset + UGroomBindingAsset, attach it to the actor's root scene
	 * component (optionally at SocketName), and register it with the world.
	 *
	 * @param ActorNameOrPath   - Actor label / name / path to attach to.
	 * @param GroomAssetPath    - UGroomAsset package path.
	 * @param BindingAssetPath  - UGroomBindingAsset package path (optional; may
	 *                            be empty to skip binding).
	 * @param SocketName        - Socket / bone name on the root component
	 *                            (may be empty for no socket).
	 * @return Registered component's name on success, empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Groom|Attach")
	static FString AttachGroomToActor(
		const FString& ActorNameOrPath,
		const FString& GroomAssetPath,
		const FString& BindingAssetPath,
		const FString& SocketName);

	// ═══════════════════════════════════════════════════════════════════
	// Config
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Toggle visibility / activity of a UGroomComponent on the given actor by
	 * component index (as reported by ListGroomComponents).
	 *
	 * @param ActorNameOrPath - Actor label / name / path.
	 * @param ComponentIndex  - Zero-based index into the actor's UGroomComponent list.
	 * @param bEnabled        - Whether to enable (visible + active) or disable.
	 * @return true on success, false on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Groom|Config")
	static bool SetGroomEnabled(
		const FString& ActorNameOrPath,
		int32 ComponentIndex,
		bool bEnabled);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enumerate every UGroomComponent on the given actor, reporting their
	 * groom asset path, binding asset path, and visibility state.
	 *
	 * @param ActorNameOrPath - Actor label / name / path.
	 * @return JSON array [{"index":..., "groom_path":..., "binding_path":...,
	 *         "is_visible":...}, ...]. Empty JSON array on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Groom|Query")
	static FString ListGroomComponents(const FString& ActorNameOrPath);

private:
	/** Return true iff the HairStrandsCore module is loaded. Logs a Warning if not. */
	static bool EnsureHairStrandsAvailable(const TCHAR* CallSite);
};
