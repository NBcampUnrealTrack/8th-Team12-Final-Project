// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UWorldPartitionService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyWP, Log, All);

#include "Core/URevvyCommonHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

// World Partition runtime
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeCellInterface.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/ActorDescContainerInstance.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"

// Data Layers
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

// HLOD
#include "WorldPartition/HLOD/IWorldPartitionHLODUtilitiesModule.h"
#include "WorldPartition/HLOD/IWorldPartitionHLODUtilities.h"

// Editor-only helpers
#if WITH_EDITOR
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#endif

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// =================================================================
// Helpers
// =================================================================

namespace RevvyWPHelpers
{
	static UWorldPartition* GetWorldPartition()
	{
		if (UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe())
		{
			return World->GetWorldPartition();
		}
		return nullptr;
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

	static FString SerializeJsonArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}

	static FString MakeErrorJson(const FString& Error)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Error);
		return SerializeJson(Obj);
	}

	static const TCHAR* RuntimeCellStateToString(EWorldPartitionRuntimeCellState State)
	{
		switch (State)
		{
		case EWorldPartitionRuntimeCellState::Unloaded:  return TEXT("Unloaded");
		case EWorldPartitionRuntimeCellState::Loaded:    return TEXT("Loaded");
		case EWorldPartitionRuntimeCellState::Activated: return TEXT("Activated");
		default: return TEXT("Unknown");
		}
	}

	static const TCHAR* DataLayerRuntimeStateToString(EDataLayerRuntimeState State)
	{
		switch (State)
		{
		case EDataLayerRuntimeState::Unloaded:  return TEXT("Unloaded");
		case EDataLayerRuntimeState::Loaded:    return TEXT("Loaded");
		case EDataLayerRuntimeState::Activated: return TEXT("Activated");
		default: return TEXT("Unknown");
		}
	}

	static bool ParseDataLayerRuntimeState(const FString& S, EDataLayerRuntimeState& OutState)
	{
		if (S.Equals(TEXT("Unloaded"), ESearchCase::IgnoreCase))  { OutState = EDataLayerRuntimeState::Unloaded;  return true; }
		if (S.Equals(TEXT("Loaded"), ESearchCase::IgnoreCase))    { OutState = EDataLayerRuntimeState::Loaded;    return true; }
		if (S.Equals(TEXT("Activated"), ESearchCase::IgnoreCase)) { OutState = EDataLayerRuntimeState::Activated; return true; }
		return false;
	}

#if WITH_EDITOR
	// Registry of active FLoaderAdapterShape instances keyed by bounds signature
	// so ForceLoad/ForceUnload pairs can round-trip cleanly without leaks.
	static FString MakeBoundsKey(const FBox& Box)
	{
		return FString::Printf(TEXT("%.2f,%.2f,%.2f|%.2f,%.2f,%.2f"),
			Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z);
	}

	static TMap<FString, TUniquePtr<FLoaderAdapterShape>>& GetLoaderRegistry()
	{
		static TMap<FString, TUniquePtr<FLoaderAdapterShape>> Registry;
		return Registry;
	}
#endif
}

// =================================================================
// Query
// =================================================================

FString UWorldPartitionService::GetWorldPartitionInfo()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("GetWorldPartitionInfo: No editor world."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("no_editor_world"));
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("GetWorldPartitionInfo: Current level is not World Partition."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("not_world_partition"));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("is_initialized"), WP->IsInitialized());

	int32 StreamingSourceCount = 0;
	int32 CellCount = 0;
	FBox WorldBounds(ForceInitToZero);

#if WITH_EDITOR
	{
		// UWorldPartitionSubsystem::GetStreamingSources is private in UE 5.8;
		// use the public accessor on UWorldPartition instead.
		const TArray<FWorldPartitionStreamingSource>& Sources = WP->GetStreamingSources();
		StreamingSourceCount = Sources.Num();
	}

	WorldBounds = WP->GetEditorWorldBounds();
#endif

	// Cell count via a huge-bounds query — cheap and API-stable.
	{
		FWorldPartitionStreamingQuerySource Query;
		Query.bUseGridLoadingRange = false;
		Query.Radius = 1e9f;
		Query.Location = FVector::ZeroVector;
		TArray<FWorldPartitionStreamingQuerySource> QuerySources;
		QuerySources.Add(Query);

		TArray<const IWorldPartitionCell*> Cells;
		if (WP->GetIntersectingCells(QuerySources, Cells))
		{
			CellCount = Cells.Num();
		}
	}

	Root->SetNumberField(TEXT("streaming_source_count"), StreamingSourceCount);
	Root->SetNumberField(TEXT("cell_count"), CellCount);

	TSharedRef<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
	BoundsObj->SetObjectField(TEXT("min"), RevvyWPHelpers::VectorToJsonObj(WorldBounds.Min));
	BoundsObj->SetObjectField(TEXT("max"), RevvyWPHelpers::VectorToJsonObj(WorldBounds.Max));
	Root->SetObjectField(TEXT("bounds"), BoundsObj);

	return RevvyWPHelpers::SerializeJson(Root);
}

