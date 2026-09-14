// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UIKRetargeterService.generated.h"

/**
 * IK Retargeter service exposed directly to Python for authoring
 * UIKRigDefinition and UIKRetargeter assets, wiring source/target
 * rigs, mapping chains and batch-retargeting animation sequences.
 *
 * Mirrors the ULightingService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.IKRetargeterService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.IKRetargeterService
 *
 *   # 1) Build source & target IK Rigs
 *   svc.create_ik_rig_definition("/Game/Chars/Src/SK_Src_Skeleton",
 *                                "/Game/Retarget/IKRigs/IK_Src")
 *   svc.add_retarget_chain("/Game/Retarget/IKRigs/IK_Src",
 *                          "LeftArm", "upperarm_l", "hand_l", "")
 *
 *   # 2) Build the retargeter linking the two rigs
 *   svc.create_ik_retargeter("/Game/Retarget/IKRigs/IK_Src",
 *                            "/Game/Retarget/IKRigs/IK_Tgt",
 *                            "/Game/Retarget/RTG_SrcToTgt")
 *
 *   # 3) Chain mapping
 *   svc.auto_map_chains("/Game/Retarget/RTG_SrcToTgt", "Fuzzy")
 *   svc.set_chain_mapping("/Game/Retarget/RTG_SrcToTgt",
 *                         "LeftArm", "LeftArm")
 *   print(svc.list_chain_mappings("/Game/Retarget/RTG_SrcToTgt"))
 *
 *   # 4) Retarget animations
 *   svc.run_retarget_on_sequence("/Game/Retarget/RTG_SrcToTgt",
 *                                "/Game/Chars/Src/A_Run",
 *                                "/Game/Retargeted/A_Run_RT")
 *   svc.batch_retarget_sequences("/Game/Retarget/RTG_SrcToTgt",
 *                                ["/Game/Chars/Src/A_Idle",
 *                                 "/Game/Chars/Src/A_Run"],
 *                                "/Game/Retargeted",
 *                                "_RT")
 *
 * @note All 10 IK-retarget operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UIKRetargeterService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// IK Rig Definition Authoring
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a UIKRigDefinition asset seeded from a USkeleton.
	 *
	 * @param SkeletonAssetPath - Package path to the source USkeleton
	 * @param OutputAssetPath   - Full package path for the new IK rig
	 *                            (e.g. "/Game/Retarget/IKRigs/IK_MyRig")
	 * @return OutputAssetPath on success, empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Rig")
	static FString CreateIKRigDefinition(const FString& SkeletonAssetPath, const FString& OutputAssetPath);

	/**
	 * Add a retarget chain to an existing UIKRigDefinition.
	 *
	 * @param RigAssetPath       - Package path to the UIKRigDefinition
	 * @param ChainName          - Chain name (uniquified on collision)
	 * @param StartBone          - Bone name at the chain root
	 * @param EndBone            - Bone name at the chain tip
	 * @param GoalBoneOptional   - Optional IK goal name; empty for FK-only
	 * @return true if the chain was added and asset saved
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Rig")
	static bool AddRetargetChain(const FString& RigAssetPath, const FString& ChainName, const FString& StartBone, const FString& EndBone, const FString& GoalBoneOptional);

	// ═══════════════════════════════════════════════════════════════════
	// IK Retargeter Authoring
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a UIKRetargeter asset wired to source + target UIKRigDefinitions.
	 *
	 * @param SourceRigAssetPath - Package path to the source UIKRigDefinition
	 * @param TargetRigAssetPath - Package path to the target UIKRigDefinition
	 * @param OutputAssetPath    - Full package path for the new retargeter
	 * @return OutputAssetPath on success, empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Retargeter")
	static FString CreateIKRetargeter(const FString& SourceRigAssetPath, const FString& TargetRigAssetPath, const FString& OutputAssetPath);

	/**
	 * Replace the source UIKRigDefinition on an existing UIKRetargeter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Retargeter")
	static bool SetSourceIKRig(const FString& RetargeterAssetPath, const FString& SourceRigAssetPath);

	/**
	 * Replace the target UIKRigDefinition on an existing UIKRetargeter.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Retargeter")
	static bool SetTargetIKRig(const FString& RetargeterAssetPath, const FString& TargetRigAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Chain Mapping
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List chain mappings on a UIKRetargeter as a JSON array string.
	 *
	 * @param RetargeterAssetPath - Package path to the UIKRetargeter
	 * @return JSON array [{"source_chain","target_chain"}, ...] or "[]" on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Chains")
	static FString ListChainMappings(const FString& RetargeterAssetPath);

	/**
	 * Bind a source chain to a target chain on the retargeter.
	 *
	 * @param RetargeterAssetPath - Package path to the UIKRetargeter
	 * @param SourceChainName     - Chain name on the source IK rig
	 * @param TargetChainName     - Chain name on the target IK rig
	 * @return true if the mapping was applied and asset saved
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Chains")
	static bool SetChainMapping(const FString& RetargeterAssetPath, const FString& SourceChainName, const FString& TargetChainName);

	/**
	 * Auto-map chains using a strategy.
	 *
	 * @param RetargeterAssetPath - Package path to the UIKRetargeter
	 * @param StrategyStr         - "Fuzzy" | "Exact" | "Clear" (case-insensitive)
	 * @return true if auto-mapping ran and asset was saved
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Chains")
	static bool AutoMapChains(const FString& RetargeterAssetPath, const FString& StrategyStr);

	// ═══════════════════════════════════════════════════════════════════
	// Retarget Execution
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Retarget a single UAnimSequence through a UIKRetargeter.
	 *
	 * @param RetargeterAssetPath  - Package path to the UIKRetargeter
	 * @param SourceAnimAssetPath  - Package path to the source UAnimSequence
	 * @param OutputAssetPath      - Full package path for the retargeted anim
	 * @return Package path of the created anim on success, empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Run")
	static FString RunRetargetOnSequence(const FString& RetargeterAssetPath, const FString& SourceAnimAssetPath, const FString& OutputAssetPath);

	/**
	 * Batch-retarget a list of UAnimSequence assets into an output folder.
	 *
	 * @param RetargeterAssetPath - Package path to the UIKRetargeter
	 * @param SourceAnimPaths     - Package paths of the source UAnimSequence assets
	 * @param OutputFolder        - Folder for the retargeted assets (e.g. "/Game/Retargeted")
	 * @param OutputSuffix        - Suffix appended to each duplicated anim (e.g. "_RT")
	 * @return Package paths of the created retargeted animations
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|IKRetarget|Run")
	static TArray<FString> BatchRetargetSequences(const FString& RetargeterAssetPath, const TArray<FString>& SourceAnimPaths, const FString& OutputFolder, const FString& OutputSuffix);
};
