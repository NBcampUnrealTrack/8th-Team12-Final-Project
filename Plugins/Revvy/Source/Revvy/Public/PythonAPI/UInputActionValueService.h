// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UInputActionValueService.generated.h"

/**
 * Enhanced Input runtime state-query service exposed directly to Python.
 *
 * Mirrors the UCameraShakeService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.InputActionValueService.<snake_case_name>(...)`.
 *
 * This is a *runtime* query service — no FScopedTransaction is used
 * because we only read live player input state (not asset data).
 * The target APlayerController is resolved via label / name / soft-path
 * lookup, or falls back to the first PC in the current world (PIE
 * preferred if available).
 *
 * Backing UE 5.8 API:
 *   UEnhancedInputLocalPlayerSubsystem (via PC->GetLocalPlayer())
 *     -> GetPlayerInput() : UEnhancedPlayerInput*
 *          -> GetActionValue(UInputAction*)          : FInputActionValue
 *          -> FindActionInstanceData(UInputAction*)  : const FInputActionInstance*
 *                 -> GetTriggerEvent()  : ETriggerEvent
 *                 -> GetElapsedTime()   : float
 *          -> GetAppliedInputContextData()           : TMap<IMC*, priority>
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.InputActionValueService
 *
 *   raw = svc.get_input_action_value("PC0", "/Game/Input/IA_Move.IA_Move")
 *   value = json.loads(raw)
 *   # -> {"value_type": "Axis2D", "bool_value": false,
 *   #     "axis1d": 0.0, "axis2d": {"x": 1.0, "y": 0.0},
 *   #     "axis3d": {"x": 0.0, "y": 0.0, "z": 0.0}}
 *
 *   triggered = svc.is_input_action_triggered("PC0", "/Game/Input/IA_Jump.IA_Jump")
 *
 *   contexts = json.loads(svc.list_active_mapping_contexts("PC0"))
 *   # -> [{"context_path": "/Game/Input/IMC_Default.IMC_Default", "priority": 0}]
 *
 *   elapsed = svc.get_input_action_elapsed_trigger_time("PC0",
 *       "/Game/Input/IA_Hold.IA_Hold")
 *
 * @note All 4 runtime action-value queries available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UInputActionValueService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Query the current FInputActionValue for a UInputAction on the
	 * target APlayerController's UEnhancedPlayerInput.
	 *
	 * @param PlayerControllerActorNameOrPath  Actor label / name / path of
	 *                                         the target APlayerController.
	 * @param InputActionAssetPath             Full path to the UInputAction
	 *                                         asset (e.g. "/Game/Input/IA_Move.IA_Move").
	 * @return JSON object with fields:
	 *         value_type ("Boolean"|"Axis1D"|"Axis2D"|"Axis3D"|"Unknown"),
	 *         bool_value, axis1d, axis2d:{x,y}, axis3d:{x,y,z}.
	 *         Returns a JSON object with a "note" field on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputActionValue|Query")
	static FString GetInputActionValue(
		const FString& PlayerControllerActorNameOrPath,
		const FString& InputActionAssetPath);

	/**
	 * True iff the given UInputAction is currently in the ETriggerEvent::Triggered
	 * state on the target APlayerController.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param InputActionAssetPath             UInputAction asset path.
	 * @return True if the action's current trigger event is Triggered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputActionValue|Query")
	static bool IsInputActionTriggered(
		const FString& PlayerControllerActorNameOrPath,
		const FString& InputActionAssetPath);

	/**
	 * List every UInputMappingContext currently applied to the target
	 * APlayerController's UEnhancedInputLocalPlayerSubsystem, together
	 * with the priority under which it was added.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return JSON array of {context_path, priority}. Empty array on
	 *         success with no contexts, or a JSON object with a "note"
	 *         field on early error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputActionValue|Query")
	static FString ListActiveMappingContexts(
		const FString& PlayerControllerActorNameOrPath);

	/**
	 * Report the elapsed evaluation time (in seconds) for a UInputAction
	 * on the target APlayerController — i.e. how long the action has been
	 * in the Started / Ongoing / Triggered chain.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param InputActionAssetPath             UInputAction asset path.
	 * @return Elapsed time in seconds; -1.0f when the action is not
	 *         active or any pointer in the chain is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputActionValue|Query")
	static float GetInputActionElapsedTriggerTime(
		const FString& PlayerControllerActorNameOrPath,
		const FString& InputActionAssetPath);
};
