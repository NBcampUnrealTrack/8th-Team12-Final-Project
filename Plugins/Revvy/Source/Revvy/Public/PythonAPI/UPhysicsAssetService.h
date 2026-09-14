// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "UPhysicsAssetService.generated.h"

/**
 * Physics asset authoring service exposed directly to Python for creating and
 * editing UPhysicsAsset assets, adding/removing collision bodies (sphere,
 * capsule/sphyl, box), configuring per-body mass and collision response, and
 * tuning ragdoll constraint angular / linear motion limits.
 *
 * Mirrors the UActorService / ULightingService / USequencerService pattern:
 * all UFUNCTIONs are static and BlueprintCallable so they are reachable from
 * Python as `unreal.PhysicsAssetService.<snake_case_name>(...)`.
 *
 * UE 5.8 API notes:
 *  - Bodies live in `PhysAsset->SkeletalBodySetups` (USkeletalBodySetup*) and
 *    are created via `FPhysicsAssetUtils::CreateNewBody(PhysAsset, BoneName, Params)`.
 *  - Constraints live in `PhysAsset->ConstraintSetup` (UPhysicsConstraintTemplate*)
 *    and are keyed by child bone via `FPhysicsAssetUtils::CreateNewConstraint`.
 *  - Aggregate geometry (Chaos backend) still uses `FKAggregateGeom` with
 *    `SphereElems`, `SphylElems`, `BoxElems`, `TaperedCapsuleElems`.
 *  - Angular limits in UE 5.0+ use degrees on
 *    `FConstraintInstance::SetAngularSwing1Limit/Swing2Limit/TwistLimit`.
 *  - Auto-generate uses `FPhysicsAssetUtils::CreateFromSkeletalMesh` with
 *    `FPhysAssetCreateParams` (see `EPhysAssetFitGeomType`).
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.PhysicsAssetService
 *
 *   # Lifecycle
 *   pa = svc.create_physics_asset("/Game/Chars/Duck/SK_Duck_Skeleton",
 *                                 "/Game/Chars/Duck/PA_Duck",
 *                                 "/Game/Chars/Duck/SK_Duck")
 *   svc.auto_generate_from_skeletal_mesh("/Game/Chars/Duck/SK_Duck",
 *                                        "/Game/Chars/Duck/PA_Duck_Auto",
 *                                        20.0, 0.75, "Sphyl")
 *
 *   # Body management
 *   svc.list_physics_bodies(pa)
 *   svc.add_sphere_body(pa, "head", 12.0, unreal.Vector(0, 0, 0))
 *   svc.add_sphyl_body(pa, "spine_01", 8.0, 20.0,
 *                      unreal.Vector(0, 0, 0), unreal.Rotator(0, 90, 0))
 *   svc.add_box_body(pa, "pelvis", unreal.Vector(15, 20, 10),
 *                    unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
 *   svc.remove_physics_body(pa, "hand_l")
 *
 *   # Property edit
 *   svc.set_physics_body_mass(pa, "head", 4.5)
 *   svc.set_physics_body_collision_type(pa, "head", "AllQueriesAndPhysics")
 *
 *   # Constraints
 *   svc.list_physics_constraints(pa)
 *   svc.set_constraint_angular_limits(pa, "spine_01", 30.0, 30.0, 15.0)
 *   svc.set_constraint_motion_type(pa, "spine_01", "Swing1", "Limited")
 *
 * @note 12 physics asset operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UPhysicsAssetService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create an empty UPhysicsAsset at the given output path and optionally
	 * bind it to a preview skeletal mesh.
	 *
	 * @param SkeletonAssetPath    - Optional USkeleton asset path (used to derive preview mesh if BindSkeletalMeshPath empty)
	 * @param OutputAssetPath      - Fully qualified asset path for the new PhysicsAsset (e.g. "/Game/Chars/Duck/PA_Duck")
	 * @param BindSkeletalMeshPath - Optional USkeletalMesh asset path; the mesh is set as the PhysAsset preview and adopts the new PhysAsset
	 * @return The created PhysicsAsset object path on success, empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Lifecycle")
	static FString CreatePhysicsAsset(
		const FString& SkeletonAssetPath,
		const FString& OutputAssetPath,
		const FString& BindSkeletalMeshPath);

	/**
	 * Auto-generate a fully populated UPhysicsAsset (bodies + constraints)
	 * from a USkeletalMesh via FPhysicsAssetUtils::CreateFromSkeletalMesh.
	 *
	 * @param SkelMeshAssetPath - USkeletalMesh asset path
	 * @param OutputAssetPath   - Output PhysicsAsset asset path
	 * @param MinBoneSize       - Minimum bone size to generate a body for (cm). Below this, bones are skipped.
	 * @param WeightThreshold   - Vertex weight influence threshold (0..1). Used to bind vertices to bodies.
	 * @param GeomTypeStr       - "Box" | "Sphere" | "Sphyl" | "TaperedCapsule"
	 * @return The created PhysicsAsset object path on success, empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Lifecycle")
	static FString AutoGenerateFromSkeletalMesh(
		const FString& SkelMeshAssetPath,
		const FString& OutputAssetPath,
		float MinBoneSize,
		float WeightThreshold,
		const FString& GeomTypeStr);

	// ═══════════════════════════════════════════════════════════════════
	// Body Management
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all physics bodies in the specified PhysicsAsset.
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @return JSON array of { bone_name, num_sphere, num_box, num_sphyl, num_tapered_capsule, mass, physics_type, collision_response }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Body")
	static FString ListPhysicsBodies(const FString& PhysAssetPath);

	/**
	 * Add a sphere collision primitive to the body for the given bone, creating
	 * the body if it does not yet exist.
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @param BoneName      - Skeleton bone to attach the body to
	 * @param Radius        - Sphere radius (cm)
	 * @param CenterOffset  - Local offset of the sphere from the bone
	 * @return Body index in SkeletalBodySetups on success, -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Body")
	static int32 AddSphereBody(
		const FString& PhysAssetPath,
		const FString& BoneName,
		float Radius,
		FVector CenterOffset);

	/**
	 * Add a capsule (sphyl) collision primitive to the body for the given bone.
	 *
	 * @param PhysAssetPath  - PhysicsAsset asset path
	 * @param BoneName       - Skeleton bone to attach the body to
	 * @param Radius         - Capsule radius (cm)
	 * @param Length         - Capsule cylindrical length excluding hemispheres (cm)
	 * @param CenterOffset   - Local offset of the capsule from the bone
	 * @param RotationOffset - Local rotation of the capsule
	 * @return Body index in SkeletalBodySetups on success, -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Body")
	static int32 AddSphylBody(
		const FString& PhysAssetPath,
		const FString& BoneName,
		float Radius,
		float Length,
		FVector CenterOffset,
		FRotator RotationOffset);

	/**
	 * Add a box collision primitive to the body for the given bone.
	 *
	 * @param PhysAssetPath  - PhysicsAsset asset path
	 * @param BoneName       - Skeleton bone to attach the body to
	 * @param Extent         - Full box extent as (X, Y, Z) in cm
	 * @param CenterOffset   - Local offset of the box from the bone
	 * @param RotationOffset - Local rotation of the box
	 * @return Body index in SkeletalBodySetups on success, -1 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Body")
	static int32 AddBoxBody(
		const FString& PhysAssetPath,
		const FString& BoneName,
		FVector Extent,
		FVector CenterOffset,
		FRotator RotationOffset);

	/**
	 * Destroy the body for the given bone (and its owning constraints).
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @param BoneName      - Bone whose body should be removed
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Body")
	static bool RemovePhysicsBody(const FString& PhysAssetPath, const FString& BoneName);

	// ═══════════════════════════════════════════════════════════════════
	// Property Edit
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Override the mass for the body of the given bone (kg).
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @param BoneName      - Bone whose body mass to override
	 * @param Mass          - New mass in kilograms
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Property")
	static bool SetPhysicsBodyMass(const FString& PhysAssetPath, const FString& BoneName, float Mass);

	/**
	 * Set the collision behaviour on the body for the given bone.
	 *
	 * @param PhysAssetPath    - PhysicsAsset asset path
	 * @param BoneName         - Bone whose body collision to configure
	 * @param CollisionTypeStr - "AllQueriesAndPhysics" | "OnlyPhysics" | "OnlyQueries" | "NoCollision"
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Property")
	static bool SetPhysicsBodyCollisionType(
		const FString& PhysAssetPath,
		const FString& BoneName,
		const FString& CollisionTypeStr);

	// ═══════════════════════════════════════════════════════════════════
	// Constraints
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all ragdoll constraints in the specified PhysicsAsset.
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @return JSON array of { parent_bone, child_bone, swing1_deg, swing2_deg, twist_deg,
	 *         motion_swing1, motion_swing2, motion_twist,
	 *         motion_linear_x, motion_linear_y, motion_linear_z }
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Constraint")
	static FString ListPhysicsConstraints(const FString& PhysAssetPath);

	/**
	 * Set the angular swing/twist limits (degrees) on the constraint keyed by
	 * the given child bone.
	 *
	 * @param PhysAssetPath  - PhysicsAsset asset path
	 * @param ChildBoneName  - Child bone of the constraint (ConstraintBone1)
	 * @param Swing1Deg      - Swing1 (cone) limit in degrees
	 * @param Swing2Deg      - Swing2 (cone) limit in degrees
	 * @param TwistDeg       - Twist limit in degrees
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Constraint")
	static bool SetConstraintAngularLimits(
		const FString& PhysAssetPath,
		const FString& ChildBoneName,
		float Swing1Deg,
		float Swing2Deg,
		float TwistDeg);

	/**
	 * Set the motion type (Free / Limited / Locked) on a single axis of the
	 * constraint keyed by the given child bone.
	 *
	 * @param PhysAssetPath - PhysicsAsset asset path
	 * @param ChildBoneName - Child bone of the constraint
	 * @param AxisStr       - "Swing1" | "Swing2" | "Twist" | "LinearX" | "LinearY" | "LinearZ"
	 * @param MotionStr     - "Free" | "Limited" | "Locked"
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsAsset|Constraint")
	static bool SetConstraintMotionType(
		const FString& PhysAssetPath,
		const FString& ChildBoneName,
		const FString& AxisStr,
		const FString& MotionStr);
};
