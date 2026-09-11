// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UWorldPartitionService.generated.h"

/**
 * World Partition service exposed directly to Python for UE 5.8 World Partition
 * authoring workflows: streaming inspection, actor pinning, data layer editing,
 * and HLOD builds against the current editor level.
 *
 * Mirrors the ULightingService / UNavigationService pattern: all UFUNCTIONs are
 * static and BlueprintCallable so they are reachable from Python as
 * `unreal.WorldPartitionService.<snake_case_name>(...)`.
 *
 * All operations resolve the target UWorldPartition from
 * `GEditor->GetEditorWorldContext().World()->GetWorldPartition()` and
 * short-circuit with a Warning log if the current level is not a WP level.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.WorldPartitionService
 *
 *   # Query
 *   info  = svc.get_world_partition_info()
 *   cells = svc.get_loaded_cells()
 *
 *   # Streaming
 *   svc.pin_actors_in_bounds((0,0,0), (5000,5000,500))
 *   svc.unpin_actors_by_guid(["<guid1>", "<guid2>"])
 *   svc.force_load_cells_in_bounds((0,0,0), (5000,5000,500))
 *   svc.force_unload_cells_in_bounds((0,0,0), (5000,5000,500))
 *
 *   # Data Layer
 *   svc.list_data_layers()
 *   svc.assign_actor_to_data_layer("Actor_0", "/Game/DL/DL_Foliage.DL_Foliage")
 *   svc.set_data_layer_runtime_state("/Game/DL/DL_Foliage.DL_Foliage", "Activated")
 *
 *   # HLOD
 *   svc.build_hlods()
 */
UCLASS(BlueprintType)
class REVVY_API UWorldPartitionService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a JSON summary of the current editor world's UWorldPartition:
	 * {"is_initialized":bool, "streaming_source_count":int, "cell_count":int,
	 *  "bounds":{"min":{x,y,z}, "max":{x,y,z}}}
	 * Returns error JSON if no editor world or the level is not WP-enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Query")
	static FString GetWorldPartitionInfo();

	/**
	 * Return a JSON array of currently loaded/activated runtime cells:
	 * [{"name":"...", "coord":{x,y,z}, "state":"Unloaded|Loaded|Activated"}]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Query")
	static FString GetLoadedCells();

	// ═══════════════════════════════════════════════════════════════════
	// Streaming
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Pin all actors whose runtime bounds intersect the [Min,Max] AABB. Pinned
	 * actors stay loaded in the editor regardless of streaming source coverage.
	 * @return Number of actors pinned.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Streaming")
	static int32 PinActorsInBounds(FVector Min, FVector Max);

	/**
	 * Unpin the given actor descriptor GUIDs (as parsed FGuid strings).
	 * @return true if the request was dispatched, false if the level is not WP.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Streaming")
	static bool UnpinActorsByGuid(const TArray<FString>& GuidStrings);

	/**
	 * Force-load all runtime cells whose bounds intersect [Min,Max] via an
	 * internal FLoaderAdapterShape registry keyed by the bounds signature.
	 * Idempotent: repeated calls for the same bounds reuse the same adapter.
	 * @return Number of cells that intersect the region (triggered).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Streaming")
	static int32 ForceLoadCellsInBounds(FVector Min, FVector Max);

	/**
	 * Force-unload any adapters previously registered for [Min,Max] by
	 * ForceLoadCellsInBounds. @return Number of cells that were released.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|Streaming")
	static int32 ForceUnloadCellsInBounds(FVector Min, FVector Max);

	// ═══════════════════════════════════════════════════════════════════
	// Data Layer
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List every UDataLayerInstance in the current editor world:
	 * [{"name":"...", "asset_path":"...", "runtime_state":"...",
	 *   "is_initial_visible":bool}]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|DataLayer")
	static FString ListDataLayers();

	/**
	 * Assign an actor to the DataLayer identified by the given
	 * UDataLayerAsset object path. Wrapped in FScopedTransaction; both the
	 * actor and the resolved DataLayerInstance are Modify()'d.
	 * @param ActorNameOrPath   Actor label or full object path
	 * @param DataLayerAssetPath Object path of the UDataLayerAsset
	 * @return true on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|DataLayer")
	static bool AssignActorToDataLayer(const FString& ActorNameOrPath, const FString& DataLayerAssetPath);

	/**
	 * Set the runtime state of the DataLayer identified by the given asset
	 * path. StateStr must be one of "Unloaded"|"Loaded"|"Activated".
	 * Wrapped in FScopedTransaction; the DataLayerInstance is Modify()'d.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|DataLayer")
	static bool SetDataLayerRuntimeState(const FString& DataLayerAssetPath, const FString& StateStr);

	// ═══════════════════════════════════════════════════════════════════
	// HLOD
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Trigger a World Partition HLOD build via the
	 * IWorldPartitionHLODUtilitiesModule (if loaded). Async operation: returns
	 * immediately after dispatch. Logs a Warning and returns false if the
	 * WorldPartitionHLODUtilities module is not available.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldPartition|HLOD")
	static bool BuildHLODs();
};
