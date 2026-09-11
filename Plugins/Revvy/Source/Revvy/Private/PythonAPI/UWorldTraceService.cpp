// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UWorldTraceService.h"
#include "Engine/World.h"
#include "Editor.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogUWorldTraceService, Log, All);

// ============================================
// Helper Functions
// ============================================

UWorld* UWorldTraceService::GetEditorWorld()
{
	if (!GEditor)
	{
		UE_LOG(LogUWorldTraceService, Warning, TEXT("GetEditorWorld: GEditor is not available"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogUWorldTraceService, Warning, TEXT("GetEditorWorld: No editor world is currently available"));
	}
	return World;
}

ECollisionChannel UWorldTraceService::ResolveTraceChannel(const FString& TraceChannel)
{
	if (TraceChannel.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
	{
		return ECC_Camera;
	}
	if (TraceChannel.Equals(TEXT("WorldStatic"), ESearchCase::IgnoreCase))
	{
		return ECC_WorldStatic;
	}
	if (TraceChannel.Equals(TEXT("WorldDynamic"), ESearchCase::IgnoreCase))
	{
		return ECC_WorldDynamic;
	}
	if (TraceChannel.Equals(TEXT("Pawn"), ESearchCase::IgnoreCase))
	{
		return ECC_Pawn;
	}

	// Default (also handles explicit "Visibility")
	if (!TraceChannel.IsEmpty() && !TraceChannel.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogUWorldTraceService, Warning,
			TEXT("ResolveTraceChannel: Unknown channel '%s', defaulting to Visibility"), *TraceChannel);
	}
	return ECC_Visibility;
}

FTraceHitInfo UWorldTraceService::MakeHitInfo(const FHitResult& Hit)
{
	FTraceHitInfo Info;
	Info.bBlockingHit = Hit.bBlockingHit;
	Info.ImpactPoint = Hit.ImpactPoint;
	Info.ImpactNormal = Hit.ImpactNormal;
	Info.Distance = Hit.Distance;

	if (const AActor* HitActor = Hit.GetActor())
	{
#if WITH_EDITOR
		Info.HitActorName = HitActor->GetActorLabel();
#else
		Info.HitActorName = HitActor->GetName();
#endif
		if (Info.HitActorName.IsEmpty())
		{
			Info.HitActorName = HitActor->GetName();
		}
	}

	if (const UPrimitiveComponent* HitComponent = Hit.GetComponent())
	{
		Info.HitComponentName = HitComponent->GetName();
	}

	return Info;
}

// ============================================
// Trace Actions
// ============================================

FTraceHitInfo UWorldTraceService::LineTrace(
	const FVector& Start,
	const FVector& End,
	const FString& TraceChannel,
	bool bTraceComplex)
{
	FTraceHitInfo Result;

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Result;
	}

	const ECollisionChannel Channel = ResolveTraceChannel(TraceChannel);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RevvyWorldLineTrace), bTraceComplex);
	QueryParams.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Channel, QueryParams);

	if (bHit)
	{
		Result = MakeHitInfo(Hit);
	}

	return Result;
}

TArray<FTraceHitInfo> UWorldTraceService::MultiLineTrace(
	const FVector& Start,
	const FVector& End,
	const FString& TraceChannel)
{
	TArray<FTraceHitInfo> Results;

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Results;
	}

	const ECollisionChannel Channel = ResolveTraceChannel(TraceChannel);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RevvyWorldMultiLineTrace), false);
	QueryParams.bReturnPhysicalMaterial = false;

	TArray<FHitResult> Hits;
	const bool bHit = World->LineTraceMultiByChannel(Hits, Start, End, Channel, QueryParams);

	if (bHit)
	{
		Results.Reserve(Hits.Num());
		for (const FHitResult& Hit : Hits)
		{
			Results.Add(MakeHitInfo(Hit));
		}
	}

	return Results;
}

FTraceHitInfo UWorldTraceService::SphereTrace(
	const FVector& Start,
	const FVector& End,
	float Radius,
	const FString& TraceChannel)
{
	FTraceHitInfo Result;

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Result;
	}

	if (Radius <= 0.0f)
	{
		UE_LOG(LogUWorldTraceService, Warning, TEXT("SphereTrace: Radius must be > 0 (got %f)"), Radius);
		return Result;
	}

	const ECollisionChannel Channel = ResolveTraceChannel(TraceChannel);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(RevvyWorldSphereTrace), false);
	QueryParams.bReturnPhysicalMaterial = false;

	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);

	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(
		Hit, Start, End, FQuat::Identity, Channel, SphereShape, QueryParams);

	if (bHit)
	{
		Result = MakeHitInfo(Hit);
	}

	return Result;
}
