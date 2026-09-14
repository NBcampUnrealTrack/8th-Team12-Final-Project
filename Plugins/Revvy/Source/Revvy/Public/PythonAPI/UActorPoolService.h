// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Transform.h"
#include "UActorPoolService.generated.h"

/**
 * ActorPool service exposed directly to Python for lightweight actor pooling in
 * the editor world.
 *
 * Mirrors the UGeometryScriptService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.ActorPoolService.<snake_case_name>(...)`.
 *
 * State model:
 *   A named-pool registry (TMap<FString, FPool>) is held in the .cpp anon
 *   namespace. Each pool tracks a UClass, a MaxSize cap, and two sets of
 *   TWeakObjectPtr<AActor> references: Available (idle, hidden) and InUse
 *   (checked out to a caller). Stale weak refs are pruned on every access.
 *
 *   No FScopedTransaction is used — pool state is runtime only and undo/redo
 *   would corrupt the Available/InUse invariants.
 *
 * World scope:
 *   Pools spawn into `GEditor->GetEditorWorldContext().World()`. During PIE
 *   the PIE world is a separate UWorld instance, so pooled actors created in
 *   the editor world will NOT be visible in PIE. Create/destroy PIE pools
 *   inside a PIE session if that is required.
 *
 * Threading:
 *   GameThread only. Registry access is not guarded.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.ActorPoolService
 *
 *   svc.create_pool("Bullets", "/Script/Engine.StaticMeshActor", 10, 100)
 *
 *   label = svc.acquire_from_pool("Bullets", unreal.Transform())
 *   # ... use actor ...
 *   svc.release_to_pool("Bullets", label)
 *
 *   print(svc.get_pool_stats("Bullets"))
 *   svc.destroy_pool("Bullets")
 */
UCLASS(BlueprintType)
class REVVY_API UActorPoolService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new named pool, resolve the actor class, and pre-spawn InitialSize actors
	 * (hidden, no collision, tick disabled) into the Available list.
	 *
	 * @param PoolName       - Unique registry key for this pool. Empty / duplicate → rejected.
	 * @param ActorClassPath - Full class path (e.g. "/Script/Engine.StaticMeshActor" or a
	 *                        Blueprint generated class path). Must be a UClass IsChildOf(AActor).
	 * @param InitialSize    - Number of actors to spawn eagerly. Clamped to [0, MaxSize].
	 * @param MaxSize        - Hard cap on total live actors (Available + InUse). Must be >= 1.
	 * @return True on successful registration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ActorPool|Lifecycle")
	static bool CreatePool(
		const FString& PoolName,
		const FString& ActorClassPath,
		int32 InitialSize,
		int32 MaxSize);

	/**
	 * Check out an actor from the pool at PoolName. Reuses an Available actor if one exists,
	 * otherwise spawns a new one (up to MaxSize). Returned actor is un-hidden, collision + tick
	 * re-enabled, and moved to InUse.
	 *
	 * @param PoolName       - Registered pool name.
	 * @param SpawnTransform - World transform to apply to the acquired actor.
	 * @return The actor's label on success, or empty string if pool missing / exhausted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ActorPool|Acquire")
	static FString AcquireFromPool(const FString& PoolName, FTransform SpawnTransform);

	/**
	 * Return an actor to the pool, hide it, disable collision + tick, and reset its transform
	 * to origin. Moves the actor from InUse to Available.
	 *
	 * @param PoolName   - Registered pool name.
	 * @param ActorLabel - Label of the acquired actor (as returned by AcquireFromPool).
	 * @return True if the actor was found in InUse and released successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ActorPool|Release")
	static bool ReleaseToPool(const FString& PoolName, const FString& ActorLabel);

	/**
	 * Return a compact JSON summary of the pool: name, actor_class, available_count,
	 * in_use_count, max_size, total_ever_spawned. Stale TWeakObjectPtr entries are cleaned
	 * as a side effect of the count.
	 *
	 * @param PoolName - Registered pool name.
	 * @return JSON string, or empty string if the pool is missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ActorPool|Query")
	static FString GetPoolStats(const FString& PoolName);

	/**
	 * Destroy every actor owned by the pool (Available + InUse) and remove the registry entry.
	 *
	 * @param PoolName - Registered pool name.
	 * @return True if the pool existed and was destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ActorPool|Lifecycle")
	static bool DestroyPool(const FString& PoolName);
};
