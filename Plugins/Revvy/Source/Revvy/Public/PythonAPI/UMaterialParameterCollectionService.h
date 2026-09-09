// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UMaterialParameterCollectionService.generated.h"

/**
 * Material Parameter Collection (MPC) authoring service exposed directly to
 * Python for creating UMaterialParameterCollection assets, editing their
 * scalar / vector default entries, and driving their runtime
 * UMaterialParameterCollectionInstance overrides in the editor world.
 *
 * Mirrors the ULightingService / USequencerService pattern: all UFUNCTIONs
 * are static and BlueprintCallable so they are reachable from Python as
 * `unreal.MaterialParameterCollectionService.<snake_case_name>(...)`.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.MaterialParameterCollectionService
 *
 *   # Create
 *   mpc_path = svc.create_mpc("/Game/Materials/MPC_Weather")
 *
 *   # Parameters (defaults, persisted to asset)
 *   svc.add_scalar_parameter(mpc_path, "WindStrength", 0.0)
 *   svc.add_vector_parameter(mpc_path, "SkyTint",
 *                            unreal.LinearColor(1, 1, 1, 1))
 *   svc.set_scalar_parameter_default(mpc_path, "WindStrength", 0.5)
 *   svc.set_vector_parameter_default(mpc_path, "SkyTint",
 *                                    unreal.LinearColor(0.9, 0.95, 1.0, 1))
 *
 *   # Runtime (editor world instance overrides — not persisted)
 *   svc.set_scalar_at_runtime(mpc_path, "WindStrength", 2.5)
 *   svc.set_vector_at_runtime(mpc_path, "SkyTint",
 *                             unreal.LinearColor(0.3, 0.5, 1.0, 1))
 *
 *   # Query
 *   svc.list_mpc_parameters(mpc_path)
 *   #   => '{"scalars":[{"name":"WindStrength","default":0.5}],
 *   #        "vectors":[{"name":"SkyTint",
 *   #                    "default":{"r":0.9,"g":0.95,"b":1.0,"a":1.0}}]}'
 *
 * @note Runtime setters operate on the editor world's MPC instance and are
 *       not saved into the asset — use SetScalarParameterDefault /
 *       SetVectorParameterDefault for persistent authoring changes.
 */
UCLASS(BlueprintType)
class REVVY_API UMaterialParameterCollectionService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new empty UMaterialParameterCollection asset at the given
	 * package path.
	 *
	 * @param OutputAssetPath - Full package path (e.g. "/Game/Materials/MPC_Weather")
	 * @return Asset path of the created MPC, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Create")
	static FString CreateMPC(const FString& OutputAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Parameters (defaults, persisted)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append a new scalar parameter entry to the MPC's default list.
	 *
	 * @param MPCAssetPath - Path to the target UMaterialParameterCollection asset
	 * @param ParamName    - Parameter name (case-sensitive, becomes FName)
	 * @param DefaultValue - Default scalar value
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Parameters")
	static bool AddScalarParameter(const FString& MPCAssetPath, const FString& ParamName, float DefaultValue);

	/**
	 * Append a new vector parameter entry to the MPC's default list.
	 *
	 * @param MPCAssetPath - Path to the target UMaterialParameterCollection asset
	 * @param ParamName    - Parameter name (case-sensitive, becomes FName)
	 * @param DefaultValue - Default vector value (as FLinearColor)
	 * @return True on success
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Parameters")
	static bool AddVectorParameter(const FString& MPCAssetPath, const FString& ParamName, FLinearColor DefaultValue);

	/**
	 * Update the default value of an existing scalar parameter (found by name).
	 *
	 * @param MPCAssetPath - Path to the target MPC asset
	 * @param ParamName    - Existing scalar parameter name
	 * @param NewDefault   - New default scalar value
	 * @return True on success (false if parameter not found)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Parameters")
	static bool SetScalarParameterDefault(const FString& MPCAssetPath, const FString& ParamName, float NewDefault);

	/**
	 * Update the default value of an existing vector parameter (found by name).
	 *
	 * @param MPCAssetPath - Path to the target MPC asset
	 * @param ParamName    - Existing vector parameter name
	 * @param NewDefault   - New default vector value (as FLinearColor)
	 * @return True on success (false if parameter not found)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Parameters")
	static bool SetVectorParameterDefault(const FString& MPCAssetPath, const FString& ParamName, FLinearColor NewDefault);

	// ═══════════════════════════════════════════════════════════════════
	// Runtime (editor world instance overrides, not persisted)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Set a scalar override on the MPC instance in the editor world.
	 * Not saved to the asset — persists only for the current editor session.
	 *
	 * @param MPCAssetPath - Path to the target MPC asset
	 * @param ParamName    - Parameter name to override
	 * @param Value        - Runtime scalar value
	 * @return True on success (false if instance or parameter not found)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Runtime")
	static bool SetScalarAtRuntime(const FString& MPCAssetPath, const FString& ParamName, float Value);

	/**
	 * Set a vector override on the MPC instance in the editor world.
	 * Not saved to the asset — persists only for the current editor session.
	 *
	 * @param MPCAssetPath - Path to the target MPC asset
	 * @param ParamName    - Parameter name to override
	 * @param Value        - Runtime vector value (as FLinearColor)
	 * @return True on success (false if instance or parameter not found)
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Runtime")
	static bool SetVectorAtRuntime(const FString& MPCAssetPath, const FString& ParamName, FLinearColor Value);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Dump the MPC's scalar and vector parameter defaults as JSON.
	 *
	 * @param MPCAssetPath - Path to the target MPC asset
	 * @return JSON string:
	 *   {"scalars":[{"name":"...","default":0.0}, ...],
	 *    "vectors":[{"name":"...","default":{"r":0,"g":0,"b":0,"a":0}}, ...]}
	 *   or {"error":"..."} on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|MaterialParameterCollection|Query")
	static FString ListMPCParameters(const FString& MPCAssetPath);
};
