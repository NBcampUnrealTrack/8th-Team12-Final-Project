// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Box.h"
#include "UGreyboxService.generated.h"

/**
 * Aggregate bounds result for a set of greybox actors. Returned by
 * UGreyboxService::GetGreyboxBounds so Python callers can inspect the
 * combined AABB, its origin (center) and half-extent in one struct.
 */
USTRUCT(BlueprintType)
struct FGreyboxBoundsResult
{
	GENERATED_BODY()

	/** True when at least one input actor was resolved and contributed to the bounds. */
	UPROPERTY(BlueprintReadWrite, Category = "Revvy|Greybox")
	bool bValid = false;

	/** Combined world-space AABB of all resolved input actors. */
	UPROPERTY(BlueprintReadWrite, Category = "Revvy|Greybox")
	FBox Bounds = FBox(ForceInit);

	/** Center of the combined AABB in world space. */
	UPROPERTY(BlueprintReadWrite, Category = "Revvy|Greybox")
	FVector Origin = FVector::ZeroVector;

	/** Half-extent of the combined AABB. */
	UPROPERTY(BlueprintReadWrite, Category = "Revvy|Greybox")
	FVector Extent = FVector::ZeroVector;
};

/**
 * Procedural greybox / blockout authoring service exposed to Python.
 *
 * Provides simple, deterministic building blocks (boxes, rooms, corridors,
 * stairs, ramps, grid arrays, linear arrays) plus query / bake helpers
 * (aggregate bounds, JSON round-trip, static-mesh conversion) that let
 * an agent rapidly rough-in a level with cube-based static mesh actors.
 *
 * Mirrors the ULightingService / USequencerService pattern: all
 * UFUNCTIONs are static and BlueprintCallable so they are reachable
 * from Python as `unreal.GreyboxService.<snake_case_name>(...)`.
 *
 * NOTE: This service is intentionally distinct from
 * `UMapBlockoutService` (a landscape-driven FPS map pipeline). This
 * class is for direct primitive-based greybox authoring only.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.GreyboxService
 *
 *   # Single primitives
 *   svc.spawn_greybox_box("Floor", (0, 0, 0), (2000, 2000, 20), "")
 *   svc.spawn_greybox_ramp("Ramp1", (0, 0, 0), (500, 0, 200), 300, 20)
 *
 *   # Compound layouts
 *   svc.spawn_greybox_room("RoomA", (0, 0, 0), (800, 600, 300), 20, True)
 *   svc.spawn_greybox_corridor("Hall1", (0, 0, 0), (2000, 0, 0), 300, 300, 20)
 *   svc.spawn_greybox_stairs("Stairs1", (0, 0, 0), 10, 20, 30, 200,
 *                            unreal.Rotator(0, 0, 0))
 *
 *   # Arrays
 *   svc.spawn_greybox_grid("Tiles", (0, 0, 0),
 *                          unreal.Vector2D(200, 200), 4, 4, "")
 *   svc.array_greybox_meshes("Pillar", 8, (250, 0, 0), "X")
 *
 *   # Query / bake
 *   res = svc.get_greybox_bounds(["Floor", "RoomA_Root"])
 *   json_str = svc.export_greybox_to_json("RoomA_Root")
 *   svc.import_greybox_from_json(json_str, "RoomA_Copy", (5000, 0, 0))
 *   svc.convert_greybox_to_static_mesh("RoomA_Root",
 *                                      "/Game/Greybox/SM_RoomA")
 */
