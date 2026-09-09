// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UProjectileMovementService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyProjectile, Log, All);

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SceneComponent.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	/** Return the first UProjectileMovementComponent owned by Actor, or nullptr. */
	UProjectileMovementComponent* FindProjectileMovementComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		return Actor->FindComponentByClass<UProjectileMovementComponent>();
	}
}

// =================================================================
// Spawn
// =================================================================

FString UProjectileMovementService::SpawnProjectile(
	const FString& ProjectileClassPath,
	FVector Location,
	FVector Direction,
	float InitialSpeed,
	float GravityScale)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyProjectile, Warning, TEXT("SpawnProjectile: no editor world."));
		return FString();
	}

	UClass* Class = URevvyCommonHelpers::LoadClassWithFallback(ProjectileClassPath, AActor::StaticClass());
	if (!Class)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SpawnProjectile: failed to load actor class at '%s'."), *ProjectileClassPath);
		return FString();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FRotator SpawnRotation = Direction.IsNearlyZero()
		? FRotator::ZeroRotator
		: Direction.Rotation();

	AActor* Spawned = World->SpawnActor<AActor>(
		Class,
		FTransform(SpawnRotation, Location),
		SpawnParams);

	if (!Spawned)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SpawnProjectile: SpawnActor returned null for class '%s'."),
			*ProjectileClassPath);
		return FString();
	}

	if (UProjectileMovementComponent* Comp = FindProjectileMovementComponent(Spawned))
	{
		Comp->InitialSpeed = InitialSpeed;
		Comp->MaxSpeed = InitialSpeed;
		Comp->Velocity = Direction.GetSafeNormal() * InitialSpeed;
		Comp->ProjectileGravityScale = GravityScale;
		Comp->Activate(true);

		UE_LOG(LogRevvyProjectile, Log,
			TEXT("SpawnProjectile: '%s' at %s (speed=%f, gravity=%f) -> '%s'."),
			*ProjectileClassPath, *Location.ToString(),
			InitialSpeed, GravityScale, *Spawned->GetActorLabel());
	}
	else
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SpawnProjectile: spawned '%s' has no UProjectileMovementComponent; velocity/gravity ignored."),
			*Spawned->GetActorLabel());
	}

	return Spawned->GetActorLabel();
}

// =================================================================
// Homing
// =================================================================

bool UProjectileMovementService::SetProjectileHomingTarget(
	const FString& ActorNameOrPath,
	const FString& TargetActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileHomingTarget: projectile actor '%s' not found."),
			*ActorNameOrPath);
		return false;
	}

	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileHomingTarget: target actor '%s' not found."),
			*TargetActorNameOrPath);
		return false;
	}

	UProjectileMovementComponent* Comp = FindProjectileMovementComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileHomingTarget: no UProjectileMovementComponent on '%s'."),
			*ActorNameOrPath);
		return false;
	}

	USceneComponent* TargetRoot = Target->GetRootComponent();
	if (!TargetRoot)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileHomingTarget: target '%s' has no root scene component."),
			*TargetActorNameOrPath);
		return false;
	}

	Comp->HomingTargetComponent = TargetRoot;
	Comp->bIsHomingProjectile = true;

	UE_LOG(LogRevvyProjectile, Log,
		TEXT("SetProjectileHomingTarget: '%s' -> '%s'."),
		*ActorNameOrPath, *TargetActorNameOrPath);
	return true;
}

// =================================================================
// Control
// =================================================================

bool UProjectileMovementService::SetProjectileVelocity(
	const FString& ActorNameOrPath,
	FVector NewVelocity)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileVelocity: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UProjectileMovementComponent* Comp = FindProjectileMovementComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("SetProjectileVelocity: no UProjectileMovementComponent on '%s'."),
			*ActorNameOrPath);
		return false;
	}

	Comp->Velocity = NewVelocity;

	UE_LOG(LogRevvyProjectile, Verbose,
		TEXT("SetProjectileVelocity: '%s' = %s"),
		*ActorNameOrPath, *NewVelocity.ToString());
	return true;
}

bool UProjectileMovementService::StopProjectileMovement(const FString& ActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("StopProjectileMovement: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UProjectileMovementComponent* Comp = FindProjectileMovementComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyProjectile, Warning,
			TEXT("StopProjectileMovement: no UProjectileMovementComponent on '%s'."),
			*ActorNameOrPath);
		return false;
	}

	Comp->StopMovementImmediately();
	Comp->Deactivate();

	UE_LOG(LogRevvyProjectile, Log,
		TEXT("StopProjectileMovement: '%s' stopped."), *ActorNameOrPath);
	return true;
}
