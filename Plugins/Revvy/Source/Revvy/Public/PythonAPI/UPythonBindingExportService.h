// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPythonBindingExportService.generated.h"

/**
 * Python-stub (.pyi) exporter for Revvy services.
 *
 * Emits Python type-stub files for every "U*Service" class shipped by the
 * Revvy plugin so that IDEs (VS Code, PyCharm) can offer autocomplete /
 * type-checking on the `unreal.<ServiceName>` shims exposed by Unreal's
 * Python binding.
 *
 * Mirrors the UServiceRegistryService (iter63) reflection-based pattern:
 * all UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.PythonBindingExportService.<snake_case_name>()`.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.PythonBindingExportService
 *   path      = svc.export_service_to_pyi("USubsystemQueryService", "D:/stubs/subsys.pyi")
 *   summary   = json.loads(svc.export_all_services_to_pyi("D:/stubs/"))
 */
UCLASS(BlueprintType)
class REVVY_API UPythonBindingExportService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Export the BlueprintCallable UFUNCTIONs of a single Revvy service to a
	 * Python type-stub (.pyi) file.
	 *
	 * @param ServiceClassName  Bare class name (e.g. "USubsystemQueryService").
	 * @param OutputPath        Absolute path to the .pyi file to write.
	 * @return                  The absolute path that was written, or "" on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PyExport|Export")
	static FString ExportServiceToPyi(const FString& ServiceClassName, const FString& OutputPath);

	/**
	 * Export every Revvy "U*Service" class to its own .pyi file inside
	 * OutputDirectory. File names follow the pattern "<snake_case>.pyi".
	 *
	 * @param OutputDirectory  Absolute directory to receive the .pyi files.
	 * @return                 JSON array of {service, path, size} entries — one
	 *                         per exported service.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PyExport|Export")
	static FString ExportAllServicesToPyi(const FString& OutputDirectory);
};
