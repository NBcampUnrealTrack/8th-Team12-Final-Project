// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UWorldTraceService.generated.h"

enum ECollisionChannel : int;
struct FHitResult;

/**
 * Result of a single world trace hit.
 *
 * When bBlockingHit is false the geometry fields (ImpactPoint, ImpactNormal,
 * Distance) are left at their defaults and the name fields are empty.
 */
USTRUCT(BlueprintType)
struct FTraceHitInfo
{
	GENERATED_BODY()

	/** True if the trace produced a blocking hit. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	bool bBlockingHit = false;

	/** World-space location of the impact point. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	FVector ImpactPoint = FVector::ZeroVector;

	/** World-space surface normal at the impact point. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	FVector ImpactNormal = FVector::ZeroVector;

	/** Distance from the trace start to the impact point. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	float Distance = 0.0f;

	/** Display label (or name) of the hit actor, empty if none. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	FString HitActorName = TEXT("");

	/** Name of the hit primitive component, empty if none. */
	UPROPERTY(BlueprintReadWrite, Category = "WorldTrace")
	FString HitComponentName = TEXT("");
};

/**
 * World trace service exposed directly to Python.
 *
 * Mirrors the Epic-native SceneTools.trace_world toolset: runs collision
 * traces against the editor world and returns structured hit information.
 *
 * Provides 3 trace actions:
 * - line_trace: Single blocking line trace between two points
 * - multi_line_trace: Line trace returning every hit along the ray
 * - sphere_trace: Swept sphere trace between two points
 *
 * Trace channel mapping (case-insensitive string -> ECollisionChannel):
 *   "Visibility"   -> ECC_Visibility (default)
 *   "Camera"       -> ECC_Camera
 *   "WorldStatic"  -> ECC_WorldStatic
 *   "WorldDynamic" -> ECC_WorldDynamic
 *   "Pawn"         -> ECC_Pawn
 *
 * Python Usage:
 *   import unreal
 *
 *   # Single line trace straight down from above the origin
 *   hit = unreal.WorldTraceService.line_trace(
 *       unreal.Vector(0, 0, 1000),
 *       unreal.Vector(0, 0, -1000),
 *       "Visibility",
 *       False)
 *   if hit.b_blocking_hit:
 *       print(f"Hit {hit.hit_actor_name} at {hit.impact_point}")
 *
 *   # Multi line trace - every overlap along the ray
 *   hits = unreal.WorldTraceService.multi_line_trace(
 *       unreal.Vector(0, 0, 1000),
 *       unreal.Vector(0, 0, -1000),
 *       "Visibility")
 *   for h in hits:
 *       print(h.hit_actor_name, h.distance)
 *
 *   # Swept sphere trace
 *   sweep = unreal.WorldTraceService.sphere_trace(
 *       unreal.Vector(0, 0, 1000),
 *       unreal.Vector(0, 0, -1000),
 *       50.0,
 *       "WorldStatic")
 *
 * @note This mirrors the SceneTools.trace_world MCP tool for the editor world.
 */
UCLASS(BlueprintType)
class REVVY_API UWorldTraceService : public UObject
{
	GENERATED_BODY()

public:
	// ============================================
	// Trace Actions
	// ============================================

	/**
	 * Perform a single blocking line trace in the editor world.
	 * Maps to SceneTools.trace_world (single).
	 *
	 * @param Start - World-space start of the trace
	 * @param End - World-space end of the trace
	 * @param TraceChannel - Collision channel name (default "Visibility")
	 * @param bTraceComplex - Trace against complex (per-poly) collision when true
	 * @return Hit info; bBlockingHit is false if nothing was hit
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTrace")
	static FTraceHitInfo LineTrace(
		const FVector& Start,
		const FVector& End,
		const FString& TraceChannel = TEXT("Visibility"),
		bool bTraceComplex = false);

	/**
	 * Perform a multi-hit line trace in the editor world, returning every hit
	 * along the ray (including the final blocking hit, if any).
	 * Maps to SceneTools.trace_world (multi).
	 *
	 * @param Start - World-space start of the trace
	 * @param End - World-space end of the trace
	 * @param TraceChannel - Collision channel name (default "Visibility")
	 * @return Array of hit info, ordered from nearest to farthest (empty on miss)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTrace")
	static TArray<FTraceHitInfo> MultiLineTrace(
		const FVector& Start,
		const FVector& End,
		const FString& TraceChannel = TEXT("Visibility"));

	/**
	 * Perform a swept sphere trace in the editor world.
	 * Maps to SceneTools.trace_world (sphere sweep).
	 *
	 * @param Start - World-space start of the sweep
	 * @param End - World-space end of the sweep
	 * @param Radius - Radius of the swept sphere
	 * @param TraceChannel - Collision channel name (default "Visibility")
	 * @return Hit info; bBlockingHit is false if nothing was hit
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|WorldTrace")
	static FTraceHitInfo SphereTrace(
		const FVector& Start,
		const FVector& End,
		float Radius,
		const FString& TraceChannel = TEXT("Visibility"));

private:
	/** Resolve the editor world used for tracing (null if unavailable). */
	static UWorld* GetEditorWorld();

	/** Map a channel name string to an ECollisionChannel (defaults to Visibility). */
	static ECollisionChannel ResolveTraceChannel(const FString& TraceChannel);

	/** Populate an FTraceHitInfo from an engine FHitResult. */
	static FTraceHitInfo MakeHitInfo(const FHitResult& Hit);
};
