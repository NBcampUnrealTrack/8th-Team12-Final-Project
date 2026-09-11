// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UBuildInfoService.generated.h"

/**
 * Build-info query utility for the Revvy Python-facing service layer.
 * Reports the Revvy plugin version marker, compile-time build metadata,
 * and the number of Revvy "U*Service" classes currently loaded in the
 * editor process.
 *
 * Mirrors the UBenchmarkService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.BuildInfoService.<snake_case_name>()`.
 *
 * All returned JSON is compact. All queries are strictly read-only.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.BuildInfoService
 *   info  = json.loads(svc.get_revvy_build_info())
 *   count = svc.get_loaded_revvy_service_count()
 */
UCLASS(BlueprintType)
class REVVY_API UBuildInfoService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Return the Revvy plugin version marker together with the compile-time
	 * build metadata (engine version, platform name, editor/debug/shipping
	 * flags, compiler identity, and build timestamp).
	 *
	 * @return JSON object
	 *         {revvy_version, build_timestamp, ue_version, platform,
	 *          is_editor, is_debug, is_shipping, compiler}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BuildInfo|Query")
	static FString GetRevvyBuildInfo();

	/**
	 * Count the number of currently loaded Revvy "U*Service" classes,
	 * defined as UClasses whose path begins with "/Script/Revvy." and
	 * whose C++ name matches the "U*Service" convention.
	 *
	 * @return Count of loaded Revvy service classes (0 if none).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|BuildInfo|Query")
	static int32 GetLoadedRevvyServiceCount();
};
