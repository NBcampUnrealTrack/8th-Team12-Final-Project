// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/URuntimeCollisionQueryService.h"

// Engine
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

// Collision
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Kismet/KismetSystemLibrary.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCollision, Log, All);

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Resolve a set of ignore actors from string identifiers. */
	TArray<AActor*> ResolveIgnoreActors(const TArray<FString>& Entries)
	{
		TArray<AActor*> Out;
		Out.Reserve(Entries.Num());
		for (const FString& Entry : Entries)
		{
			if (AActor* Resolved = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, Entry))
			{
				Out.AddUnique(Resolved);
			}
		}
		return Out;
	}

	/** Map a channel name string to an ECollisionChannel (defaults to Visibility). */
	ECollisionChannel ResolveTraceChannel(const FString& TraceChannelStr)
	{
		if (TraceChannelStr.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))
		{
			return ECC_Camera;
		}
		if (TraceChannelStr.Equals(TEXT("WorldStatic"), ESearchCase::IgnoreCase))
		{
			return ECC_WorldStatic;
		}
		if (TraceChannelStr.Equals(TEXT("WorldDynamic"), ESearchCase::IgnoreCase))
		{
			return ECC_WorldDynamic;
		}
		if (TraceChannelStr.Equals(TEXT("Pawn"), ESearchCase::IgnoreCase))
		{
			return ECC_Pawn;
		}
		if (TraceChannelStr.Equals(TEXT("PhysicsBody"), ESearchCase::IgnoreCase))
		{
			return ECC_PhysicsBody;
		}

		if (!TraceChannelStr.IsEmpty() &&
			!TraceChannelStr.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogRevvyCollision, Warning,
				TEXT("ResolveTraceChannel: unknown channel '%s', defaulting to Visibility"),
				*TraceChannelStr);
		}
		return ECC_Visibility;
	}

	/** Return the actor display label (falls back to GetName). */
	FString ResolveActorLabel(const AActor* Actor)
	{
		if (!Actor)
		{
			return FString();
		}
#if WITH_EDITOR
		const FString Label = Actor->GetActorLabel();
		return Label.IsEmpty() ? Actor->GetName() : Label;
#else
		return Actor->GetName();
#endif
	}

	/** Build a {x,y,z} JSON object from an FVector. */
	TSharedPtr<FJsonObject> VectorToJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	/** Build a single-hit JSON object (as used by both single / multi trace). */
	TSharedPtr<FJsonObject> HitResultToJson(const FHitResult& Hit)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("actor_label"), ResolveActorLabel(Hit.GetActor()));
		Obj->SetObjectField(TEXT("impact_point"),  VectorToJson(Hit.ImpactPoint));
		Obj->SetObjectField(TEXT("impact_normal"), VectorToJson(Hit.ImpactNormal));
		Obj->SetNumberField(TEXT("distance"), Hit.Distance);
		return Obj;
	}

	/** Serialize a JSON object to string. */
	FString RuntimeCollisionJsonObjectToString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Result;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Result;
	}

	/** Serialize a JSON array to string. */
	FString RuntimeCollisionJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Result;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
		FJsonSerializer::Serialize(Arr, Writer);
		return Result;
	}

	/** Build the default "no hit" response for LineTraceSingle. */
	FString MakeNoHitJson(const FString& Error = FString())
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("hit"), false);
		Obj->SetStringField(TEXT("actor_label"), TEXT(""));
		Obj->SetObjectField(TEXT("impact_point"),  VectorToJson(FVector::ZeroVector));
		Obj->SetObjectField(TEXT("impact_normal"), VectorToJson(FVector::ZeroVector));
		Obj->SetNumberField(TEXT("distance"), 0.0);
		if (!Error.IsEmpty())
		{
			Obj->SetStringField(TEXT("error"), Error);
		}
		return RuntimeCollisionJsonObjectToString(Obj);
	}

	/**
	 * Build the "all common object types" list used by overlap queries.
	 * Covers WorldStatic / WorldDynamic / Pawn / PhysicsBody which is what
	 * the vast majority of gameplay actors register against.
	 */
	TArray<TEnumAsByte<EObjectTypeQuery>> BuildDefaultOverlapObjectTypes()
	{
		TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
		ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
		ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
		ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
		ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_PhysicsBody));
		return ObjectTypes;
	}
}

// =================================================================
// Line Traces
// =================================================================

FString URuntimeCollisionQueryService::LineTraceSingle(
	FVector Start,
	FVector End,
	const FString& TraceChannelStr)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyCollision, Warning,
			TEXT("LineTraceSingle: no editor world available"));
		return MakeNoHitJson(TEXT("no_world"));
	}

	const ECollisionChannel Channel = ResolveTraceChannel(TraceChannelStr);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RevvyRuntimeLineTraceSingle), false);
	Params.bReturnPhysicalMaterial = false;

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Channel, Params);

	if (!bHit)
	{
		return MakeNoHitJson();
	}

	TSharedPtr<FJsonObject> Obj = HitResultToJson(Hit);
	Obj->SetBoolField(TEXT("hit"), true);

	UE_LOG(LogRevvyCollision, Verbose,
		TEXT("LineTraceSingle: Actor='%s' Distance=%.2f"),
		*ResolveActorLabel(Hit.GetActor()), Hit.Distance);

	return RuntimeCollisionJsonObjectToString(Obj);
}

