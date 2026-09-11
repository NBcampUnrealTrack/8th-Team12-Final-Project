// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UActorPoolService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyActorPool, Log, All);

#include "Core/URevvyCommonHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Static state (anonymous namespace — hidden from other TUs)
// =================================================================

namespace
{
	/** Per-pool bookkeeping. */
	struct FPool
	{
		TWeakObjectPtr<UClass> ActorClass;
		FString ActorClassPath;
		int32 MaxSize = 0;
		int32 TotalEverSpawned = 0;
		TArray<TWeakObjectPtr<AActor>> Available;
		TArray<TWeakObjectPtr<AActor>> InUse;
	};

	/** Named-pool registry, keyed by user-supplied pool name. */
	static TMap<FString, FPool> Pools;

	/** Emit a Warning if not on the GameThread — registry access is not thread-safe. */
	void ActorPoolWarnIfNotGameThread(const TCHAR* Op)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogRevvyActorPool, Warning,
				TEXT("UActorPoolService::%s called off GameThread — registry access is not thread-safe."),
				Op);
		}
	}

	/** Drop entries whose weak refs no longer point at a live actor. */
	void PruneStale(TArray<TWeakObjectPtr<AActor>>& Refs)
	{
		Refs.RemoveAll([](const TWeakObjectPtr<AActor>& Ref)
		{
			return !Ref.IsValid();
		});
	}

	/** Put an actor into the "idle" state used by pooled inhabitants of Available. */
	void DeactivateActor(AActor* Actor)
	{
		if (!Actor) return;

		Actor->SetActorHiddenInGame(true);
		Actor->SetActorEnableCollision(false);
		Actor->SetActorTickEnabled(false);
	#if WITH_EDITOR
		Actor->SetIsTemporarilyHiddenInEditor(true);
	#endif
	}

	/** Wake an actor at the requested transform for a caller of AcquireFromPool. */
	void ActivateActor(AActor* Actor, const FTransform& SpawnTransform)
	{
		if (!Actor) return;

		Actor->SetActorTransform(SpawnTransform, /*bSweep=*/ false, /*OutSweepHitResult=*/ nullptr,
			ETeleportType::TeleportPhysics);
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorEnableCollision(true);
		Actor->SetActorTickEnabled(true);
	#if WITH_EDITOR
		Actor->SetIsTemporarilyHiddenInEditor(false);
	#endif
	}

	/** Spawn a fresh actor into the editor world, deactivate it, and bump the pool counter. */
	AActor* SpawnPoolActor(FPool& Pool, UWorld* World)
	{
		UClass* Cls = Pool.ActorClass.Get();
		if (!World || !Cls)
		{
			return nullptr;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags = RF_Transactional;

		AActor* NewActor = World->SpawnActor<AActor>(Cls, FTransform::Identity, SpawnParams);
		if (!NewActor)
		{
			return nullptr;
		}

		Pool.TotalEverSpawned++;
		return NewActor;
	}

	/** Serialize a JSON object to a compact string. */
	FString ActorPoolJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}
}

// =================================================================
// Lifecycle
// =================================================================

bool UActorPoolService::CreatePool(
	const FString& PoolName,
	const FString& ActorClassPath,
	int32 InitialSize,
	int32 MaxSize)
{
	ActorPoolWarnIfNotGameThread(TEXT("CreatePool"));

	if (PoolName.IsEmpty())
	{
		UE_LOG(LogRevvyActorPool, Warning, TEXT("CreatePool: PoolName must be non-empty."));
		return false;
	}

	if (Pools.Contains(PoolName))
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("CreatePool: pool '%s' is already registered."), *PoolName);
		return false;
	}

	if (MaxSize < 1)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("CreatePool: MaxSize must be >= 1 (got %d)."), MaxSize);
		return false;
	}

	if (ActorClassPath.IsEmpty())
	{
		UE_LOG(LogRevvyActorPool, Warning, TEXT("CreatePool: ActorClassPath must be non-empty."));
		return false;
	}

	UClass* ResolvedClass = URevvyCommonHelpers::LoadClassWithFallback(ActorClassPath, AActor::StaticClass());
	if (!ResolvedClass)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("CreatePool: could not resolve '%s' to a UClass IsChildOf(AActor)."),
			*ActorClassPath);
		return false;
	}

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("CreatePool: no editor world available (GEditor missing)."));
		return false;
	}

	FPool NewPool;
	NewPool.ActorClass = ResolvedClass;
	NewPool.ActorClassPath = ActorClassPath;
	NewPool.MaxSize = MaxSize;

	const int32 ClampedInitial = FMath::Clamp(InitialSize, 0, MaxSize);
	for (int32 i = 0; i < ClampedInitial; ++i)
	{
		AActor* Actor = SpawnPoolActor(NewPool, World);
		if (!Actor)
		{
			UE_LOG(LogRevvyActorPool, Warning,
				TEXT("CreatePool: pre-spawn %d/%d failed for pool '%s'; continuing with partial pool."),
				i + 1, ClampedInitial, *PoolName);
			break;
		}
		DeactivateActor(Actor);
		NewPool.Available.Add(Actor);
	}

	Pools.Add(PoolName, MoveTemp(NewPool));

	UE_LOG(LogRevvyActorPool, Log,
		TEXT("CreatePool: '%s' registered (class='%s', initial=%d, max=%d)."),
		*PoolName, *ActorClassPath, ClampedInitial, MaxSize);
	return true;
}

