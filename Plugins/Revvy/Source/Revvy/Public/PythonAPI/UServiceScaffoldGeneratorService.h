// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UServiceScaffoldGeneratorService.generated.h"

/**
 * Revvy service .h/.cpp stub scaffold generator.
 *
 * Emits a minimal, compilable "U*Service" pair (header + implementation)
 * to bootstrap a new Revvy service. The generated header declares a single
 * static BlueprintCallable UFUNCTION (GetInfo) so that the resulting class
 * is immediately reachable from Python via `unreal.<ClassName>.get_info()`.
 *
 * Mirrors the UPythonBindingExportService (iter64) file-writer pattern:
 * all UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.ServiceScaffoldGeneratorService.<snake_case_name>()`.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.ServiceScaffoldGeneratorService
 *   result = json.loads(svc.generate_service_scaffold(
 *       "UMyNewFeatureService",
 *       "MyFeature",
 *       "D:/DuckSoul/Plugins/Revvy/Source/Revvy/Public/PythonAPI/"))
 *   print(result["header_path"], result["cpp_path"])
 */
UCLASS(BlueprintType)
class REVVY_API UServiceScaffoldGeneratorService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Generate a stub .h/.cpp pair for a new Revvy "U*Service" class.
	 *
	 * The header is written to OutputDir/<ClassName>.h and the cpp is written
	 * to OutputDir/<ClassName>.cpp. OutputDir is created recursively if it
	 * does not exist. ServiceClassName must begin with 'U' and end with
	 * "Service" (e.g. "UMyFeatureService").
	 *
	 * @param ServiceClassName  Bare class name (e.g. "UMyFeatureService").
	 * @param CategoryDomain    Sub-domain used in UFUNCTION Category metadata
	 *                          (e.g. "MyFeature"). Special JSON/UPROPERTY
	 *                          characters are escaped before emission.
	 * @param OutputDir         Absolute directory to receive the two files.
	 * @return                  JSON object of the form
	 *                          {"header_path","cpp_path","class_name","category_domain"},
	 *                          or "" on validation / IO failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Scaffold|Generate")
	static FString GenerateServiceScaffold(const FString& ServiceClassName, const FString& CategoryDomain, const FString& OutputDir);
};
