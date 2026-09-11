// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UNiagaraComponentService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyNiagaraComp, Log, All);

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "UObject/SoftObjectPath.h"
#include "EditorAssetLibrary.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// Niagara runtime
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraActor.h"

#include "Components/SceneComponent.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	/** Load a UNiagaraSystem from a package path. */
	UNiagaraSystem* LoadNiagaraSystemChecked(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			UE_LOG(LogRevvyNiagaraComp, Warning, TEXT("Niagara system path is empty."));
			return nullptr;
		}

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Loaded)
		{
			// Fallback: soft object path resolve / LoadObject
			Loaded = FSoftObjectPath(AssetPath).TryLoad();
		}

		UNiagaraSystem* System = Cast<UNiagaraSystem>(Loaded);
		if (!System)
		{
			UE_LOG(LogRevvyNiagaraComp, Warning,
				TEXT("Failed to load UNiagaraSystem at '%s'."), *AssetPath);
			return nullptr;
		}
		return System;
	}

	/** Return the first UNiagaraComponent owned by Actor, or nullptr. */
	UNiagaraComponent* FindNiagaraComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		return Actor->FindComponentByClass<UNiagaraComponent>();
	}
}

// =================================================================
// Spawn
// =================================================================

FString UNiagaraComponentService::SpawnNiagaraAtLocation(
	const FString& SystemAssetPath,
	FVector Location,
	FRotator Rotation,
	bool bAutoDestroy)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning, TEXT("SpawnNiagaraAtLocation: no editor world."));
		return FString();
	}

	UNiagaraSystem* System = LoadNiagaraSystemChecked(SystemAssetPath);
	if (!System)
	{
		return FString();
	}

	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World,
		System,
		Location,
		Rotation,
		FVector(1.f),
		bAutoDestroy,
		/*bAutoActivate=*/ true,
		ENCPoolMethod::None,
		/*bPreCullCheck=*/ true);

	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SpawnNiagaraAtLocation: SpawnSystemAtLocation returned null for '%s'."),
			*SystemAssetPath);
		return FString();
	}

	AActor* Owner = Comp->GetOwner();
	if (!Owner)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SpawnNiagaraAtLocation: component has no owning actor."));
		return FString();
	}

	UE_LOG(LogRevvyNiagaraComp, Log,
		TEXT("Spawned Niagara '%s' at %s -> owner '%s'."),
		*SystemAssetPath, *Location.ToString(), *Owner->GetActorLabel());

	return Owner->GetActorLabel();
}

FString UNiagaraComponentService::SpawnNiagaraAttached(
	const FString& SystemAssetPath,
	const FString& TargetActorNameOrPath,
	const FString& SocketName,
	bool bAutoDestroy)
{
	UNiagaraSystem* System = LoadNiagaraSystemChecked(SystemAssetPath);
	if (!System)
	{
		return FString();
	}

	AActor* TargetActor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!TargetActor)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SpawnNiagaraAttached: target actor '%s' not found."),
			*TargetActorNameOrPath);
		return FString();
	}

	// Prefer the actor root; a socket lookup on the root component is usually
	// enough for skeletal / static meshes reparented as root.
	USceneComponent* AttachTo = TargetActor->GetRootComponent();
	if (!AttachTo)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SpawnNiagaraAttached: target actor '%s' has no root scene component."),
			*TargetActorNameOrPath);
		return FString();
	}

	const FName Socket = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);

	UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System,
		AttachTo,
		Socket,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		bAutoDestroy,
		/*bAutoActivate=*/ true,
		ENCPoolMethod::None,
		/*bPreCullCheck=*/ true);

	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SpawnNiagaraAttached: SpawnSystemAttached returned null for '%s' on '%s'."),
			*SystemAssetPath, *TargetActorNameOrPath);
		return FString();
	}

	AActor* Owner = Comp->GetOwner();
	if (!Owner)
	{
		// SpawnSystemAttached usually parents the component under the target
		// actor rather than creating a fresh ANiagaraActor — fall back to the
		// target label in that case.
		UE_LOG(LogRevvyNiagaraComp, Log,
			TEXT("SpawnNiagaraAttached: no distinct owner, returning target label '%s'."),
			*TargetActor->GetActorLabel());
		return TargetActor->GetActorLabel();
	}

	UE_LOG(LogRevvyNiagaraComp, Log,
		TEXT("Spawned Niagara '%s' attached to '%s' (socket '%s') -> owner '%s'."),
		*SystemAssetPath, *TargetActor->GetActorLabel(),
		*Socket.ToString(), *Owner->GetActorLabel());

	return Owner->GetActorLabel();
}

// =================================================================
// User Parameters
// =================================================================

bool UNiagaraComponentService::SetNiagaraUserParamFloat(
	const FString& ActorNameOrPath,
	const FString& ParamName,
	float Value)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamFloat: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UNiagaraComponent* Comp = FindNiagaraComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamFloat: no UNiagaraComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	Comp->SetVariableFloat(FName(*ParamName), Value);
	UE_LOG(LogRevvyNiagaraComp, Verbose,
		TEXT("SetNiagaraUserParamFloat: '%s'.'%s' = %f"),
		*ActorNameOrPath, *ParamName, Value);
	return true;
}

bool UNiagaraComponentService::SetNiagaraUserParamVector(
	const FString& ActorNameOrPath,
	const FString& ParamName,
	FVector Value)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamVector: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UNiagaraComponent* Comp = FindNiagaraComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamVector: no UNiagaraComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	Comp->SetVariableVec3(FName(*ParamName), Value);
	UE_LOG(LogRevvyNiagaraComp, Verbose,
		TEXT("SetNiagaraUserParamVector: '%s'.'%s' = %s"),
		*ActorNameOrPath, *ParamName, *Value.ToString());
	return true;
}

bool UNiagaraComponentService::SetNiagaraUserParamColor(
	const FString& ActorNameOrPath,
	const FString& ParamName,
	FLinearColor Value)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamColor: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UNiagaraComponent* Comp = FindNiagaraComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("SetNiagaraUserParamColor: no UNiagaraComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	Comp->SetVariableLinearColor(FName(*ParamName), Value);
	UE_LOG(LogRevvyNiagaraComp, Verbose,
		TEXT("SetNiagaraUserParamColor: '%s'.'%s' = %s"),
		*ActorNameOrPath, *ParamName, *Value.ToString());
	return true;
}

// =================================================================
// Control
// =================================================================

bool UNiagaraComponentService::StopNiagaraComponent(const FString& ActorNameOrPath, bool bImmediate)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("StopNiagaraComponent: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UNiagaraComponent* Comp = FindNiagaraComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyNiagaraComp, Warning,
			TEXT("StopNiagaraComponent: no UNiagaraComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	if (bImmediate)
	{
		Comp->DeactivateImmediate();
	}
	else
	{
		Comp->Deactivate();
	}

	UE_LOG(LogRevvyNiagaraComp, Log,
		TEXT("StopNiagaraComponent: '%s' (immediate=%d)."),
		*ActorNameOrPath, bImmediate ? 1 : 0);
	return true;
}
