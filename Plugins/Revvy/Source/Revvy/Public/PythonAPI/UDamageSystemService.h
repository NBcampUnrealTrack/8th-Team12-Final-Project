// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UDamageSystemService.generated.h"

/**
 * Damage System service exposed directly to Python for runtime damage
 * dispatch to actors in the current editor world (typically PIE).
 *
 * Mirrors the UCameraShakeService pattern: all UFUNCTIONs are static
 * and BlueprintCallable so they are reachable from Python as
 * `unreal.DamageSystemService.<snake_case_name>(...)`.
 *
 * This is a runtime state service — no FScopedTransaction is required
 * because damage events are transient gameplay effects (not persisted
 * asset / actor state).
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.DamageSystemService
 *
 *   # Generic damage
 *   svc.apply_damage("Enemy_01", 25.0, "PC0", "Sword",
 *                    "/Script/Engine.DamageType")
 *
 *   # Point damage (impact)
 *   svc.apply_point_damage("Enemy_01", 40.0,
 *                          unreal.Vector(1, 0, 0),
 *                          unreal.Vector(100, 200, 90),
 *                          "PC0", "Rifle",
 *                          "/Script/Engine.DamageType")
 *
 *   # Radial damage (AoE)
 *   svc.apply_radial_damage(unreal.Vector(0, 0, 100), 60.0, 500.0,
 *                           ["PC0"], "PC0", "Grenade",
 *                           "/Script/Engine.DamageType", True)
 *
 *   # Best-effort reflection query
 *   hp = svc.get_actor_health("Enemy_01")
 *
 * @note 4 damage operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UDamageSystemService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Apply
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Apply generic damage to a target actor via UGameplayStatics::ApplyDamage.
	 *
	 * @param TargetActorNameOrPath        Actor label / name / path of the target.
	 * @param BaseDamage                   Base damage amount to apply.
	 * @param InstigatorActorNameOrPath    Actor label / name / path of the
	 *                                     instigator (Pawn). Empty = null.
	 * @param DamageCauserActorNameOrPath  Actor label / name / path of the
	 *                                     damage causer (e.g. weapon). Empty = null.
	 * @param DamageTypeClassPath          Full path to a UDamageType subclass
	 *                                     (fallback to UDamageType::StaticClass()
	 *                                     when empty or unresolvable).
	 * @return Actual damage taken by the target (0 if target invalid).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Damage|Apply")
	static float ApplyDamage(
		const FString& TargetActorNameOrPath,
		float BaseDamage,
		const FString& InstigatorActorNameOrPath,
		const FString& DamageCauserActorNameOrPath,
		const FString& DamageTypeClassPath);

	/**
	 * Apply point damage (with impact direction & location) to a target actor
	 * via UGameplayStatics::ApplyPointDamage.
	 *
	 * @param TargetActorNameOrPath      Actor label / name / path of the target.
	 * @param BaseDamage                 Base damage amount to apply.
	 * @param HitFromDirection           Direction of the impact (unit vector).
	 * @param HitLocation                World-space location of the impact.
	 * @param InstigatorActorNameOrPath  Actor label / name / path of the
	 *                                   instigator. Empty = null.
	 * @param CauserActorNameOrPath      Actor label / name / path of the
	 *                                   damage causer. Empty = null.
	 * @param DamageTypeClassPath        Full path to a UDamageType subclass.
	 * @return Actual damage taken by the target (0 if target invalid).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Damage|Apply")
	static float ApplyPointDamage(
		const FString& TargetActorNameOrPath,
		float BaseDamage,
		FVector HitFromDirection,
		FVector HitLocation,
		const FString& InstigatorActorNameOrPath,
		const FString& CauserActorNameOrPath,
		const FString& DamageTypeClassPath);

	/**
	 * Apply radial damage from an origin point via
	 * UGameplayStatics::ApplyRadialDamage.
	 *
	 * @param Origin                     World-space origin of the explosion.
	 * @param BaseDamage                 Base damage at the origin.
	 * @param DamageRadius               Radius outside of which no damage is
	 *                                   applied.
	 * @param IgnoreActorNamesOrPaths    Actor labels / names / paths to skip
	 *                                   during the overlap test. Empty entries
	 *                                   or unresolvable entries are dropped.
	 * @param InstigatorActorNameOrPath  Actor label / name / path of the
	 *                                   instigator. Empty = null.
	 * @param CauserActorNameOrPath      Actor label / name / path of the
	 *                                   damage causer. Empty = null.
	 * @param DamageTypeClassPath        Full path to a UDamageType subclass.
	 * @param bDoFullDamage              If true, every actor in the radius takes
	 *                                   BaseDamage (no falloff).
	 * @return True if the dispatch was attempted with a valid world.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Damage|Apply")
	static bool ApplyRadialDamage(
		FVector Origin,
		float BaseDamage,
		float DamageRadius,
		const TArray<FString>& IgnoreActorNamesOrPaths,
		const FString& InstigatorActorNameOrPath,
		const FString& CauserActorNameOrPath,
		const FString& DamageTypeClassPath,
		bool bDoFullDamage);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Best-effort reflection read of a float UPROPERTY named "Health" on the
	 * target actor. Health is game-specific, so this only works when the
	 * actor's class exposes a UPROPERTY(float) named exactly "Health".
	 *
	 * @param ActorNameOrPath  Actor label / name / path of the target.
	 * @return Current Health value, or -1 if the actor or property was
	 *         not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Damage|Query")
	static float GetActorHealth(const FString& ActorNameOrPath);
};