UCLASS(BlueprintType)
class REVVY_API UGreyboxService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Geometry Primitives
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a single greybox box (StaticMeshActor using /Engine/BasicShapes/Cube
	 * scaled to Size in world units).
	 *
	 * @param Name              - Display label to assign to the new actor
	 * @param Location          - World-space center of the box
	 * @param Size              - World-space size in cm (X, Y, Z); Cube is 100cm so scale = Size/100
	 * @param MaterialAssetPath - Optional /Game path to a UMaterialInterface; empty or invalid keeps default
	 * @return Actor label of the spawned box, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Geometry")
	static FString SpawnGreyboxBox(const FString& Name, FVector Location, FVector Size, const FString& MaterialAssetPath);

	/**
	 * Spawn a rectangular room made of 4 walls, a floor, and an optional
	 * ceiling parented to a single AActor root.
	 *
	 * @param RoomName         - Base label for the root actor; walls / floor / ceiling get suffixed names
	 * @param Center           - World-space center of the room footprint (Z = floor top)
	 * @param Size             - Internal room size in cm (X, Y, Z)
	 * @param WallThickness    - Thickness of every wall / floor / ceiling slab in cm
	 * @param bIncludeCeiling  - When true, emit a ceiling slab at Z = Size.Z + WallThickness
	 * @return Array of actor labels: [Root, Floor, WallN, WallS, WallE, WallW, Ceiling?]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Geometry")
	static TArray<FString> SpawnGreyboxRoom(const FString& RoomName, FVector Center, FVector Size, float WallThickness, bool bIncludeCeiling);

	/**
	 * Spawn a straight corridor (floor + 2 side walls + ceiling) between Start and End.
	 * The corridor is axis-aligned to the Start->End vector.
	 *
	 * @param Name           - Base label for the root actor
	 * @param Start          - World-space start position (floor center)
	 * @param End            - World-space end position (floor center)
	 * @param Width          - Interior width in cm
	 * @param Height         - Interior height in cm
	 * @param WallThickness  - Thickness of walls / floor / ceiling in cm
	 * @return Array of actor labels: [Root, Floor, Ceiling, WallLeft, WallRight]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Geometry")
	static TArray<FString> SpawnGreyboxCorridor(const FString& Name, FVector Start, FVector End, float Width, float Height, float WallThickness);

	/**
	 * Spawn a run of stairs as a series of step boxes parented to a root.
	 *
	 * @param Name         - Base label for the root actor
	 * @param BasePos      - World-space position of the base of the first step
	 * @param StepCount    - Number of steps to emit (>0)
	 * @param StepRise     - Vertical rise per step in cm
	 * @param StepRun      - Horizontal run per step in cm
	 * @param StepWidth    - Width of every step in cm
	 * @param DirectionRot - Rotation applied to the root, defines climb direction
	 * @return Array of actor labels: [Root, Step_0, Step_1, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Geometry")
	static TArray<FString> SpawnGreyboxStairs(const FString& Name, FVector BasePos, int32 StepCount, float StepRise, float StepRun, float StepWidth, FRotator DirectionRot);

	/**
	 * Spawn a flat ramp box connecting Start to End, rotated to face End.
	 *
	 * @param Name       - Display label to assign to the new actor
	 * @param Start      - World-space start position (low end)
	 * @param End        - World-space end position (high end)
	 * @param Width      - Ramp width in cm
	 * @param Thickness  - Ramp slab thickness in cm
	 * @return Actor label of the spawned ramp, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Geometry")
	static FString SpawnGreyboxRamp(const FString& Name, FVector Start, FVector End, float Width, float Thickness);

	// ═══════════════════════════════════════════════════════════════════
	// Array / Distribution
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a GridX x GridY grid of cells parented to a single root actor.
	 * Each cell is either the asset at CellAssetPath (if valid) or a
	 * default cube scaled to CellSize.
	 *
	 * @param Name           - Base label for the root actor; cells get suffixed names
	 * @param Origin         - World-space origin (grid corner at [0,0])
	 * @param CellSize       - Size of every cell in cm (X, Y)
	 * @param GridX          - Number of cells along X (>0)
	 * @param GridY          - Number of cells along Y (>0)
	 * @param CellAssetPath  - Optional /Game path to a UStaticMesh; empty falls back to Cube
	 * @return Array of actor labels: [Root, Cell_0_0, Cell_0_1, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Array")
	static TArray<FString> SpawnGreyboxGrid(const FString& Name, FVector Origin, FVector2D CellSize, int32 GridX, int32 GridY, const FString& CellAssetPath);

	/**
	 * Duplicate an existing greybox actor along an axis Count times.
	 *
	 * @param BaseActorNameOrPath - Label / soft-path of the source actor
	 * @param Count               - Number of copies to emit (>0)
	 * @param Spacing             - Per-instance offset added to each copy (world space)
	 * @param AxisStr             - "X" / "Y" / "Z" / "XYZ" — which components of Spacing to apply
	 * @return Array of newly created actor labels (excludes the source)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Array")
	static TArray<FString> ArrayGreyboxMeshes(const FString& BaseActorNameOrPath, int32 Count, FVector Spacing, const FString& AxisStr);

	// ═══════════════════════════════════════════════════════════════════
	// Query / Bake
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Compute the aggregate world-space AABB of all supplied actors
	 * (labels, names or soft-object paths).
	 *
	 * @param ActorNamesOrPaths - Actor identifiers to include
	 * @return FGreyboxBoundsResult with bValid=false when nothing resolves
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Query")
	static FGreyboxBoundsResult GetGreyboxBounds(const TArray<FString>& ActorNamesOrPaths);

	/**
	 * Serialize a greybox actor + its descendants to a JSON string.
	 * Captures class, relative transform, static-mesh asset path and
	 * material asset paths per child.
	 *
	 * @param RootActorNameOrPath - Root actor to walk
	 * @return JSON string, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Query")
	static FString ExportGreyboxToJSON(const FString& RootActorNameOrPath);

	/**
	 * Import a greybox hierarchy previously produced by ExportGreyboxToJSON,
	 * placing the new root at RootLocation.
	 *
	 * @param JsonString   - JSON payload from ExportGreyboxToJSON
	 * @param NewRootName  - Label to assign to the new root actor
	 * @param RootLocation - World-space location for the new root
	 * @return Array of newly created actor labels: [Root, Child, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Query")
	static TArray<FString> ImportGreyboxFromJSON(const FString& JsonString, const FString& NewRootName, FVector RootLocation);

	/**
	 * Convert a greybox hierarchy into a single UStaticMesh asset.
	 *
	 * NAIVE fallback implementation: duplicates the UStaticMesh referenced
	 * by the first descendant that owns a StaticMeshComponent via
	 * UEditorAssetLibrary::DuplicateAsset to OutputAssetPath. Emits a
	 * LogRevvyGreybox Warning describing the limitation. A proper mesh
	 * merge / geometry bake is out of scope.
	 *
	 * @param RootActorNameOrPath - Root actor whose descendants supply the source mesh
	 * @param OutputAssetPath     - /Game/... target asset path for the duplicated mesh
	 * @return Package path of the duplicated asset, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Greybox|Query")
	static FString ConvertGreyboxToStaticMesh(const FString& RootActorNameOrPath, const FString& OutputAssetPath);

private:
	/**
	 * Apply an actor display label if non-empty and return the final label,
	 * so spawn methods can return a stable identifier back to Python.
	 */
	static FString ApplyActorLabel(AActor* Actor, const FString& DesiredName);

	/**
	 * Load a UStaticMesh from AssetPath, falling back to
	 * /Engine/BasicShapes/Cube with a Warning log if the path is empty or
	 * fails to resolve. Guaranteed non-null unless the engine cube is
	 * missing (very unusual).
	 */
	static class UStaticMesh* LoadStaticMeshOrCube(const FString& AssetPath);

	/**
	 * Load a UMaterialInterface from AssetPath. Returns nullptr for empty
	 * / invalid paths (callers should skip material assignment in that case).
	 */
	static class UMaterialInterface* LoadMaterialOrNull(const FString& AssetPath);

	/**
	 * Spawn a plain AActor to serve as an attach root at Location and
	 * assign RootLabel. Returns the new root or nullptr on failure.
	 */
	static AActor* SpawnRootActor(const FString& RootLabel, FVector Location);

	/**
	 * Spawn a AStaticMeshActor at Center, assign Mesh + Material and scale
	 * the cube (default 100cm) to match BoxSize. Registers Modify() on the
	 * new actor. Returns nullptr on failure.
	 */
	static class AStaticMeshActor* SpawnBoxInternal(const FString& Label, FVector Center, FVector BoxSize, class UStaticMesh* Mesh, class UMaterialInterface* Material);
};