FString URuntimeCollisionQueryService::MultiLineTraceByChannel(
	FVector Start,
	FVector End,
	const FString& TraceChannelStr,
	const TArray<FString>& IgnoreActorNamesOrPaths)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyCollision, Warning,
			TEXT("MultiLineTraceByChannel: no editor world available"));
		return TEXT("[]");
	}

	const ECollisionChannel Channel = ResolveTraceChannel(TraceChannelStr);
	const TArray<AActor*> IgnoreActors = ResolveIgnoreActors(IgnoreActorNamesOrPaths);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RevvyRuntimeMultiLineTrace), false);
	Params.bReturnPhysicalMaterial = false;
	Params.AddIgnoredActors(IgnoreActors);

	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, Channel, Params);

	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Hits.Num());
	for (const FHitResult& Hit : Hits)
	{
		Out.Add(MakeShared<FJsonValueObject>(HitResultToJson(Hit)));
	}

	UE_LOG(LogRevvyCollision, Verbose,
		TEXT("MultiLineTraceByChannel: hits=%d ignored=%d"),
		Hits.Num(), IgnoreActors.Num());

	return RuntimeCollisionJsonArrayToString(Out);
}

// =================================================================
// Overlaps
// =================================================================

FString URuntimeCollisionQueryService::SphereOverlapActors(
	FVector Origin,
	float Radius,
	const TArray<FString>& IgnoreActorNamesOrPaths)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyCollision, Warning,
			TEXT("SphereOverlapActors: no editor world available"));
		return TEXT("[]");
	}

	if (Radius <= 0.0f)
	{
		UE_LOG(LogRevvyCollision, Warning,
			TEXT("SphereOverlapActors: radius must be > 0 (got %f)"), Radius);
		return TEXT("[]");
	}

	const TArray<AActor*> IgnoreActors = ResolveIgnoreActors(IgnoreActorNamesOrPaths);
	const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes = BuildDefaultOverlapObjectTypes();

	TArray<AActor*> OverlappingActors;
	UKismetSystemLibrary::SphereOverlapActors(
		World,
		Origin,
		Radius,
		ObjectTypes,
		/*ActorClassFilter*/ nullptr,
		IgnoreActors,
		OverlappingActors);

	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(OverlappingActors.Num());
	for (AActor* Actor : OverlappingActors)
	{
		if (!Actor) { continue; }
		const float Distance = FVector::Dist(Actor->GetActorLocation(), Origin);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("actor_label"), ResolveActorLabel(Actor));
		Obj->SetNumberField(TEXT("distance_from_origin"), Distance);
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	UE_LOG(LogRevvyCollision, Verbose,
		TEXT("SphereOverlapActors: Origin=%s Radius=%.2f found=%d ignored=%d"),
		*Origin.ToString(), Radius, OverlappingActors.Num(), IgnoreActors.Num());

	return RuntimeCollisionJsonArrayToString(Out);
}

FString URuntimeCollisionQueryService::BoxOverlapActors(
	FVector Origin,
	FVector Extent,
	FRotator Rotation,
	const TArray<FString>& IgnoreActorNamesOrPaths)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyCollision, Warning,
			TEXT("BoxOverlapActors: no editor world available"));
		return TEXT("[]");
	}

	const TArray<AActor*> IgnoreActors = ResolveIgnoreActors(IgnoreActorNamesOrPaths);

	// UKismetSystemLibrary::BoxOverlapActors is axis-aligned only, so we
	// use World->OverlapMultiByObjectType with an oriented FCollisionShape
	// and hand-roll the unique-actor aggregation.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RevvyRuntimeBoxOverlap), false);
	Params.AddIgnoredActors(IgnoreActors);

	const FCollisionShape BoxShape = FCollisionShape::MakeBox(Extent);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		Rotation.Quaternion(),
		ObjectParams,
		BoxShape,
		Params);

	TSet<AActor*> Seen;
	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Overlaps.Num());
	for (const FOverlapResult& R : Overlaps)
	{
		AActor* Actor = R.GetActor();
		if (!Actor || Seen.Contains(Actor)) { continue; }
		Seen.Add(Actor);

		const float Distance = FVector::Dist(Actor->GetActorLocation(), Origin);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("actor_label"), ResolveActorLabel(Actor));
		Obj->SetNumberField(TEXT("distance_from_origin"), Distance);
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	UE_LOG(LogRevvyCollision, Verbose,
		TEXT("BoxOverlapActors: Origin=%s Extent=%s Rot=%s found=%d ignored=%d"),
		*Origin.ToString(), *Extent.ToString(), *Rotation.ToString(),
		Seen.Num(), IgnoreActors.Num());

	return RuntimeCollisionJsonArrayToString(Out);
}
