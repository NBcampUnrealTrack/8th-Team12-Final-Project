// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UNavigationService.generated.h"

/**
 * Navigation service exposed directly to Python for RecastNavMesh authoring
 * and path queries in the current editor level.
 *
 * Mirrors the ULightingService / USequencerService pattern: all UFUNCTIONs
 * are static and BlueprintCallable so they are reachable from Python as
 * `unreal.NavigationService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.NavigationService
 *
 *   # Volume + Build
 *   svc.spawn_nav_mesh_bounds_volume("NavVol", (0, 0, 0), (5000, 5000, 500))
 *   svc.rebuild_nav_mesh()
 *   busy = svc.is_nav_mesh_generating()
 *
 *   # Query
 *   bounds_json = svc.get_nav_mesh_bounds()
 *   path_json   = svc.find_path_between_points((0,0,0), (500,500,0))
 *   proj_json   = svc.project_point_on_nav_mesh((0,0,50), (200,200,200))
 *   rnd_json    = svc.get_random_reachable_point((0,0,0), 2000.0)
 *   length      = svc.get_path_length_between_points((0,0,0), (500,500,0))
 *
 *   # Config
 *   svc.set_nav_mesh_agent_radius(35.0)
 *   svc.set_nav_mesh_agent_height(144.0)
 */
UCLASS(BlueprintType)
class REVVY_API UNavigationService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Volume + Build
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn an ANavMeshBoundsVolume that defines the region where the
	 * RecastNavMesh will be generated.
	 *
	 * @param Name     - Actor label to assign
	 * @param Location - World-space center of the volume
	 * @param Extent   - Half-extent of the volume in cm (converts to actor scale, default brush is +/-100)
	 * @return Actor label of the spawned volume, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Volume")
	static FString SpawnNavMeshBoundsVolume(const FString& Name, FVector Location, FVector Extent);

	/**
	 * Trigger a full navigation rebuild on the editor world.
	 * @return True if the rebuild was successfully kicked off.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Build")
	static bool RebuildNavMesh();

	/**
	 * Query whether the navigation system is currently generating tiles.
	 * @return True if a build is in progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Build")
	static bool IsNavMeshGenerating();

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Get the current bounds of the default navigation data.
	 * @return JSON: { "valid": bool, "min": {x,y,z}, "max": {x,y,z} }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Query")
	static FString GetNavMeshBounds();

	/**
	 * Compute a synchronous path from Start to End on the default nav data.
	 * @return JSON: { "success": bool, "length": float, "points": [{x,y,z}, ...] }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Query")
	static FString FindPathBetweenPoints(FVector Start, FVector End);

	/**
	 * Project a world-space point onto the nearest reachable nav mesh location.
	 * @return JSON: { "success": bool, "location": {x,y,z}, "nodeRef": int }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Query")
	static FString ProjectPointOnNavMesh(FVector Point, FVector Extent);

	/**
	 * Pick a random reachable point on the nav mesh within Radius of Origin.
	 * @return JSON: { "success": bool, "location": {x,y,z} }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Query")
	static FString GetRandomReachablePoint(FVector Origin, float Radius);

	/**
	 * Compute only the length of the synchronous path between two points.
	 * @return Path length in cm, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Query")
	static float GetPathLengthBetweenPoints(FVector Start, FVector End);

	// ═══════════════════════════════════════════════════════════════════
	// Config
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the AgentRadius on the default RecastNavMesh (used for generation).
	 * @param Radius - New agent radius in cm.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Config")
	static bool SetNavMeshAgentRadius(float Radius);

	/**
	 * Set the AgentHeight on the default RecastNavMesh (used for generation).
	 * @param Height - New agent height in cm.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Navigation|Config")
	static bool SetNavMeshAgentHeight(float Height);
};
