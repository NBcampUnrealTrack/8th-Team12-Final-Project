// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UBenchmarkService.generated.h"

/**
 * Service-call latency measurement utility for the Revvy Python-facing
 * service layer. Provides bounded, reflection-safe micro-benchmarks so
 * Python callers can quantify measurement overhead and per-service
 * reflection cost without invoking arbitrary UFUNCTIONs.
 *
 * Mirrors the UServiceHealthCheckService pattern: all UFUNCTIONs are
 * static + BlueprintCallable so they are reachable from Python as
 * `unreal.BenchmarkService.<snake_case_name>()`.
 *
 * Timing source: FPlatformTime::Seconds (monotonic double, seconds).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.BenchmarkService
 *   base = json.loads(svc.benchmark_timestamp_call(10000))
 *   load = json.loads(svc.measure_service_load_time("USubsystemQueryService"))
 *
 * All returned JSON is compact.
 */
UCLASS(BlueprintType)
class REVVY_API UBenchmarkService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Baseline sanity check: measure the overhead of the timing primitive
	 * itself by calling FPlatformTime::Seconds() in a tight loop.
	 *
	 * @param Iterations  Number of FPlatformTime::Seconds() calls to make.
	 *                    Values <= 0 are coerced to 1.
	 * @return JSON object
	 *         {iterations, total_ms, avg_us, ops_per_sec}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Benchmark|Measure")
	static FString BenchmarkTimestampCall(int32 Iterations);

	/**
	 * Measure the cost of the two reflection paths Python takes to reach a
	 * Revvy service: CDO acquisition (GetDefaultObject) and UFunction field
	 * enumeration ("FieldMap access"), both across 100 iterations.
	 *
	 * @param ServiceClassName  Bare class name (leading 'U' optional).
	 * @return JSON object
	 *         {service, cdo_access_ns, ufn_iter_ns, class_flags}.
	 *         Contains "error" and "note" fields when the class cannot
	 *         be resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Benchmark|Measure")
	static FString MeasureServiceLoadTime(const FString& ServiceClassName);
};
