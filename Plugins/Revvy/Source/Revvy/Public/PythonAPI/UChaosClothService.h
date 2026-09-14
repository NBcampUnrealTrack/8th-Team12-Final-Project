// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UChaosClothService.generated.h"

/**
 * Chaos Cloth authoring service exposed directly to Python for inspecting and
 * editing skeletal mesh cloth simulation bindings and per-asset Chaos cloth
 * configuration.
 *
 * Mirrors the UActorService / ULightingService / UPhysicsAssetService pattern:
 * all UFUNCTIONs are static and BlueprintCallable so they are reachable from
 * Python as `unreal.ChaosClothService.<snake_case_name>(...)`.
 *
 * UE 5.8 API notes:
 *  - Skeletal mesh cloth assets live in USkeletalMesh::MeshClothingAssets and
 *    are queried via USkeletalMesh::GetMeshClothingAssets(). Registration uses
 *    USkeletalMesh::AddClothingAsset(UClothingAssetBase*).
 *  - Section-to-asset binding is performed by
 *    UClothingAssetCommon::BindToSkeletalMesh / UnbindFromSkeletalMesh
 *    (WITH_EDITOR only).
 *  - Per-asset simulation config lives in UClothingAssetCommon::ClothConfigs
 *    (TMap keyed by class FName). Chaos config is UChaosClothConfig with fields
 *    like MassMode, TotalMass, Density, BendingStiffnessWeighted (FChaosClothWeightedValue
 *    Low/High), CollisionThickness, FrictionCoefficient, DampingCoefficient.
 *  - Runtime interaction (post-cook) uses UChaosClothingSimulationInteractor,
 *    not covered by this authoring service.
 *  - The newer UChaosClothAsset (ChaosClothAsset plugin) is a separate consolidated
 *    asset type and is not targeted here; this service focuses on the widely
 *    supported UClothingAssetCommon path used by regular USkeletalMesh cloth.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.ChaosClothService
 *
 *   # Query
 *   print(svc.list_skel_mesh_clothing_assets("/Game/Chars/Duck/SK_Duck_Cloak"))
 *   print(svc.get_cloth_config_info("/Game/Chars/Duck/CA_Duck_Cloak"))
 *
 *   # Binding
 *   svc.add_clothing_simulation_to_section(
 *       "/Game/Chars/Duck/SK_Duck_Cloak", 0, 1,
 *       "/Game/Chars/Duck/CA_Duck_Cloak")
 *   svc.remove_clothing_from_section("/Game/Chars/Duck/SK_Duck_Cloak", 0, 1)
 *
 *   # Physics / Config tuning (Chaos)
 *   svc.set_cloth_physics_mass("/Game/Chars/Duck/CA_Duck_Cloak", 0.5)
 *   svc.set_cloth_bending_stiffness("/Game/Chars/Duck/CA_Duck_Cloak", 0.8)
 *   svc.set_cloth_collision_thickness("/Game/Chars/Duck/CA_Duck_Cloak", 1.5)
 *
 * @note 7 cloth authoring operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UChaosClothService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all UClothingAssetBase entries registered on a USkeletalMesh's
	 * MeshClothingAssets array.
	 *
	 * @param SkelMeshAssetPath - USkeletalMesh asset path (e.g. "/Game/Chars/Duck/SK_Duck")
	 * @return JSON array [{"name":..., "class":..., "path":..., "num_lods":..., "reference_bone":...}, ...]
	 *         Empty JSON array on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Query")
	static FString ListSkelMeshClothingAssets(const FString& SkelMeshAssetPath);

	/**
	 * Serialize the Chaos cloth simulation config on a UClothingAssetCommon into a
	 * JSON blob covering the most commonly tuned physics + material parameters
	 * (mass, bending / edge stiffness, collision thickness, damping, drag).
	 *
	 * @param ClothAssetPath - UClothingAssetCommon asset path
	 * @return JSON object with numeric fields. Empty JSON object on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Query")
	static FString GetClothConfigInfo(const FString& ClothAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Assignment / Binding
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Add a clothing asset to the skeletal mesh (registering it in MeshClothingAssets
	 * if needed) and bind it to the given LOD + Section, driving that section's render
	 * geometry from the cloth sim.
	 *
	 * @param SkelMeshAssetPath - USkeletalMesh asset path
	 * @param LODIndex          - Skeletal mesh LOD index
	 * @param SectionIndex      - Skeletal mesh section index within that LOD
	 * @param ClothAssetPath    - UClothingAssetCommon asset path
	 * @return true on success, false on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Assign")
	static bool AddClothingSimulationToSection(
		const FString& SkelMeshAssetPath,
		int32 LODIndex,
		int32 SectionIndex,
		const FString& ClothAssetPath);

	/**
	 * Unbind whichever UClothingAssetCommon is currently driving the given LOD +
	 * Section on the skeletal mesh.
	 *
	 * @param SkelMeshAssetPath - USkeletalMesh asset path
	 * @param LODIndex          - Skeletal mesh LOD index
	 * @param SectionIndex      - Skeletal mesh section index within that LOD
	 * @return true on success, false on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Assign")
	static bool RemoveClothingFromSection(
		const FString& SkelMeshAssetPath,
		int32 LODIndex,
		int32 SectionIndex);

	// ═══════════════════════════════════════════════════════════════════
	// Physics
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the total cloth mass on a UClothingAssetCommon's Chaos config.
	 * Forces MassMode = TotalMass so the value takes effect.
	 *
	 * @param ClothAssetPath - UClothingAssetCommon asset path
	 * @param Mass           - Total cloth mass (kg). Range roughly 0.001..10.
	 * @return true on success, false on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Physics")
	static bool SetClothPhysicsMass(const FString& ClothAssetPath, float Mass);

	// ═══════════════════════════════════════════════════════════════════
	// Config
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set BendingStiffnessWeighted (Low = High = Stiffness) on the Chaos cloth
	 * config of a UClothingAssetCommon. Range [0..1] where 1 is fully stiff.
	 *
	 * @param ClothAssetPath - UClothingAssetCommon asset path
	 * @param Stiffness      - Bending stiffness value, clamped to [0, 1]
	 * @return true on success, false on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Config")
	static bool SetClothBendingStiffness(const FString& ClothAssetPath, float Stiffness);

	/**
	 * Set the CollisionThickness on the Chaos cloth config of a UClothingAssetCommon
	 * (added thickness in cm applied to collision shapes).
	 *
	 * @param ClothAssetPath - UClothingAssetCommon asset path
	 * @param Thickness      - Added collision shape thickness (cm), clamped to [0, 1000]
	 * @return true on success, false on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Cloth|Config")
	static bool SetClothCollisionThickness(const FString& ClothAssetPath, float Thickness);
};
