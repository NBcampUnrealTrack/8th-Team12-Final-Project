// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPlayerControllerService.generated.h"

/**
 * Player Controller service exposed directly to Python for tiny runtime
 * possession / input helpers on APlayerController.
 *
 * Mirrors the UCameraShakeService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.PlayerControllerService.<snake_case_name>(...)`.
 *
 * This is a runtime state service — no FScopedTransaction is required
 * because possession / input toggles are transient runtime state.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.PlayerControllerService
 *
 *   svc.possess_pawn("PC0", "MyPawn")
 *   label = svc.get_controlled_pawn("PC0")
 *   svc.enable_player_input("PC0", False)
 *   svc.unpossess_pawn("PC0")
 *
 * @note All 4 player-controller helpers available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UPlayerControllerService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Possession
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Possess the target Pawn with the target PlayerController.
	 *
	 * @param PlayerControllerActorNameOrPath  Actor label / name / path of
	 *                                         the target APlayerController.
	 * @param PawnActorNameOrPath              Actor label / name / path of
	 *                                         the target APawn.
	 * @return True if PC and Pawn were resolved and Possess() was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerController|Possess")
	static bool PossessPawn(
		const FString& PlayerControllerActorNameOrPath,
		const FString& PawnActorNameOrPath);

	/**
	 * Unpossess whatever pawn the target PlayerController currently controls.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return True if PC was resolved and UnPossess() was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerController|Possess")
	static bool UnpossessPawn(const FString& PlayerControllerActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Get the actor label of the pawn currently controlled by the target PC.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return Actor label of the possessed pawn, or empty string if none / PC missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerController|Query")
	static FString GetControlledPawn(const FString& PlayerControllerActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Input
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enable or disable input on the target PlayerController.
	 * When bEnable is true, calls PC->EnableInput(PC); when false,
	 * calls PC->DisableInput(PC).
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param bEnable                          True to enable input, false to disable.
	 * @return True if PC was resolved and the toggle was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PlayerController|Input")
	static bool EnablePlayerInput(
		const FString& PlayerControllerActorNameOrPath,
		bool bEnable);
};
