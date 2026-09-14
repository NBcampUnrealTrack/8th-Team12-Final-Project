// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UNiagaraComponentService.generated.h"

/**
 * Runtime Niagara component service exposed directly to Python for spawning
 * Niagara VFX systems into the current editor world and binding user-exposed
 * parameters at runtime.
 *
 * Distinct from:
 *   - UNiagaraService           (asset-authoring: parameter definitions, system
 *                                inspection, module edits on the .uasset)
 *   - UNiagaraEmitterService    (emitter mutation on UNiagaraEmitter assets)
 *   - UNiagaraScratchPadService (in-system scratchpad module authoring)
 *
 * This service targets the *runtime component* side: it wraps
 * UNiagaraFunctionLibrary::SpawnSystem* calls and UNiagaraComponent::SetVariable*
 * accessors so Python can spawn one-shot or attached FX, drive user params, and
 * stop live components — no editor transaction is created (runtime spawn).
 *
 * Mirrors the ULightingService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.NiagaraComponentService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.NiagaraComponentService
 *
 *   # One-shot spawn at a world location
 *   label = svc.spawn_niagara_at_location(
 *       "/Game/VFX/NS_Impact",
 *       unreal.Vector(0, 0, 100),
 *       unreal.Rotator(0, 0, 0),
 *       True)
 *
 *   # Attached to another actor's root (or socket)
 *   svc.spawn_niagara_attached(
 *       "/Game/VFX/NS_Trail", "Hero", "MuzzleSocket", False)
 *
 *   # User-exposed parameter binding
 *   svc.set_niagara_user_param_float(label, "User.DamageAmount", 150.0)
 *   svc.set_niagara_user_param_vector(label, "User.HitNormal",
 *                                     unreal.Vector(0, 0, 1))
 *   svc.set_niagara_user_param_color(label, "User.TintColor",
 *                                    unreal.LinearColor(1, 0, 0, 1))
 *
 *   # Stop / kill
 *   svc.stop_niagara_component(label, True)
 */
UCLASS(BlueprintType)
class REVVY_API UNiagaraComponentService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Spawn
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Spawn a Niagara system at a world-space location as a fire-and-forget
	 * one-shot effect. A transient ANiagaraActor owner is auto-created by the
	 * engine and its label is returned so the caller can drive parameters or
	 * stop it later.
	 *
	 * @param SystemAssetPath - Package path to a UNiagaraSystem asset
	 *                          (e.g. "/Game/VFX/NS_Impact").
	 * @param Location        - World-space spawn location.
	 * @param Rotation        - World-space rotation.
	 * @param bAutoDestroy    - If true the component destroys itself when the
	 *                          system completes.
	 * @return Actor label of the owning ANiagaraActor, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|Spawn")
	static FString SpawnNiagaraAtLocation(
		const FString& SystemAssetPath,
		FVector Location,
		FRotator Rotation,
		bool bAutoDestroy);

	/**
	 * Spawn a Niagara system attached to a target actor's root component (or
	 * to a named socket on the actor's first skeletal-mesh-like component).
	 *
	 * @param SystemAssetPath        - Package path to a UNiagaraSystem asset.
	 * @param TargetActorNameOrPath  - Label / name / soft-object-path of the actor
	 *                                 to attach to.
	 * @param SocketName             - Socket name on the target component (blank
	 *                                 attaches to the component root).
	 * @param bAutoDestroy           - If true the component destroys itself when
	 *                                 the system completes.
	 * @return Actor label of the owning ANiagaraActor (or the target actor when
	 *         the engine parents the component under it), or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|Spawn")
	static FString SpawnNiagaraAttached(
		const FString& SystemAssetPath,
		const FString& TargetActorNameOrPath,
		const FString& SocketName,
		bool bAutoDestroy);

	// ═══════════════════════════════════════════════════════════════════
	// User Parameters
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set a User-namespace float parameter on the Niagara component owned by
	 * the target actor.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor
	 *                          hosting the UNiagaraComponent.
	 * @param ParamName       - Parameter name, typically prefixed with "User."
	 *                          (e.g. "User.DamageAmount").
	 * @param Value           - Scalar value to bind.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|UserParam")
	static bool SetNiagaraUserParamFloat(
		const FString& ActorNameOrPath,
		const FString& ParamName,
		float Value);

	/**
	 * Set a User-namespace vector (Vec3) parameter on the Niagara component
	 * owned by the target actor.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @param ParamName       - Parameter name (e.g. "User.HitNormal").
	 * @param Value           - Vector value to bind.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|UserParam")
	static bool SetNiagaraUserParamVector(
		const FString& ActorNameOrPath,
		const FString& ParamName,
		FVector Value);

	/**
	 * Set a User-namespace linear-color parameter on the Niagara component
	 * owned by the target actor.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor.
	 * @param ParamName       - Parameter name (e.g. "User.TintColor").
	 * @param Value           - Linear color value to bind.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|UserParam")
	static bool SetNiagaraUserParamColor(
		const FString& ActorNameOrPath,
		const FString& ParamName,
		FLinearColor Value);

	// ═══════════════════════════════════════════════════════════════════
	// Control
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Stop the Niagara component owned by the target actor.
	 *
	 * @param ActorNameOrPath - Label / name / soft-object-path of the actor
	 *                          hosting the UNiagaraComponent.
	 * @param bImmediate      - If true, kill all particles right away
	 *                          (DeactivateImmediate); if false, stop spawning and
	 *                          let existing particles drain (Deactivate).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|NiagaraComponent|Control")
	static bool StopNiagaraComponent(const FString& ActorNameOrPath, bool bImmediate);
};
