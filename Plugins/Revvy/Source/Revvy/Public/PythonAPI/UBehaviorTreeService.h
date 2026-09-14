// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UBehaviorTreeService.generated.h"

/**
 * Behavior Tree / Blackboard authoring + runtime service exposed to Python.
 *
 * Mirrors the UAIPerceptionService / UChooserService pattern: all UFUNCTIONs
 * are static + BlueprintCallable so they are reachable from Python as
 * `unreal.BehaviorTreeService.<snake_case_name>(...)`.
 *
 * Asset-mutating operations (CreateBehaviorTree, CreateBlackboardData,
 * AddBlackboardKey) wrap work in an FScopedTransaction so the change is
 * undoable in the editor and shows up in the transaction buffer. Runtime
 * operations (blackboard SetValueAs*, RunBehaviorTree, StopBehaviorTree)
 * intentionally omit the transaction scope — they touch transient runtime
 * state on live UBlackboardComponent / UBehaviorTreeComponent instances
 * and must not be recorded into the undo buffer.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.BehaviorTreeService
 *
 *   # Author
 *   bb = svc.create_blackboard_data("/Game/AI/BB_Enemy")
 *   svc.add_blackboard_key(bb, "TargetActor", "Object")
 *   svc.add_blackboard_key(bb, "IsAlerted",  "Bool")
 *   bt = svc.create_behavior_tree("/Game/AI/BT_Enemy", bb)
 *
 *   # Runtime
 *   svc.set_blackboard_value_as_bool("EnemyAIController0", "IsAlerted", True)
 *   svc.run_behavior_tree("EnemyAIController0", "/Game/AI/BT_Enemy")
 *   svc.stop_behavior_tree("EnemyAIController0")
 */
UCLASS(BlueprintType)
class REVVY_API UBehaviorTreeService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Asset Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new UBehaviorTree asset at the given package path and
	 * optionally bind it to an existing UBlackboardData asset.
	 *
	 * @param OutputAssetPath      Package path e.g. "/Game/AI/BT_Enemy".
	 * @param BlackboardDataPath   Optional path to an existing UBlackboardData asset.
	 * @return Full path of the created BT asset, empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Create")
	static FString CreateBehaviorTree(
		const FString& OutputAssetPath,
		const FString& BlackboardDataPath);

	/**
	 * Create a new empty UBlackboardData asset at the given package path.
	 *
	 * @param OutputAssetPath  Package path e.g. "/Game/AI/BB_Enemy".
	 * @return Full path of the created BB asset, empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Create")
	static FString CreateBlackboardData(const FString& OutputAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Blackboard Schema
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append a typed key to a UBlackboardData asset.
	 *
	 * @param BlackboardDataPath  Path to UBlackboardData asset.
	 * @param KeyName             Entry name (becomes FName).
	 * @param KeyTypeStr          "Object"|"Bool"|"Float"|"Int"|"Vector"|"String"|"Name"|"Enum"|"Class".
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Schema")
	static bool AddBlackboardKey(
		const FString& BlackboardDataPath,
		const FString& KeyName,
		const FString& KeyTypeStr);

	// ═══════════════════════════════════════════════════════════════════
	// Blackboard Runtime Values
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set a boolean blackboard value on the given AI's UBlackboardComponent.
	 *
	 * @param AIControllerActorNameOrPath  Actor label / name / path of the
	 *                                     AAIController OR a pawn that owns one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Runtime")
	static bool SetBlackboardValueAsBool(
		const FString& AIControllerActorNameOrPath,
		const FString& KeyName,
		bool Value);

	/** Set a float blackboard value on the given AI's UBlackboardComponent. */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Runtime")
	static bool SetBlackboardValueAsFloat(
		const FString& AIControllerActorNameOrPath,
		const FString& KeyName,
		float Value);

	/**
	 * Set an object blackboard value on the given AI's UBlackboardComponent.
	 * ObjectPath is resolved via EditorAssetLibrary::LoadAsset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Runtime")
	static bool SetBlackboardValueAsObject(
		const FString& AIControllerActorNameOrPath,
		const FString& KeyName,
		const FString& ObjectPath);

	// ═══════════════════════════════════════════════════════════════════
	// BT Runtime
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Load a UBehaviorTree asset and start it on the target AAIController via
	 * AAIController::RunBehaviorTree.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Runtime")
	static bool RunBehaviorTree(
		const FString& AIControllerActorNameOrPath,
		const FString& BTAssetPath);

	/**
	 * Stop the currently running behavior tree on the target AAIController
	 * by calling UBrainComponent::StopLogic.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BehaviorTree|Runtime")
	static bool StopBehaviorTree(const FString& AIControllerActorNameOrPath);
};
