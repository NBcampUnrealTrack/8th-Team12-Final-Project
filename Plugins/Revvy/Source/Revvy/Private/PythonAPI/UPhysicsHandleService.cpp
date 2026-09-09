// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPhysicsHandleService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPhysicsHandle, Log, All);

#include "GameFramework/Actor.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Static state (anonymous namespace — hidden from other TUs)
// =================================================================

namespace
{
	/** Named-handle registry, keyed by user-supplied handle name. */
	static TMap<FString, TWeakObjectPtr<UPhysicsHandleComponent>> Handles;

	/** Emit a Warning if not on the GameThread — registry access is not thread-safe. */
	void PhysicsHandleWarnIfNotGameThread(const TCHAR* Op)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogRevvyPhysicsHandle, Warning,
				TEXT("UPhysicsHandleService::%s called off GameThread — registry access is not thread-safe."),
				Op);
		}
	}

	/** Fetch a live UPhysicsHandleComponent from the registry, or log + return nullptr. */
	UPhysicsHandleComponent* GetLiveHandle(const FString& HandleName, const TCHAR* Op)
	{
		TWeakObjectPtr<UPhysicsHandleComponent>* WeakHandle = Handles.Find(HandleName);
		if (!WeakHandle)
		{
			UE_LOG(LogRevvyPhysicsHandle, Warning,
				TEXT("%s: handle '%s' is not registered."), Op, *HandleName);
			return nullptr;
		}

		UPhysicsHandleComponent* Handle = WeakHandle->Get();
		if (!Handle)
		{
			UE_LOG(LogRevvyPhysicsHandle, Warning,
				TEXT("%s: handle '%s' has been garbage-collected (owner destroyed)."),
				Op, *HandleName);
			Handles.Remove(HandleName);
			return nullptr;
		}

		return Handle;
	}
}

// =================================================================
// Lifecycle
// =================================================================

FString UPhysicsHandleService::SpawnPhysicsHandle(
	const FString& OwnerActorNameOrPath,
	const FString& HandleName)
{
	PhysicsHandleWarnIfNotGameThread(TEXT("SpawnPhysicsHandle"));

	if (HandleName.IsEmpty())
	{
		UE_LOG(LogRevvyPhysicsHandle, Warning,
			TEXT("SpawnPhysicsHandle: HandleName must be non-empty."));
		return FString();
	}

	if (Handles.Contains(HandleName))
	{
		// If the registered entry is stale we allow re-spawn; otherwise reject.
		if (Handles[HandleName].IsValid())
		{
			UE_LOG(LogRevvyPhysicsHandle, Warning,
				TEXT("SpawnPhysicsHandle: handle '%s' is already registered."), *HandleName);
			return FString();
		}
		Handles.Remove(HandleName);
	}

	AActor* Owner = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, OwnerActorNameOrPath);
	if (!Owner)
	{
		UE_LOG(LogRevvyPhysicsHandle, Warning,
			TEXT("SpawnPhysicsHandle: could not resolve owner actor '%s'."),
			*OwnerActorNameOrPath);
		return FString();
	}

	UPhysicsHandleComponent* Handle = NewObject<UPhysicsHandleComponent>(Owner);
	if (!Handle)
	{
		UE_LOG(LogRevvyPhysicsHandle, Warning,
			TEXT("SpawnPhysicsHandle: NewObject<UPhysicsHandleComponent> failed for owner '%s'."),
			*Owner->GetActorLabel());
		return FString();
	}

	Owner->AddInstanceComponent(Handle);
	Handle->RegisterComponent();

	Handles.Add(HandleName, Handle);

	UE_LOG(LogRevvyPhysicsHandle, Log,
		TEXT("SpawnPhysicsHandle: '%s' registered on owner '%s'."),
		*HandleName, *Owner->GetActorLabel());
	return HandleName;
}

// =================================================================
// Grab
// =================================================================

bool UPhysicsHandleService::GrabObject(
	const FString& HandleName,
	const FString& TargetActorNameOrPath,
	const FString& BoneName,
	FVector GrabLocation,
	FRotator GrabRotation)
{
	PhysicsHandleWarnIfNotGameThread(TEXT("GrabObject"));

	UPhysicsHandleComponent* Handle = GetLiveHandle(HandleName, TEXT("GrabObject"));
	if (!Handle)
	{
		return false;
	}

	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyPhysicsHandle, Warning,
			TEXT("GrabObject: could not resolve target actor '%s'."),
			*TargetActorNameOrPath);
		return false;
	}

	UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Target->GetRootComponent());
	if (!Prim)
	{
		UE_LOG(LogRevvyPhysicsHandle, Warning,
			TEXT("GrabObject: target '%s' root component is not a UPrimitiveComponent."),
			*Target->GetActorLabel());
		return false;
	}

	Handle->GrabComponentAtLocationWithRotation(Prim, FName(*BoneName), GrabLocation, GrabRotation);

	UE_LOG(LogRevvyPhysicsHandle, Verbose,
		TEXT("GrabObject: handle '%s' grabbed '%s' (bone='%s')."),
		*HandleName, *Target->GetActorLabel(), *BoneName);
	return true;
}

// =================================================================
// Move
// =================================================================

bool UPhysicsHandleService::MoveGrabbedObject(
	const FString& HandleName,
	FVector NewLocation,
	FRotator NewRotation)
{
	PhysicsHandleWarnIfNotGameThread(TEXT("MoveGrabbedObject"));

	UPhysicsHandleComponent* Handle = GetLiveHandle(HandleName, TEXT("MoveGrabbedObject"));
	if (!Handle)
	{
		return false;
	}

	Handle->SetTargetLocationAndRotation(NewLocation, NewRotation);

	UE_LOG(LogRevvyPhysicsHandle, Verbose,
		TEXT("MoveGrabbedObject: handle '%s' target updated."), *HandleName);
	return true;
}

// =================================================================
// Release
// =================================================================

bool UPhysicsHandleService::ReleaseObject(const FString& HandleName)
{
	PhysicsHandleWarnIfNotGameThread(TEXT("ReleaseObject"));

	UPhysicsHandleComponent* Handle = GetLiveHandle(HandleName, TEXT("ReleaseObject"));
	if (!Handle)
	{
		return false;
	}

	Handle->ReleaseComponent();

	UE_LOG(LogRevvyPhysicsHandle, Verbose,
		TEXT("ReleaseObject: handle '%s' released grabbed component."), *HandleName);
	return true;
}