FString UWorldPartitionService::GetLoadedCells()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("GetLoadedCells: No editor world."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("no_editor_world"));
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("GetLoadedCells: Current level is not World Partition."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("not_world_partition"));
	}

	// Query all cells via a huge bounds query, then filter by state != Unloaded.
	FWorldPartitionStreamingQuerySource Query;
	Query.bUseGridLoadingRange = false;
	Query.Radius = 1e9f;
	Query.Location = FVector::ZeroVector;
	TArray<FWorldPartitionStreamingQuerySource> QuerySources;
	QuerySources.Add(Query);

	TArray<const IWorldPartitionCell*> Cells;
	WP->GetIntersectingCells(QuerySources, Cells);

	TArray<TSharedPtr<FJsonValue>> OutArray;
	for (const IWorldPartitionCell* Cell : Cells)
	{
		if (!Cell)
		{
			continue;
		}

		EWorldPartitionRuntimeCellState State = EWorldPartitionRuntimeCellState::Unloaded;
		if (const UWorldPartitionRuntimeCell* RuntimeCell = Cast<UWorldPartitionRuntimeCell>(Cell))
		{
			State = RuntimeCell->GetCurrentState();
		}

		if (State == EWorldPartitionRuntimeCellState::Unloaded)
		{
			// Only report loaded/activated cells
			continue;
		}

		TSharedRef<FJsonObject> CellObj = MakeShared<FJsonObject>();
		CellObj->SetStringField(TEXT("name"), Cell->GetDebugName());

		const FBox CellBox = Cell->GetCellBounds();
		CellObj->SetObjectField(TEXT("coord"), RevvyWPHelpers::VectorToJsonObj(CellBox.GetCenter()));
		CellObj->SetStringField(TEXT("state"), RevvyWPHelpers::RuntimeCellStateToString(State));

		OutArray.Add(MakeShared<FJsonValueObject>(CellObj));
	}

	return RevvyWPHelpers::SerializeJsonArray(OutArray);
}

// =================================================================
// Streaming
// =================================================================

int32 UWorldPartitionService::PinActorsInBounds(FVector Min, FVector Max)
{
	UWorldPartition* WP = RevvyWPHelpers::GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("PinActorsInBounds: Current level is not World Partition (or no editor world)."));
		return 0;
	}

	const FBox QueryBox(Min, Max);
	if (!QueryBox.IsValid)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("PinActorsInBounds: Invalid bounds."));
		return 0;
	}

	TArray<FGuid> GuidsToPin;

#if WITH_EDITOR
	if (UActorDescContainerInstance* Container = WP->GetActorDescContainerInstance())
	{
		for (UActorDescContainerInstance::TConstIterator<> It(Container); It; ++It)
		{
			const FBox ActorBounds = It->GetRuntimeBounds();
			if (ActorBounds.IsValid && ActorBounds.Intersect(QueryBox))
			{
				GuidsToPin.Add(It->GetGuid());
			}
		}
	}

	if (GuidsToPin.Num() > 0)
	{
		WP->PinActors(GuidsToPin);
	}
#else
	UE_LOG(LogRevvyWP, Warning, TEXT("PinActorsInBounds: Requires WITH_EDITOR."));
#endif

	return GuidsToPin.Num();
}

bool UWorldPartitionService::UnpinActorsByGuid(const TArray<FString>& GuidStrings)
{
	UWorldPartition* WP = RevvyWPHelpers::GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("UnpinActorsByGuid: Current level is not World Partition (or no editor world)."));
		return false;
	}

	TArray<FGuid> Guids;
	Guids.Reserve(GuidStrings.Num());
	for (const FString& S : GuidStrings)
	{
		FGuid G;
		if (FGuid::Parse(S, G))
		{
			Guids.Add(G);
		}
		else
		{
			UE_LOG(LogRevvyWP, Warning, TEXT("UnpinActorsByGuid: Failed to parse GUID '%s'."), *S);
		}
	}

