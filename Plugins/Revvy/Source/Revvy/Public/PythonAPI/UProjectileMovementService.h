// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UProjectileMovementService.generated.h"

/**
 * Runtime projectile spawn + control service exposed directly to Python.
 *
 * Wraps world->SpawnActor for actor classes that host a
 * UProjectileMovementComponent (bullets, arrows, thrown grenades, homing
 * missiles) and exposes velocity / homing / stop controls on the resulting
 * component. No editor transaction is created — this is a *runtime* spawn
 * mirror of UNiagaraComponentService.
 *
 * Distinct from any asset-side projectile authoring: this touches only live
 * actors and their component state in the current editor world.
 *
 * All UFUNCTIONs are static and BlueprintCallable so they are reachable from
 * Python as `unreal.ProjectileMovementService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.ProjectileMovementService
 *
 *   # Spawn a projectile actor blueprint at a location, firing along Direction
 *   label = svc.spawn_projectile(
 *       "/Game/Weapons/BP_ArrowProjectile",
 *       unreal.Vector(0, 0, 100),
 *       unreal.Vector(1, 0, 0),
 *       3000.0,
 *       0.25)
 *
 *   # Turn the projectile into a homing missile targeting an actor
 *   svc.set_projectile_homing_target(label, "Hero")
 *
 *   # Retarget velocity mid-flight
 *   svc.set_projectile_velocity(label, unreal.Vector(0, 5000, 0))
 *
 *   # Kill movement
 *   svc.stop_projectile_movement(label)
 */
UCLASS(BlueprintType)
class REVVY_API UProjectileMovementService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Spawn
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn an actor of the given class at Location with its rotation aligned
	 * to Direction, then configure its UProjectileMovementComponent (if any)
	 * with the requested initial speed, velocity, and gravity scale.
	 *
	 * @param ProjectileClassPath - Package path of an AActor UClass or a
	 *                              Blueprint (e.g. "/Game/Weapons/BP_Arrow").
	 * @param Location            - World-space spawn location.
	 * @param Direction           - World-space direction the projectile faces
	 *                              and moves along. Not required to be normalized.
	 * @param InitialSpeed        - Initial speed applied to the component's
	 *                              InitialSpeed / MaxSpeed / Velocity.
	 * @param GravityScale        - Value written to ProjectileGravityScale
	 *                              (0.0 = flat trajectory, 1.0 = full gravity).
	 * @return Actor label of the spawned actor, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Projectile|Spawn")
	static FString SpawnProjectile(
		const FString& ProjectileClassPath,
		FVector Location,
		FVector Direction,
		float InitialSpeed,
		float GravityScale);

	// ═══════════════════════════════════════════════════════════════════
	// Homing
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enable homing on the projectile actor's UProjectileMovementComponent and
	 * bind its HomingTargetComponent to the target actor's root component.
	 *
	 * @param ActorNameOrPath        - Label / name / soft-object-path of the
	 *                                 projectile actor.
	 * @param TargetActorNameOrPath  - Label / name / soft-object-path of the
	 *                                 actor the projectile should home in on.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Projectile|Homing")
	static bool SetProjectileHomingTarget(
		const FString& ActorNameOrPath,
		const FString& TargetActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Control
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Overwrite the current Velocity on the projectile actor's
	 * UProjectileMovementComponent.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @param NewVelocity     - New world-space velocity (cm/s).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Projectile|Control")
	static bool SetProjectileVelocity(
		const FString& ActorNameOrPath,
		FVector NewVelocity);

	/**
	 * Stop projectile movement immediately: zeroes velocity and deactivates
	 * the UProjectileMovementComponent.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Projectile|Control")
	static bool StopProjectileMovement(const FString& ActorNameOrPath);
};
