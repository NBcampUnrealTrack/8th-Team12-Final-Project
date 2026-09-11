// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Transform.h"
#include "UGeometryScriptService.generated.h"

class UDynamicMesh;

/**
 * GeometryScript service exposed directly to Python for procedural mesh
 * authoring via UE 5.4+ GeometryScripting plugin.
 *
 * Mirrors the ULightingService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.GeometryScriptService.<snake_case_name>(...)`.
 *
 * State model:
 *   UDynamicMesh instances are transient runtime objects. This service
 *   keeps a static handle-keyed registry so Python callers can chain
 *   primitive appends and boolean operations across multiple calls
 *   without owning a UObject reference on the Python side.
 *
 * Threading:
 *   All operations must be issued from the GameThread. Registry access
 *   is not protected by a lock; violations are logged.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.GeometryScriptService
 *
 *   # Lifecycle
 *   handle = svc.create_dynamic_mesh("MyMesh")
 *
 *   # Primitives
 *   xform = unreal.Transform()
 *   svc.append_box(handle, xform, unreal.Vector(100, 100, 100), 1, 1, 1)
 *   svc.append_sphere(handle, xform, 50.0, 16, 16)
 *
 *   # Boolean
 *   other = svc.create_dynamic_mesh("Cutter")
 *   svc.append_sphere(other, xform, 60.0, 16, 16)
 *   svc.boolean_subtract(handle, other)
 *
 *   # Export to StaticMesh asset
 *   svc.convert_dynamic_mesh_to_static_mesh(handle, "/Game/Meshes/SM_Generated")
 *
 *   # Cleanup
 *   svc.dispose_dynamic_mesh(handle)
 *   svc.dispose_dynamic_mesh(other)
 */
