// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UMotionMatchingService.generated.h"

/**
 * Motion Matching / Pose Search Database authoring service exposed directly to
 * Python for creating and editing UPoseSearchDatabase assets (UE 5.4+ Motion
 * Matching pipeline). All UFUNCTIONs are static and BlueprintCallable so they
 * are reachable from Python as
 *   `unreal.MotionMatchingService.<snake_case_name>(...)`.
 *
 * Mirrors the UChooserService / UActorService / ULightingService pattern:
 * CreatePackage + NewObject asset authoring wrapped in FScopedTransaction.
 *
 * UE 5.8 API notes:
 *  - `UPoseSearchDatabase` (in the PoseSearch plugin, Runtime module) derives
 *    from `UDataAsset` and holds a `TObjectPtr<const UPoseSearchSchema> Schema`
 *    + private `TArray<FPoseSearchDatabaseAnimationAsset> DatabaseAnimationAssets`.
 *  - Rows are added via editor-only `UPoseSearchDatabase::AddAnimationAsset(
 *    const FPoseSearchDatabaseAnimationAsset&)`. The struct's `AnimAsset`
 *    field takes any `UObject*` (typically `UAnimSequence`, `UAnimMontage`,
 *    `UAnimComposite`, or `UBlendSpace`).
 *  - Index build is asynchronous and driven by
 *    `UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex`
 *    with `ERequestAsyncBuildFlag::NewRequest | WaitForCompletion` for
 *    synchronous behavior from Python.
 *  - The PoseSearch plugin ships with the engine but is NOT enabled by default
 *    (see PoseSearch.uplugin `EnabledByDefault: false`). Callers must enable
 *    "PoseSearch" in the .uproject for these APIs to link and load.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.MotionMatchingService
 *
 *   # 1) Create the database referencing an existing schema
 *   db = svc.create_pose_search_database(
 *       "/Game/MotionMatching/MMDB_Locomotion",
 *       "/Game/MotionMatching/MMS_Locomotion")
 *
 *   # 2) Register anim sequences
 *   svc.add_anim_to_pose_search_db(db, "/Game/Anim/Idle")
 *   svc.add_anim_to_pose_search_db(db, "/Game/Anim/Run_F")
 *
 *   # 3) Build the derived data (blocking)
 *   svc.build_pose_search_db(db)
 *
 *   # 4) Inspect
 *   info = svc.get_pose_search_db_stats(db)  # JSON
 *
 * @note 4 Pose Search Database operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UMotionMatchingService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new UPoseSearchDatabase asset at the given content path.
	 *
	 * If SchemaAssetPath is non-empty, the referenced UPoseSearchSchema is
	 * loaded and assigned to `Database->Schema`. The database is created
	 * empty (0 animation assets) — populate it via AddAnimToPoseSearchDb
	 * and finalize with BuildPoseSearchDb.
	 *
	 * @param OutputAssetPath  - Content path for the new asset,
	 *                            e.g. "/Game/MotionMatching/MMDB_Locomotion".
	 * @param SchemaAssetPath  - Content path of an existing UPoseSearchSchema
	 *                            asset, e.g. "/Game/MotionMatching/MMS_Locomotion".
	 *                            May be empty (Schema left null; DB not indexable
	 *                            until a schema is later assigned).
	 * @return Package path of the created asset, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MotionMatching|Create")
	static FString CreatePoseSearchDatabase(
		const FString& OutputAssetPath,
		const FString& SchemaAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Author
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append an animation asset (UAnimSequence / UAnimMontage / UAnimComposite /
	 * UBlendSpace) as a new `FPoseSearchDatabaseAnimationAsset` row on the
	 * database's `DatabaseAnimationAssets` array.
	 *
	 * @param DatabasePath      - Content path of the UPoseSearchDatabase asset.
	 * @param AnimSequencePath  - Content path of the animation asset to add.
	 * @return True on success, false if either asset failed to load or the
	 *          PoseSearch plugin is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MotionMatching|Author")
	static bool AddAnimToPoseSearchDb(
		const FString& DatabasePath,
		const FString& AnimSequencePath);

	/**
	 * Trigger a synchronous derived-data build for the given database via
	 * `FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex` with
	 * `NewRequest | WaitForCompletion` so the call returns only after the
	 * search index is finalized or has failed.
	 *
	 * @param DatabasePath - Content path of the UPoseSearchDatabase asset.
	 * @return True if the build reported Success, false otherwise (failure,
	 *          load error, or plugin unavailable).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MotionMatching|Author")
	static bool BuildPoseSearchDb(const FString& DatabasePath);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a JSON object summarizing the database structure:
	 *
	 *   {
	 *     "database_path":     "<content path>",
	 *     "schema_path":       "<schema asset path or empty>",
	 *     "anim_asset_count":  <int>,
	 *     "pose_count":        <int>,
	 *     "is_built":          <bool>,
	 *     "pose_search_mode":  "<BruteForce|PCAKDTree|VPTree|EventOnly>"
	 *   }
	 *
	 * @param DatabasePath - Content path of the UPoseSearchDatabase asset.
	 * @return JSON object string, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MotionMatching|Query")
	static FString GetPoseSearchDbStats(const FString& DatabasePath);
};
