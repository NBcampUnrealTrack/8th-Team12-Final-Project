// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UDamageSystemService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyDamage, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

// Damage dispatch
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"

// Property reflection (for GetActorHealth)
#include "UObject/UnrealType.h"

// Shared Revvy helpers (actor lookup, world access, class loading)
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/**
	 * Resolve a UDamageType subclass by path. Falls back to
	 * UDamageType::StaticClass() when the path is empty or the class fails
	 * to load / is not a UDamageType subclass.
	 *
	 * Delegates class loading (with _C fallback + base-class validation)
	 * to URevvyCommonHelpers::LoadClassWithFallback.
	 */
	TSubclassOf<UDamageType> ResolveDamageTypeClass(const FString& DamageTypeClassPath)
	{
		if (DamageTypeClassPath.IsEmpty())
		{
			return UDamageType::StaticClass();
		}

		UClass* Loaded = URevvyCommonHelpers::LoadClassWithFallback(
			DamageTypeClassPath, UDamageType::StaticClass());
		if (!Loaded)
		{
			UE_LOG(LogRevvyDamage, Warning,
				TEXT("ResolveDamageTypeClass: failed to resolve '%s' as UDamageType; falling back to UDamageType::StaticClass()"),
				*DamageTypeClassPath);
			return UDamageType::StaticClass();
		}

		return Loaded;
	}

	/**
	 * Resolve the instigator controller from an instigator actor. If the
	 * actor is a Pawn, uses GetInstigatorController(). Otherwise attempts to
	 * cast the actor directly to AController. Returns nullptr for null input.
	 */
	AController* ResolveInstigatorController(AActor* InstigatorActor)
	{
		if (!InstigatorActor)
		{
			return nullptr;
		}
		return InstigatorActor->GetInstigatorController();
	}
}

// =================================================================
// Apply
// =================================================================

float UDamageSystemService::ApplyDamage(
	const FString& TargetActorNameOrPath,
	float BaseDamage,
	const FString& InstigatorActorNameOrPath,
	const FString& DamageCauserActorNameOrPath,
	const FString& DamageTypeClassPath)
{
	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("ApplyDamage: Target '%s' not found"),
			*TargetActorNameOrPath);
		return 0.0f;
	}

	AActor* Instigator = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, InstigatorActorNameOrPath);
	AActor* Causer     = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, DamageCauserActorNameOrPath);

	TSubclassOf<UDamageType> DamageTypeClass = ResolveDamageTypeClass(DamageTypeClassPath);
	AController* InstigController            = ResolveInstigatorController(Instigator);

	const float Actual = UGameplayStatics::ApplyDamage(
		Target,
		BaseDamage,
		InstigController,
		Causer,
		DamageTypeClass);

	UE_LOG(LogRevvyDamage, Log,
		TEXT("ApplyDamage: Target='%s' Base=%.2f Actual=%.2f"),
		*Target->GetName(), BaseDamage, Actual);

	return Actual;
}

float UDamageSystemService::ApplyPointDamage(
	const FString& TargetActorNameOrPath,
	float BaseDamage,
	FVector HitFromDirection,
	FVector HitLocation,
	const FString& InstigatorActorNameOrPath,
	const FString& CauserActorNameOrPath,
	const FString& DamageTypeClassPath)
{
	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("ApplyPointDamage: Target '%s' not found"),
			*TargetActorNameOrPath);
		return 0.0f;
	}

	AActor* Instigator = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, InstigatorActorNameOrPath);
	AActor* Causer     = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CauserActorNameOrPath);

	TSubclassOf<UDamageType> DamageTypeClass = ResolveDamageTypeClass(DamageTypeClassPath);
	AController* InstigController            = ResolveInstigatorController(Instigator);

	FHitResult HitInfo;
	HitInfo.Location = HitLocation;
	HitInfo.ImpactPoint = HitLocation;
	HitInfo.HitObjectHandle = FActorInstanceHandle(Target);

	const float Actual = UGameplayStatics::ApplyPointDamage(
		Target,
		BaseDamage,
		HitFromDirection,
		HitInfo,
		InstigController,
		Causer,
		DamageTypeClass);

	UE_LOG(LogRevvyDamage, Log,
		TEXT("ApplyPointDamage: Target='%s' Base=%.2f Actual=%.2f"),
		*Target->GetName(), BaseDamage, Actual);

	return Actual;
}

bool UDamageSystemService::ApplyRadialDamage(
	FVector Origin,
	float BaseDamage,
	float DamageRadius,
	const TArray<FString>& IgnoreActorNamesOrPaths,
	const FString& InstigatorActorNameOrPath,
	const FString& CauserActorNameOrPath,
	const FString& DamageTypeClassPath,
	bool bDoFullDamage)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("ApplyRadialDamage: no editor world available"));
		return false;
	}

	TArray<AActor*> IgnoreActors;
	IgnoreActors.Reserve(IgnoreActorNamesOrPaths.Num());
	for (const FString& Entry : IgnoreActorNamesOrPaths)
	{
		if (AActor* Resolved = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, Entry))
		{
			IgnoreActors.AddUnique(Resolved);
		}
	}

	AActor* Instigator = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, InstigatorActorNameOrPath);
	AActor* Causer     = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CauserActorNameOrPath);

	TSubclassOf<UDamageType> DamageTypeClass = ResolveDamageTypeClass(DamageTypeClassPath);
	AController* InstigController            = ResolveInstigatorController(Instigator);

	UGameplayStatics::ApplyRadialDamage(
		World,
		BaseDamage,
		Origin,
		DamageRadius,
		DamageTypeClass,
		IgnoreActors,
		Causer,
		InstigController,
		bDoFullDamage);

	UE_LOG(LogRevvyDamage, Log,
		TEXT("ApplyRadialDamage: Origin=%s Base=%.2f Radius=%.2f Ignored=%d FullDamage=%d"),
		*Origin.ToString(), BaseDamage, DamageRadius, IgnoreActors.Num(), bDoFullDamage ? 1 : 0);

	return true;
}

// =================================================================
// Query
// =================================================================

float UDamageSystemService::GetActorHealth(const FString& ActorNameOrPath)
{
	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("GetActorHealth: Actor '%s' not found"),
			*ActorNameOrPath);
		return -1.0f;
	}

	// Best-effort: locate a float UPROPERTY named "Health" via reflection.
	UClass* Cls = Target->GetClass();
	if (!Cls)
	{
		return -1.0f;
	}

	FProperty* Property = Cls->FindPropertyByName(TEXT("Health"));
	if (!Property)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("GetActorHealth: no UPROPERTY named 'Health' on '%s' (class '%s')"),
			*Target->GetName(), *Cls->GetName());
		return -1.0f;
	}

	FFloatProperty* FloatProp = CastField<FFloatProperty>(Property);
	if (!FloatProp)
	{
		UE_LOG(LogRevvyDamage, Warning,
			TEXT("GetActorHealth: 'Health' on '%s' is not a float property"),
			*Target->GetName());
		return -1.0f;
	}

	const float Value = FloatProp->GetPropertyValue_InContainer(Target);
	return Value;
}
