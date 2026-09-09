// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UNavigationService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyNav, Log, All);

#include "Engine/World.h"
#include "Editor.h"
#include "ScopedTransaction.h"

// Navigation
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "AI/NavigationSystemBase.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Helpers
// =================================================================

namespace RevvyNavHelpers
{
	static UNavigationSystemV1* GetNavSystem()
	{
		UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
		if (!World)
		{
			return nullptr;
		}
		return FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	}

	static FString VectorToJsonString(const FVector& V)
	{
		return FString::Printf(TEXT("{\"x\":%f,\"y\":%f,\"z\":%f}"), V.X, V.Y, V.Z);
	}

	static TSharedPtr<FJsonObject> VectorToJsonObj(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	static FString MakeErrorJson(const FString& Error)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Error);
		return SerializeJson(Obj);
	}
}

// =================================================================
// Volume + Build
// =================================================================

FString UNavigationService::SpawnNavMeshBoundsVolume(const FString& Name, FVector Location, FVector Extent)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SpawnNavMeshBoundsVolume: No editor world."));
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("NavigationService", "SpawnNavMeshBoundsVolume", "Spawn NavMeshBoundsVolume"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavMeshBoundsVolume* Volume = World->SpawnActor<ANavMeshBoundsVolume>(
		ANavMeshBoundsVolume::StaticClass(),
		Location,
		FRotator::ZeroRotator,
		SpawnParams);

	if (!Volume)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SpawnNavMeshBoundsVolume: SpawnActor failed."));
		return FString();
	}

	Volume->Modify();

	// Default AVolume brush is +/-100 (half-extent 100). Scale = target half-extent / 100.
	const FVector Scale(
		FMath::Max(Extent.X / 200.0f, KINDA_SMALL_NUMBER),
		FMath::Max(Extent.Y / 200.0f, KINDA_SMALL_NUMBER),
		FMath::Max(Extent.Z / 200.0f, KINDA_SMALL_NUMBER));
	Volume->SetActorScale3D(Scale);

	if (!Name.IsEmpty())
	{
		Volume->SetActorLabel(Name);
	}

	// Notify navigation system of new bounds actor.
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSys->OnNavigationBoundsUpdated(Volume);
	}

	return Volume->GetActorLabel();
}

bool UNavigationService::RebuildNavMesh()
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("RebuildNavMesh: No navigation system."));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("NavigationService", "RebuildNavMesh", "Rebuild Nav Mesh"));

	NavSys->Build();
	return true;
}

bool UNavigationService::IsNavMeshGenerating()
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		return false;
	}
	return NavSys->IsNavigationBuildInProgress();
}

// =================================================================
// Query
// =================================================================

FString UNavigationService::GetNavMeshBounds()
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		return RevvyNavHelpers::MakeErrorJson(TEXT("No navigation system"));
	}

	ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
	if (!NavData)
	{
		return RevvyNavHelpers::MakeErrorJson(TEXT("No default nav data"));
	}

	const FBox Bounds = NavData->GetBounds();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("valid"), Bounds.IsValid != 0);
	Obj->SetObjectField(TEXT("min"), RevvyNavHelpers::VectorToJsonObj(Bounds.Min));
	Obj->SetObjectField(TEXT("max"), RevvyNavHelpers::VectorToJsonObj(Bounds.Max));
	return RevvyNavHelpers::SerializeJson(Obj);
}

FString UNavigationService::FindPathBetweenPoints(FVector Start, FVector End)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		return RevvyNavHelpers::MakeErrorJson(TEXT("No editor world"));
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Path || !Path->IsValid())
	{
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetNumberField(TEXT("length"), 0.0);
		Obj->SetArrayField(TEXT("points"), TArray<TSharedPtr<FJsonValue>>());
		Obj->SetStringField(TEXT("error"), TEXT("No valid path"));
		return RevvyNavHelpers::SerializeJson(Obj);
	}

	TArray<TSharedPtr<FJsonValue>> Points;
	for (const FVector& P : Path->PathPoints)
	{
		Points.Add(MakeShared<FJsonValueObject>(RevvyNavHelpers::VectorToJsonObj(P)));
	}

	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("length"), Path->GetPathLength());
	Obj->SetArrayField(TEXT("points"), Points);
	return RevvyNavHelpers::SerializeJson(Obj);
}

FString UNavigationService::ProjectPointOnNavMesh(FVector Point, FVector Extent)
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		return RevvyNavHelpers::MakeErrorJson(TEXT("No navigation system"));
	}

	FNavLocation NavLoc;
	const bool bOk = NavSys->ProjectPointToNavigation(Point, NavLoc, Extent);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), bOk);
	Obj->SetObjectField(TEXT("location"), RevvyNavHelpers::VectorToJsonObj(NavLoc.Location));
	Obj->SetNumberField(TEXT("nodeRef"), static_cast<double>(NavLoc.NodeRef));
	return RevvyNavHelpers::SerializeJson(Obj);
}

FString UNavigationService::GetRandomReachablePoint(FVector Origin, float Radius)
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		return RevvyNavHelpers::MakeErrorJson(TEXT("No navigation system"));
	}

	FNavLocation Out;
	const bool bOk = NavSys->GetRandomReachablePointInRadius(Origin, Radius, Out);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), bOk);
	Obj->SetObjectField(TEXT("location"), RevvyNavHelpers::VectorToJsonObj(Out.Location));
	return RevvyNavHelpers::SerializeJson(Obj);
}

float UNavigationService::GetPathLengthBetweenPoints(FVector Start, FVector End)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("GetPathLengthBetweenPoints: No editor world."));
		return -1.0f;
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Start, End);
	if (!Path || !Path->IsValid())
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("GetPathLengthBetweenPoints: No valid path."));
		return -1.0f;
	}
	return Path->GetPathLength();
}

// =================================================================
// Config
// =================================================================

bool UNavigationService::SetNavMeshAgentRadius(float Radius)
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SetNavMeshAgentRadius: No navigation system."));
		return false;
	}

	ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
	if (!Recast)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SetNavMeshAgentRadius: No ARecastNavMesh default nav data."));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("NavigationService", "SetNavMeshAgentRadius", "Set NavMesh Agent Radius"));
	Recast->Modify();
	Recast->AgentRadius = Radius;
	return true;
}

bool UNavigationService::SetNavMeshAgentHeight(float Height)
{
	UNavigationSystemV1* NavSys = RevvyNavHelpers::GetNavSystem();
	if (!NavSys)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SetNavMeshAgentHeight: No navigation system."));
		return false;
	}

	ARecastNavMesh* Recast = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
	if (!Recast)
	{
		UE_LOG(LogRevvyNav, Warning, TEXT("SetNavMeshAgentHeight: No ARecastNavMesh default nav data."));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("NavigationService", "SetNavMeshAgentHeight", "Set NavMesh Agent Height"));
	Recast->Modify();
	Recast->AgentHeight = Height;
	return true;
}
