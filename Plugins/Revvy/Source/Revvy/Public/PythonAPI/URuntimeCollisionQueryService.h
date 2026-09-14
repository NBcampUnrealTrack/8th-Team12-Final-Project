// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "URuntimeCollisionQueryService.generated.h"

/**
 * Runtime Collision Query service exposed directly to Python for
 * on-demand line traces and overlap queries against the current editor
 * world (typically PIE).
 *
 * Mirrors the UDamageSystemService pattern: all UFUNCTIONs are static
 * and BlueprintCallable so they are reachable from Python as
 * `unreal.RuntimeCollisionQueryService.<snake_case_name>(...)`.
 *
 * This is a runtime query service — no FScopedTransaction is required
 * because trace / overlap queries are pure reads (no persisted state).
 *
 * Trace channel mapping (case-insensitive string -> ECollisionChannel):
 *   "Visibility"   -> ECC_Visibility (default)
 *   "Camera"       -> ECC_Camera
 *   "WorldStatic"  -> ECC_WorldStatic
 *   "WorldDynamic" -> ECC_WorldDynamic
 *   "Pawn"         -> ECC_Pawn
 *   "PhysicsBody"  -> ECC_PhysicsBody
 *
 * All results are returned as JSON strings for easy consumption from
 * Python (mirroring several other Revvy query services).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.RuntimeCollisionQueryService
 *
 *   # Single blocking line trace
 *   result = json.loads(svc.line_trace_single(
 *       unreal.Vector(0, 0, 1000),
 *       unreal.Vector(0, 0, -1000),
 *       "Visibility"))
 *
 *   # Sphere overlap - actors inside radius
 *   hits = json.loads(svc.sphere_overlap_actors(
 *       unreal.Vector(0, 0, 100), 500.0, ["PC0"]))
 *
 *   # Box overlap - actors inside oriented box
 *   hits = json.loads(svc.box_overlap_actors(
 *       unreal.Vector(0, 0, 100),
 *       unreal.Vector(100, 100, 100),
 *       unreal.Rotator(0, 0, 0),
 *       []))
 *
 *   # Multi-hit line trace with ignore list
 *   hits = json.loads(svc.multi_line_trace_by_channel(
 *       unreal.Vector(0, 0, 1000),
 *       unreal.Vector(0, 0, -1000),
 *       "Visibility",
 *       ["PC0"]))
 *
 * @note 4 collision query operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API URuntimeCollisionQueryService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Line Traces
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Perform a single blocking line trace by channel against the current
	 * editor world.
	 *
	 * @param Start            World-space start of the trace.
	 * @param End              World-space end of the trace.
	 * @param TraceChannelStr  Collision channel name. One of:
	 *                         "Visibility" | "Camera" | "WorldStatic" |
	 *                         "WorldDynamic" | "Pawn" | "PhysicsBody".
	 *                         Empty / unknown -> "Visibility".
	 * @return JSON object:
	 *   {
	 *     "hit": bool,
	 *     "actor_label": string,
	 *     "impact_point":  {"x": float, "y": float, "z": float},
	 *     "impact_normal": {"x": float, "y": float, "z": float},
	 *     "distance": float
	 *   }
	 *   On error (no world), returns a JSON object with "hit":false and
	 *   an "error" field describing the failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Collision|LineTrace")
	static FString LineTraceSingle(
		FVector Start,
		FVector End,
		const FString& TraceChannelStr);

	/**
	 * Perform a multi-hit line trace by channel against the current editor
	 * world. Returns every hit along the ray, honouring the ignore list.
	 *
	 * @param Start                     World-space start of the trace.
	 * @param End                       World-space end of the trace.
	 * @param TraceChannelStr           Collision channel name (see
	 *                                  LineTraceSingle for the accepted
	 *                                  values).
	 * @param IgnoreActorNamesOrPaths   Actor labels / names / paths to add
	 *                                  to FCollisionQueryParams's ignored
	 *                                  actors list. Unresolvable entries
	 *                                  are silently dropped.
	 * @return JSON array of hit objects (each with the same shape as the
	 *         LineTraceSingle result, minus the top-level "hit" flag).
	 *         On error (no world), returns "[]".
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Collision|LineTrace")
	static FString MultiLineTraceByChannel(
		FVector Start,
		FVector End,
		const FString& TraceChannelStr,
		const TArray<FString>& IgnoreActorNamesOrPaths);

	// ═══════════════════════════════════════════════════════════════════
	// Overlaps
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Perform a sphere overlap query and return every unique actor whose
	 * primitive components overlap the sphere.
	 *
	 * @param Origin                    World-space centre of the sphere.
	 * @param Radius                    Sphere radius (must be > 0).
	 * @param IgnoreActorNamesOrPaths   Actor labels / names / paths to
	 *                                  omit from the result. Unresolvable
	 *                                  entries are silently dropped.
	 * @return JSON array:
	 *   [ { "actor_label": string, "distance_from_origin": float }, ... ]
	 *   On error (no world / invalid radius), returns "[]".
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Collision|Overlap")
	static FString SphereOverlapActors(
		FVector Origin,
		float Radius,
		const TArray<FString>& IgnoreActorNamesOrPaths);

	/**
	 * Perform an oriented box overlap query and return every unique actor
	 * whose primitive components overlap the box.
	 *
	 * @param Origin                    World-space centre of the box.
	 * @param Extent                    Half-extents of the box (x / y / z).
	 * @param Rotation                  Orientation of the box in world space.
	 * @param IgnoreActorNamesOrPaths   Actor labels / names / paths to
	 *                                  omit from the result. Unresolvable
	 *                                  entries are silently dropped.
	 * @return JSON array:
	 *   [ { "actor_label": string, "distance_from_origin": float }, ... ]
	 *   On error (no world), returns "[]".
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Collision|Overlap")
	static FString BoxOverlapActors(
		FVector Origin,
		FVector Extent,
		FRotator Rotation,
		const TArray<FString>& IgnoreActorNamesOrPaths);
};