FString UActorPoolService::AcquireFromPool(const FString& PoolName, FTransform SpawnTransform)
{
	ActorPoolWarnIfNotGameThread(TEXT("AcquireFromPool"));

	FPool* Pool = Pools.Find(PoolName);
	if (!Pool)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("AcquireFromPool: pool '%s' is not registered."), *PoolName);
		return FString();
	}

	PruneStale(Pool->Available);
	PruneStale(Pool->InUse);

	// Reuse an available actor if we have one.
	while (Pool->Available.Num() > 0)
	{
		TWeakObjectPtr<AActor> WeakActor = Pool->Available.Pop(EAllowShrinking::No);
		AActor* Actor = WeakActor.Get();
		if (!Actor)
		{
			continue; // stale — try the next one
		}

		ActivateActor(Actor, SpawnTransform);
		Pool->InUse.Add(WeakActor);

		UE_LOG(LogRevvyActorPool, Verbose,
			TEXT("AcquireFromPool: pool '%s' reused actor '%s'."),
			*PoolName, *Actor->GetActorLabel());
		return Actor->GetActorLabel();
	}

	// No available actors — try to spawn a new one if there is headroom.
	const int32 LiveCount = Pool->Available.Num() + Pool->InUse.Num();
	if (LiveCount >= Pool->MaxSize)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("AcquireFromPool: pool '%s' exhausted (live=%d, max=%d)."),
			*PoolName, LiveCount, Pool->MaxSize);
		return FString();
	}

	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("AcquireFromPool: no editor world available for pool '%s'."),
			*PoolName);
		return FString();
	}

	AActor* NewActor = SpawnPoolActor(*Pool, World);
	if (!NewActor)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("AcquireFromPool: SpawnActor failed for pool '%s'."), *PoolName);
		return FString();
	}

	ActivateActor(NewActor, SpawnTransform);
	Pool->InUse.Add(NewActor);

	UE_LOG(LogRevvyActorPool, Verbose,
		TEXT("AcquireFromPool: pool '%s' spawned new actor '%s' (total_spawned=%d)."),
		*PoolName, *NewActor->GetActorLabel(), Pool->TotalEverSpawned);
	return NewActor->GetActorLabel();
}

bool UActorPoolService::ReleaseToPool(const FString& PoolName, const FString& ActorLabel)
{
	ActorPoolWarnIfNotGameThread(TEXT("ReleaseToPool"));

	FPool* Pool = Pools.Find(PoolName);
	if (!Pool)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("ReleaseToPool: pool '%s' is not registered."), *PoolName);
		return false;
	}

	PruneStale(Pool->InUse);

	int32 FoundIndex = INDEX_NONE;
	AActor* Target = nullptr;
	for (int32 i = 0; i < Pool->InUse.Num(); ++i)
	{
		AActor* Actor = Pool->InUse[i].Get();
		if (Actor && Actor->GetActorLabel() == ActorLabel)
		{
			FoundIndex = i;
			Target = Actor;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE || !Target)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("ReleaseToPool: actor with label '%s' not found in InUse of pool '%s'."),
			*ActorLabel, *PoolName);
		return false;
	}

	TWeakObjectPtr<AActor> WeakActor = Pool->InUse[FoundIndex];
	Pool->InUse.RemoveAt(FoundIndex, EAllowShrinking::No);

	DeactivateActor(Target);
	Target->SetActorTransform(FTransform::Identity, /*bSweep=*/ false, nullptr, ETeleportType::TeleportPhysics);
	Pool->Available.Add(WeakActor);

	UE_LOG(LogRevvyActorPool, Verbose,
		TEXT("ReleaseToPool: pool '%s' released actor '%s'."),
		*PoolName, *ActorLabel);
	return true;
}

FString UActorPoolService::GetPoolStats(const FString& PoolName)
{
	ActorPoolWarnIfNotGameThread(TEXT("GetPoolStats"));

	FPool* Pool = Pools.Find(PoolName);
	if (!Pool)
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("GetPoolStats: pool '%s' is not registered."), *PoolName);
		return FString();
	}

	// Clean up stale weak refs as a side effect of counting.
	PruneStale(Pool->Available);
	PruneStale(Pool->InUse);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), PoolName);
	Obj->SetStringField(TEXT("actor_class"), Pool->ActorClassPath);
	Obj->SetNumberField(TEXT("available_count"), Pool->Available.Num());
	Obj->SetNumberField(TEXT("in_use_count"), Pool->InUse.Num());
	Obj->SetNumberField(TEXT("max_size"), Pool->MaxSize);
	Obj->SetNumberField(TEXT("total_ever_spawned"), Pool->TotalEverSpawned);

	return ActorPoolJsonObjectToString(Obj);
}

bool UActorPoolService::DestroyPool(const FString& PoolName)
{
	ActorPoolWarnIfNotGameThread(TEXT("DestroyPool"));

	FPool Pool;
	if (!Pools.RemoveAndCopyValue(PoolName, Pool))
	{
		UE_LOG(LogRevvyActorPool, Warning,
			TEXT("DestroyPool: pool '%s' is not registered."), *PoolName);
		return false;
	}

	int32 DestroyedCount = 0;

	auto DestroyList = [&DestroyedCount](TArray<TWeakObjectPtr<AActor>>& List)
	{
		for (const TWeakObjectPtr<AActor>& WeakActor : List)
		{
			if (AActor* Actor = WeakActor.Get())
			{
				if (UWorld* World = Actor->GetWorld())
				{
					World->DestroyActor(Actor);
					DestroyedCount++;
				}
			}
		}
		List.Reset();
	};

	DestroyList(Pool.Available);
	DestroyList(Pool.InUse);

	UE_LOG(LogRevvyActorPool, Log,
		TEXT("DestroyPool: '%s' removed (%d actors destroyed)."),
		*PoolName, DestroyedCount);
	return true;
}