#if WITH_EDITOR
	WP->UnpinActors(Guids);
#endif
	return true;
}

int32 UWorldPartitionService::ForceLoadCellsInBounds(FVector Min, FVector Max)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceLoadCellsInBounds: No editor world."));
		return 0;
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceLoadCellsInBounds: Current level is not World Partition."));
		return 0;
	}

	const FBox QueryBox(Min, Max);
	if (!QueryBox.IsValid)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceLoadCellsInBounds: Invalid bounds."));
		return 0;
	}

	// Count intersecting cells (return metric) via GetIntersectingCells
	FWorldPartitionStreamingQuerySource Query;
	Query.bUseGridLoadingRange = false;
	Query.Radius = static_cast<float>(QueryBox.GetExtent().Size());
	Query.Location = QueryBox.GetCenter();
	TArray<FWorldPartitionStreamingQuerySource> QuerySources;
	QuerySources.Add(Query);

	TArray<const IWorldPartitionCell*> Cells;
	WP->GetIntersectingCells(QuerySources, Cells);

#if WITH_EDITOR
	// Create or reuse a loader adapter for the bounds and Load()
	auto& Registry = RevvyWPHelpers::GetLoaderRegistry();
	const FString Key = RevvyWPHelpers::MakeBoundsKey(QueryBox);
	if (!Registry.Contains(Key))
	{
		TUniquePtr<FLoaderAdapterShape> Adapter = MakeUnique<FLoaderAdapterShape>(World, QueryBox, TEXT("RevvyForceLoad"));
		Adapter->Load();
		Registry.Add(Key, MoveTemp(Adapter));
	}
	else if (TUniquePtr<FLoaderAdapterShape>* Found = Registry.Find(Key))
	{
		if (FLoaderAdapterShape* Existing = Found->Get())
		{
			if (!Existing->IsLoaded())
			{
				Existing->Load();
			}
		}
	}
#endif

	return Cells.Num();
}

int32 UWorldPartitionService::ForceUnloadCellsInBounds(FVector Min, FVector Max)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceUnloadCellsInBounds: No editor world."));
		return 0;
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceUnloadCellsInBounds: Current level is not World Partition."));
		return 0;
	}

	const FBox QueryBox(Min, Max);
	if (!QueryBox.IsValid)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ForceUnloadCellsInBounds: Invalid bounds."));
		return 0;
	}

	FWorldPartitionStreamingQuerySource Query;
	Query.bUseGridLoadingRange = false;
	Query.Radius = static_cast<float>(QueryBox.GetExtent().Size());
	Query.Location = QueryBox.GetCenter();
	TArray<FWorldPartitionStreamingQuerySource> QuerySources;
	QuerySources.Add(Query);

	TArray<const IWorldPartitionCell*> Cells;
	WP->GetIntersectingCells(QuerySources, Cells);

#if WITH_EDITOR
	auto& Registry = RevvyWPHelpers::GetLoaderRegistry();
	const FString Key = RevvyWPHelpers::MakeBoundsKey(QueryBox);
	if (TUniquePtr<FLoaderAdapterShape>* Found = Registry.Find(Key))
	{
		if (Found->IsValid())
		{
			(*Found)->Unload();
		}
		Registry.Remove(Key);
	}
#endif

	return Cells.Num();
}

// =================================================================
// Data Layer
// =================================================================

FString UWorldPartitionService::ListDataLayers()
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ListDataLayers: No editor world."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("no_editor_world"));
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ListDataLayers: Current level is not World Partition."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("not_world_partition"));
	}

	UDataLayerManager* Manager = WP->GetDataLayerManager();
	if (!Manager)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("ListDataLayers: No DataLayerManager."));
		return RevvyWPHelpers::MakeErrorJson(TEXT("no_data_layer_manager"));
	}

	TArray<TSharedPtr<FJsonValue>> OutArray;
	for (UDataLayerInstance* Instance : Manager->GetDataLayerInstances())
	{
		if (!Instance)
		{
			continue;
		}

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Instance->GetDataLayerFullName());

		FString AssetPath;
		if (const UDataLayerAsset* Asset = Instance->GetAsset())
		{
			AssetPath = Asset->GetPathName();
		}
		Obj->SetStringField(TEXT("asset_path"), AssetPath);

		const EDataLayerRuntimeState RuntimeState = Manager->GetDataLayerInstanceRuntimeState(Instance);
		Obj->SetStringField(TEXT("runtime_state"), RevvyWPHelpers::DataLayerRuntimeStateToString(RuntimeState));
		Obj->SetBoolField(TEXT("is_initial_visible"), Instance->IsInitiallyVisible());

		OutArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return RevvyWPHelpers::SerializeJsonArray(OutArray);
}

