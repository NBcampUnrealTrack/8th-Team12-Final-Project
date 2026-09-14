// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPCGService.generated.h"

/**
 * Procedural Content Generation (PCG) service exposed directly to Python for
 * asset-authoring and runtime component control via the UE 5.4+ PCG plugin.
 *
 * Mirrors the UGeometryScriptService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.PCGService.<snake_case_name>(...)`.
 *
 * Plugin requirement:
 *   Requires the "PCG" plugin to be enabled in the .uproject. If PCG is not
 *   enabled, the module will fail to link (PCG and PCGEditor are declared as
 *   private dependencies of Revvy).
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.PCGService
 *
 *   # Author a new PCG graph asset
 *   graph_path = svc.create_pcg_graph("/Game/PCG/PG_Forest")
 *
 *   # Bind the graph to a UPCGComponent on a placed actor
 *   comp_name = svc.create_pcg_component("PCGVolume_Forest", graph_path)
 *
 *   # Kick generation
 *   svc.generate_pcg_component("PCGVolume_Forest", True)
 *
 *   # Query status
 *   info_json = svc.get_pcg_component_info("PCGVolume_Forest")
 *
 *   # Cancel in-progress generation
 *   svc.cancel_pcg_generation("PCGVolume_Forest")
 */
UCLASS(BlueprintType)
class REVVY_API UPCGService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Asset authoring
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new empty UPCGGraph asset at OutputAssetPath.
	 * Wrapped in an FScopedTransaction and saved to disk on success.
	 *
	 * @param OutputAssetPath - Package path for the new UPCGGraph
	 *                         (e.g. "/Game/PCG/PG_Forest").
	 * @return The asset path on success, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PCG|Create")
	static FString CreatePCGGraph(const FString& OutputAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Component authoring
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a UPCGComponent on the given actor, bind the PCG graph asset,
	 * attach to the actor's root and register the component.
	 *
	 * @param ActorNameOrPath - Actor label / name / soft path (as resolved by
	 *                          the standard Revvy actor lookup).
	 * @param PCGGraphPath    - Package path of a UPCGGraph asset.
	 * @return The created component's name on success, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PCG|Component")
	static FString CreatePCGComponent(const FString& ActorNameOrPath, const FString& PCGGraphPath);

	// ═══════════════════════════════════════════════════════════════════
	// Generation control
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Trigger UPCGComponent::Generate(bForce) on the actor's PCG component.
	 *
	 * @param ActorNameOrPath - Actor label / name / soft path.
	 * @param bForce          - Forwarded to UPCGComponent::Generate; forces
	 *                          re-generation even when the cache is valid.
	 * @return True if a PCG component was found and Generate was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PCG|Generate")
	static bool GeneratePCGComponent(const FString& ActorNameOrPath, bool bForce);

	/**
	 * Cancel any in-progress generation on the actor's PCG component.
	 *
	 * @param ActorNameOrPath - Actor label / name / soft path.
	 * @return True on dispatch, false if no PCG component was resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PCG|Generate")
	static bool CancelPCGGeneration(const FString& ActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a JSON blob describing the actor's PCG component state:
	 *   { "graph_path": string, "is_generating": bool,
	 *     "is_partitioned": bool, "seed": int }
	 *
	 * @param ActorNameOrPath - Actor label / name / soft path.
	 * @return JSON string, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PCG|Query")
	static FString GetPCGComponentInfo(const FString& ActorNameOrPath);
};
