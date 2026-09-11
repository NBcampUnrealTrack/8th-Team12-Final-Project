// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UCharacterMovementService.generated.h"

/**
 * Runtime character-movement tuning service exposed directly to Python.
 *
 * Targets ACharacter actors already present in the current editor world and
 * mutates their owned UCharacterMovementComponent (walking speed, jump impulse,
 * gravity scale, crouched half-height, etc.). No editor transaction is created
 * — this is a *runtime* mirror of the projectile / niagara service pattern.
 *
 * Distinct from asset-side character authoring: this touches only live actors
 * and their component state in the current editor world.
 *
 * All UFUNCTIONs are static and BlueprintCallable so they are reachable from
 * Python as `unreal.CharacterMovementService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.CharacterMovementService
 *
 *   # Retune the hero character's ground speed
 *   svc.set_max_walk_speed("Hero", 800.0)
 *
 *   # Bump up jump impulse and lower gravity for floaty jumps
 *   svc.set_jump_z_velocity("Hero", 900.0)
 *   svc.set_gravity_scale("Hero", 0.75)
 *
 *   # Crouch height tweak
 *   svc.set_crouched_half_height("Hero", 40.0)
 *
 *   # Dump current movement state as JSON
 *   info = svc.get_character_movement_info("Hero")
 */
UCLASS(BlueprintType)
class REVVY_API UCharacterMovementService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Speed
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set MaxWalkSpeed on the target character's UCharacterMovementComponent.
	 *
	 * @param CharacterActorNameOrPath - Label / name / soft-object-path of an
	 *                                   ACharacter actor in the editor world.
	 * @param MaxWalkSpeed             - New maximum walk speed (cm/s).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CharacterMovement|Speed")
	static bool SetMaxWalkSpeed(
		const FString& CharacterActorNameOrPath,
		float MaxWalkSpeed);

	// ═══════════════════════════════════════════════════════════════════
	// Jump
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set JumpZVelocity on the target character's UCharacterMovementComponent.
	 *
	 * @param CharacterActorNameOrPath - Label / name / soft-object-path of an
	 *                                   ACharacter actor in the editor world.
	 * @param JumpZVelocity            - New jump impulse (cm/s along +Z).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CharacterMovement|Jump")
	static bool SetJumpZVelocity(
		const FString& CharacterActorNameOrPath,
		float JumpZVelocity);

	// ═══════════════════════════════════════════════════════════════════
	// Gravity
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set GravityScale on the target character's UCharacterMovementComponent.
	 *
	 * @param CharacterActorNameOrPath - Label / name / soft-object-path of an
	 *                                   ACharacter actor in the editor world.
	 * @param GravityScale             - Multiplier applied to world gravity
	 *                                   (0.0 = float, 1.0 = normal, >1 = heavy).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CharacterMovement|Gravity")
	static bool SetGravityScale(
		const FString& CharacterActorNameOrPath,
		float GravityScale);

	// ═══════════════════════════════════════════════════════════════════
	// Crouch
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set the crouched capsule half-height on the target character's
	 * UCharacterMovementComponent.
	 *
	 * @param CharacterActorNameOrPath - Label / name / soft-object-path of an
	 *                                   ACharacter actor in the editor world.
	 * @param HalfHeight               - New crouched capsule half-height (cm).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CharacterMovement|Crouch")
	static bool SetCrouchedHalfHeight(
		const FString& CharacterActorNameOrPath,
		float HalfHeight);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Read the current state of the character's UCharacterMovementComponent
	 * and return it as a JSON string.
	 *
	 * Fields: max_walk_speed, max_walk_speed_crouched, jump_z_velocity,
	 *         gravity_scale, movement_mode, is_falling, is_crouching,
	 *         current_velocity:{x,y,z}.
	 *
	 * @param CharacterActorNameOrPath - Label / name / soft-object-path of an
	 *                                   ACharacter actor in the editor world.
	 * @return JSON string describing the movement state, or "{}" on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CharacterMovement|Query")
	static FString GetCharacterMovementInfo(
		const FString& CharacterActorNameOrPath);
};
