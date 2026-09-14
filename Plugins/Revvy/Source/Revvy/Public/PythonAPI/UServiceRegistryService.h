// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UServiceRegistryService.generated.h"

/**
 * Meta-introspection service: Revvy interrogates its OWN Python-facing
 * services (all UObject-derived classes shipped by the Revvy plugin
 * whose name follows the "U*Service" convention).
 *
 * Mirrors the USubsystemQueryService pattern: all UFUNCTIONs are
 * static + BlueprintCallable so they are reachable from Python as
 * `unreal.ServiceRegistryService.<snake_case_name>()`.
 *
 * No mutation → no FScopedTransaction required.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.ServiceRegistryService
 *   services = json.loads(svc.list_revvy_services())
 *   ufns     = json.loads(svc.get_service_ufunctions("USubsystemQueryService"))
 *   matches  = json.loads(svc.search_service_by_category("Query"))
 *
 * All returned JSON is compact.
 */
UCLASS(BlueprintType)
class REVVY_API UServiceRegistryService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Enumerate every UClass shipped by the Revvy plugin ("/Script/Revvy.")
	 * whose name starts with 'U' and ends with 'Service'.
	 *
	 * @return JSON array of {class_name, class_path, ufunction_count}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ServiceRegistry|Query")
	static FString ListRevvyServices();

	/**
	 * Enumerate the BlueprintCallable UFUNCTIONs on a given Revvy service.
	 *
	 * @param ServiceClassName  Bare class name (e.g. "USubsystemQueryService").
	 * @return JSON array of {name, signature, category, num_params}. Empty
	 *         array if the class cannot be resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ServiceRegistry|Query")
	static FString GetServiceUFunctions(const FString& ServiceClassName);

	/**
	 * Case-insensitive substring scan across every Revvy service and every
	 * one of its UFUNCTIONs, matching against the UFUNCTION's Category meta.
	 *
	 * @param CategorySubstring  Fragment to match (e.g. "Query").
	 * @return JSON array of {service_name, ufn_name, category}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ServiceRegistry|Query")
	static FString SearchServiceByCategory(const FString& CategorySubstring);
};