UCLASS(BlueprintType)
class REVVY_API UGeometryScriptService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new transient UDynamicMesh and register it under the given handle.
	 *
	 * @param Handle - Caller-provided string key to reference the mesh in later calls.
	 * @return The handle on success, or empty string if creation failed / handle collided.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Lifecycle")
	static FString CreateDynamicMesh(const FString& Handle);

	/**
	 * Remove the mesh registered under Handle and null out the registry entry.
	 *
	 * @param Handle - Handle previously returned by CreateDynamicMesh.
	 * @return True if a mesh was found and disposed, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Lifecycle")
	static bool DisposeDynamicMesh(const FString& Handle);

	// ═══════════════════════════════════════════════════════════════════
	// Primitives
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append a box primitive to the mesh at Handle.
	 *
	 * @param Handle     - Target mesh handle.
	 * @param Transform  - Placement transform for the primitive.
	 * @param Dimensions - Box extents (X = width, Y = depth, Z = height) in world units.
	 * @param StepsX     - Number of subdivision steps along the X axis.
	 * @param StepsY     - Number of subdivision steps along the Y axis.
	 * @param StepsZ     - Number of subdivision steps along the Z axis.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Primitives")
	static bool AppendBox(
		const FString& Handle,
		FTransform Transform,
		FVector Dimensions,
		int32 StepsX,
		int32 StepsY,
		int32 StepsZ);

	/**
	 * Append a lat/long sphere primitive to the mesh at Handle.
	 *
	 * @param Handle     - Target mesh handle.
	 * @param Transform  - Placement transform for the primitive.
	 * @param Radius     - Sphere radius in world units.
	 * @param StepsPhi   - Number of latitude subdivisions.
	 * @param StepsTheta - Number of longitude subdivisions.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Primitives")
	static bool AppendSphere(
		const FString& Handle,
		FTransform Transform,
		float Radius,
		int32 StepsPhi,
		int32 StepsTheta);

	/**
	 * Append a cylinder primitive to the mesh at Handle.
	 *
	 * @param Handle       - Target mesh handle.
	 * @param Transform    - Placement transform for the primitive.
	 * @param Radius       - Cylinder radius in world units.
	 * @param Height       - Cylinder height in world units.
	 * @param RadialSteps  - Number of radial subdivisions.
	 * @param HeightSteps  - Number of subdivisions along the height axis.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Primitives")
	static bool AppendCylinder(
		const FString& Handle,
		FTransform Transform,
		float Radius,
		float Height,
		int32 RadialSteps,
		int32 HeightSteps);

	/**
	 * Append a torus or partial torus primitive to the mesh at Handle.
	 *
	 * @param Handle      - Target mesh handle.
	 * @param Transform   - Placement transform for the primitive.
	 * @param MajorRadius - Torus major radius (center of tube to origin).
	 * @param MinorRadius - Torus minor radius (tube thickness).
	 * @param MajorSteps  - Number of subdivisions around the major radius.
	 * @param MinorSteps  - Number of subdivisions around the minor radius.
	 * @param RevolveDegrees - Arc swept around the major radius, in the range (0, 360].
	 * @param DegreeOffset - Finite angular offset applied before the revolve, in degrees.
	 * @param bFillEndCaps - Fill the two open ends when RevolveDegrees is less than 360.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Primitives")
	static bool AppendTorus(
		const FString& Handle,
		FTransform Transform,
		float MajorRadius,
		float MinorRadius,
		int32 MajorSteps,
		int32 MinorSteps,
		float RevolveDegrees = 360.0f,
		float DegreeOffset = 0.0f,
		bool bFillEndCaps = true);

	// ═══════════════════════════════════════════════════════════════════
	// Mesh Import
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Copy geometry from an existing UStaticMesh asset into the mesh at Handle.
	 *
	 * @param Handle              - Target mesh handle.
	 * @param StaticMeshAssetPath - Package path of a UStaticMesh (e.g. "/Game/Meshes/SM_Cube").
	 * @param Transform           - Placement transform to apply on append.
	 * @param LODIndex            - Source LOD to copy (usually 0).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Import")
	static bool AppendMeshFromStaticMesh(
		const FString& Handle,
		const FString& StaticMeshAssetPath,
		FTransform Transform,
		int32 LODIndex);

	// ═══════════════════════════════════════════════════════════════════
	// Boolean
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Perform a boolean UNION of Other into Target. Result is stored in Target.
	 *
	 * @param TargetHandle - Mesh handle receiving the union result.
	 * @param OtherHandle  - Mesh handle providing the additional geometry.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Boolean")
	static bool BooleanUnion(const FString& TargetHandle, const FString& OtherHandle);

	/**
	 * Perform a boolean SUBTRACT of Other from Target. Result is stored in Target.
	 *
	 * @param TargetHandle - Mesh handle receiving the subtracted result.
	 * @param OtherHandle  - Mesh handle acting as the cutter.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Boolean")
	static bool BooleanSubtract(const FString& TargetHandle, const FString& OtherHandle);

	/**
	 * Perform a boolean INTERSECT between Target and Other. Result is stored in Target.
	 *
	 * @param TargetHandle - Mesh handle receiving the intersection result.
	 * @param OtherHandle  - Mesh handle providing the intersection region.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Boolean")
	static bool BooleanIntersect(const FString& TargetHandle, const FString& OtherHandle);

	// ═══════════════════════════════════════════════════════════════════
	// Export
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Convert the mesh at Handle into a UStaticMesh asset at OutputAssetPath.
	 * Creates the asset if it does not exist, replaces contents if it does.
	 * Wrapped in an FScopedTransaction for undo/redo support.
	 *
	 * @param Handle          - Source mesh handle.
	 * @param OutputAssetPath - Package path for the new/updated UStaticMesh (e.g. "/Game/Meshes/SM_Gen").
	 * @return The asset path on success, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GeometryScript|Export")
	static FString ConvertDynamicMeshToStaticMesh(const FString& Handle, const FString& OutputAssetPath);

private:
	/** Handle-keyed registry of transient UDynamicMesh instances. */
	static TMap<FString, TObjectPtr<UDynamicMesh>> MeshRegistry;

	/**
	 * Look up the mesh registered under Handle.
	 * Logs a Warning and returns nullptr if Handle is missing.
	 */
	static UDynamicMesh* FindMesh(const FString& Handle);

	/** Emit a Warning if not on the GameThread. */
	static void WarnIfNotGameThread(const TCHAR* Op);
};
