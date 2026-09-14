// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UServiceHealthCheckService.generated.h"

/**
 * Smoke-test / health-check service for the Revvy Python-facing service
 * layer. Performs pure REFLECTION checks — no UFUNCTION is invoked, so
 * the checks are guaranteed to be side-effect free.
 *
 * Mirrors the UServiceRegistryService pattern: all UFUNCTIONs are
 * static + BlueprintCallable so they are reachable from Python as
 * `unreal.ServiceHealthCheckService.<snake_case_name>()`.
 *
 * Health-check criteria per service:
 *   - class loaded          (implicit: only iterated loaded classes)
 *   - ufunction_count > 0   (BlueprintCallable UFN count)
 *   - CDO creatable         (GetDefaultObject() non-null)
 *   - signature parseable   (first UFN has a valid function-property map)
 *
 * status semantics:
 *   "ok"      — all four checks passed
 *   "warning" — CDO ok but zero UFUNCTIONs OR unparseable signature
 *   "error"   — CDO could not be constructed
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.ServiceHealthCheckService
 *   report  = json.loads(svc.health_check_all_services())
 *   single  = json.loads(svc.health_check_service("USubsystemQueryService"))
 *   loaded  = svc.is_class_loaded("USubsystemQueryService")
 *
 * All returned JSON is compact.
 */
UCLASS(BlueprintType)
class REVVY_API UServiceHealthCheckService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Run the reflection smoke-test across every Revvy "U*Service" class.
	 *
	 * @return JSON array of {service, ufn_count, status, note}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|HealthCheck|Check")
	static FString HealthCheckAllServices();

	/**
	 * Run the reflection smoke-test on a single Revvy service class.
	 *
	 * @param ServiceClassName  Bare class name (e.g. "USubsystemQueryService").
	 * @return JSON object {service, ufn_count, status, note}. status="error"
	 *         with an explanatory note if the class cannot be resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|HealthCheck|Check")
	static FString HealthCheckService(const FString& ServiceClassName);

	/**
	 * True if a UClass with the given bare name is currently loaded under
	 * the "/Script/Revvy." package path.
	 *
	 * @param ServiceClassName  Bare class name (leading 'U' optional).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|HealthCheck|Query")
	static bool IsClassLoaded(const FString& ServiceClassName);
};