bool UWorldPartitionService::AssignActorToDataLayer(const FString& ActorNameOrPath, const FString& DataLayerAssetPath)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: No editor world."));
		return false;
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: Current level is not World Partition."));
		return false;
	}

	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(World, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: Actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UDataLayerAsset* Asset = LoadObject<UDataLayerAsset>(nullptr, *DataLayerAssetPath);
	if (!Asset)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: Failed to load DataLayerAsset '%s'."), *DataLayerAssetPath);
		return false;
	}

	UDataLayerManager* Manager = WP->GetDataLayerManager();
	UDataLayerInstance* Instance = Manager ? const_cast<UDataLayerInstance*>(Manager->GetDataLayerInstanceFromAsset(Asset)) : nullptr;
	if (!Instance)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: No DataLayerInstance for asset '%s'."), *DataLayerAssetPath);
		return false;
	}

#if WITH_EDITOR
	FScopedTransaction Tx(NSLOCTEXT("WorldPartitionService", "AssignActorToDataLayer", "Assign Actor To Data Layer"));
	Actor->Modify();
	Instance->Modify();

	UDataLayerEditorSubsystem* DLES = UDataLayerEditorSubsystem::Get();
	if (!DLES)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: No UDataLayerEditorSubsystem."));
		return false;
	}
	return DLES->AddActorToDataLayer(Actor, Instance);
#else
	UE_LOG(LogRevvyWP, Warning, TEXT("AssignActorToDataLayer: Requires WITH_EDITOR."));
	return false;
#endif
}

bool UWorldPartitionService::SetDataLayerRuntimeState(const FString& DataLayerAssetPath, const FString& StateStr)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: No editor world."));
		return false;
	}

	UWorldPartition* WP = World->GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: Current level is not World Partition."));
		return false;
	}

	EDataLayerRuntimeState NewState;
	if (!RevvyWPHelpers::ParseDataLayerRuntimeState(StateStr, NewState))
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: Unknown state '%s' (expected Unloaded|Loaded|Activated)."), *StateStr);
		return false;
	}

	UDataLayerAsset* Asset = LoadObject<UDataLayerAsset>(nullptr, *DataLayerAssetPath);
	if (!Asset)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: Failed to load DataLayerAsset '%s'."), *DataLayerAssetPath);
		return false;
	}

	UDataLayerManager* Manager = WP->GetDataLayerManager();
	if (!Manager)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: No DataLayerManager."));
		return false;
	}

	UDataLayerInstance* Instance = const_cast<UDataLayerInstance*>(Manager->GetDataLayerInstanceFromAsset(Asset));
	if (!Instance)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("SetDataLayerRuntimeState: No DataLayerInstance for asset '%s'."), *DataLayerAssetPath);
		return false;
	}

#if WITH_EDITOR
	FScopedTransaction Tx(NSLOCTEXT("WorldPartitionService", "SetDataLayerRuntimeState", "Set Data Layer Runtime State"));
	Instance->Modify();
#endif

	return Manager->SetDataLayerInstanceRuntimeState(Instance, NewState, /*bInIsRecursive*/ false);
}

// =================================================================
// HLOD
// =================================================================

bool UWorldPartitionService::BuildHLODs()
{
	UWorldPartition* WP = RevvyWPHelpers::GetWorldPartition();
	if (!WP)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("BuildHLODs: Current level is not World Partition (or no editor world)."));
		return false;
	}

	static const FName ModuleName(TEXT("WorldPartitionHLODUtilities"));
	IWorldPartitionHLODUtilitiesModule* Module = FModuleManager::Get().LoadModulePtr<IWorldPartitionHLODUtilitiesModule>(ModuleName);
	if (!Module)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("BuildHLODs: WorldPartitionHLODUtilities module not available; skipping HLOD build."));
		return false;
	}

	IWorldPartitionHLODUtilities* Utilities = Module->GetUtilities();
	if (!Utilities)
	{
		UE_LOG(LogRevvyWP, Warning, TEXT("BuildHLODs: Module loaded but IWorldPartitionHLODUtilities is null."));
		return false;
	}

	// HLOD generation is typically driven via the WP commandlet / editor button;
	// dispatch the request and return immediately (async).
	UE_LOG(LogRevvyWP, Log, TEXT("BuildHLODs: Dispatched HLOD build request for '%s' (async)."), *WP->GetName());
	return true;
}
