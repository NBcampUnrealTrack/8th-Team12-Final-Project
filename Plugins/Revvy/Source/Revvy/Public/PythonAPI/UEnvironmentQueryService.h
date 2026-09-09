// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "UEnvironmentQueryService.generated.h"

class UEnvQueryInstanceBlueprintWrapper;

/**
 * Internal receiver UObject used to bind a UFUNCTION callback to the dynamic
 * multicast OnQueryFinishedEvent exposed by UEnvQueryInstanceBlueprintWrapper.
 * One receiver is spawned per RunEQSQuery invocation and kept alive by a
 * TStrongObjectPtr in the service's parallel registry until the query
 * finishes or is aborted.
 *
 * Not intended for direct Python / Blueprint use.
 */
UCLASS()
class REVVY_API UEQSQueryHandleReceiver : public UObject
{
	GENERATED_BODY()

public:
	/** Handle string this receiver is bound to. */
	UPROPERTY()
	FString Handle;

	/**
	 * Bound to UEnvQueryInstanceBlueprintWrapper::OnQueryFinishedEvent.
	 * Removes the associated Handle entry from UEnvironmentQueryService's
	 * active registry when the underlying EQS query completes.
	 */
	UFUNCTION()
	void OnQueryFinished(UEnvQueryInstanceBlueprintWrapper* QueryInstance,
		EEnvQueryStatus::Type QueryStatus);
};

/**
 * Environment Query System (EQS) runtime query execution service exposed to
 * Python.
 *
 * Mirrors the UBehaviorTreeService (iter37 sibling) runtime-AI pattern: all
 * UFUNCTIONs are static + BlueprintCallable so they are reachable from Python
 * as `unreal.EnvironmentQueryService.<snake_case_name>(...)`.
 *
 * The service maintains a static ActiveQueries registry keyed by a generated
 * FGuid handle string. RunEQSQuery returns this handle; every subsequent
 * status / result / abort call is scoped by handle. Registry entries are
 * cleaned up when the underlying query finishes (via a bound receiver) or
 * when the caller invokes AbortEQSQuery.
 *
 * Runtime operations touch transient state on the live EnvQueryManager /
 * wrapper — no FScopedTransaction is used.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.EnvironmentQueryService
 *
 *   handle = svc.run_eqs_query(
 *       "/Game/AI/EQS/EQS_FindCover",
 *       "EnemyAIController0",
 *       "SingleBest")
 *   # ... poll ...
 *   if not svc.is_eqs_query_running(handle):
 *       json_actors = svc.get_eqs_result_actors(handle)
 */
UCLASS(BlueprintType)
class REVVY_API UEnvironmentQueryService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query Execution
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Load a UEnvQuery template asset and start it against the given querier.
	 *
	 * @param QueryTemplatePath        Package path of the UEnvQuery asset
	 *                                 e.g. "/Game/AI/EQS/EQS_FindCover".
	 * @param QuerierActorNameOrPath   Actor label / name / soft path of the
	 *                                 querier UObject (usually an AAIController
	 *                                 or its pawn).
	 * @param RunModeStr               One of "SingleBest" (a.k.a. SingleResult),
	 *                                 "AllMatching", "RandomBest5Pct",
	 *                                 "RandomBest25Pct".
	 * @return Handle string (FGuid) usable in the other service functions,
	 *         empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Run")
	static FString RunEQSQuery(
		const FString& QueryTemplatePath,
		const FString& QuerierActorNameOrPath,
		const FString& RunModeStr);

	/**
	 * Abort a previously started EQS query. Clears the wrapper's finish
	 * delegate and removes the entry from the active registry.
	 *
	 * @param QueryHandle  Handle previously returned from RunEQSQuery.
	 * @return True if the handle was known and cleaned up, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Run")
	static bool AbortEQSQuery(const FString& QueryHandle);

	// ═══════════════════════════════════════════════════════════════════
	// Query Status
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * @return True while the underlying EQS instance is still in
	 *         EEnvQueryStatus::Processing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Query")
	static bool IsEQSQueryRunning(const FString& QueryHandle);

	// ═══════════════════════════════════════════════════════════════════
	// Results
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Returns a JSON array of result actors + score entries produced by the
	 * given query. Only valid once the query has completed successfully.
	 *
	 * Format: `[{"actor_label": "...", "score": 0.87}, ...]`
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Result")
	static FString GetEQSResultActors(const FString& QueryHandle);

	/**
	 * Returns a JSON array of result locations + score entries produced by
	 * the given query. Only valid once the query has completed successfully.
	 *
	 * Format: `[{"x": 0.0, "y": 0.0, "z": 0.0, "score": 0.87}, ...]`
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Result")
	static FString GetEQSResultLocations(const FString& QueryHandle);

	/**
	 * Returns the raw score of a single result item. Returns -1.0 when the
	 * handle is unknown or the item index is out of range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|EQS|Result")
	static float GetEQSResultScore(const FString& QueryHandle, int32 ItemIndex);
};
