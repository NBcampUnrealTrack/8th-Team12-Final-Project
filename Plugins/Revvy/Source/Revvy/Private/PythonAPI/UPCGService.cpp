// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPCGService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPCG, Log, All);

#include "Core/URevvyCommonHelpers.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"

#include "UObject/Package.h"
#include "Misc/PackageName.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// PCG plugin (runtime module)
#include "PCGGraph.h"
#include "PCGComponent.h"
#include "PCGSubsystem.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	/** Split "/Game/Foo/Bar" into ("/Game/Foo", "Bar"). Returns false on empty inputs. */
	bool PCGSplitPackagePath(const FString& AssetPath, FString& OutPackageDir, FString& OutAssetName)
	{
		if (AssetPath.IsEmpty())
		{
			return false;
		}
		OutPackageDir = FPackageName::GetLongPackagePath(AssetPath);
		OutAssetName  = FPackageName::GetShortName(AssetPath);
		return !OutPackageDir.IsEmpty() && !OutAssetName.IsEmpty();
	}
}

// =================================================================
// Asset authoring
// =================================================================

FString UPCGService::CreatePCGGraph(const FString& OutputAssetPath)
{
#if WITH_EDITOR
	FString PackageDir, AssetName;
	if (!PCGSplitPackagePath(OutputAssetPath, PackageDir, AssetName))
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CreatePCGGraph: invalid OutputAssetPath '%s'."), *OutputAssetPath);
		return FString();
	}

	if (!FPackageName::IsValidLongPackageName(OutputAssetPath))
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CreatePCGGraph: not a valid long package name '%s'."), *OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CreatePCGGraph: asset already exists at '%s'."), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("Revvy", "PCGCreateGraph", "Create PCG Graph"));

	const FString FullPackageName = PackageDir / AssetName;
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		UE_LOG(LogRevvyPCG, Error,
			TEXT("CreatePCGGraph: failed to create package '%s'."), *FullPackageName);
		return FString();
	}
	Package->FullyLoad();

	UPCGGraph* NewGraph = NewObject<UPCGGraph>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewGraph)
	{
		UE_LOG(LogRevvyPCG, Error,
			TEXT("CreatePCGGraph: NewObject<UPCGGraph> failed for '%s'."), *OutputAssetPath);
		return FString();
	}

	FAssetRegistryModule::AssetCreated(NewGraph);
	Package->SetDirtyFlag(true);
	NewGraph->MarkPackageDirty();
	NewGraph->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(NewGraph, /*bOnlyIfDirty*/ false);

	UE_LOG(LogRevvyPCG, Log, TEXT("CreatePCGGraph: created '%s'."), *OutputAssetPath);
	return NewGraph->GetPathName();
#else
	UE_LOG(LogRevvyPCG, Warning,
		TEXT("CreatePCGGraph: editor-only operation not available in this build."));
	return FString();
#endif
}

// =================================================================
// Component authoring
// =================================================================

FString UPCGService::CreatePCGComponent(const FString& ActorNameOrPath, const FString& PCGGraphPath)
{
#if WITH_EDITOR
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CreatePCGComponent: could not resolve actor '%s'."), *ActorNameOrPath);
		return FString();
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(PCGGraphPath);
	UPCGGraph* Graph = Cast<UPCGGraph>(Loaded);
	if (!Graph)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CreatePCGComponent: '%s' is not a UPCGGraph asset."), *PCGGraphPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("Revvy", "PCGCreateComponent", "Create PCG Component"));
	Actor->Modify();

	UPCGComponent* Comp = NewObject<UPCGComponent>(Actor, UPCGComponent::StaticClass(),
		NAME_None, RF_Transactional);
	if (!Comp)
	{
		UE_LOG(LogRevvyPCG, Error,
			TEXT("CreatePCGComponent: NewObject<UPCGComponent> failed on actor '%s'."),
			*Actor->GetActorLabel());
		return FString();
	}

	// UE 5.8: UPCGComponent derives from UActorComponent (not USceneComponent),
	// so it has no transform and cannot be attached via SetupAttachment.
	Comp->RegisterComponent();
	Actor->AddInstanceComponent(Comp);

	Comp->SetGraph(Graph);

	UE_LOG(LogRevvyPCG, Log,
		TEXT("CreatePCGComponent: attached PCG component '%s' to actor '%s' with graph '%s'."),
		*Comp->GetName(), *Actor->GetActorLabel(), *PCGGraphPath);

	return Comp->GetName();
#else
	UE_LOG(LogRevvyPCG, Warning,
		TEXT("CreatePCGComponent: editor-only operation not available in this build."));
	return FString();
#endif
}

// =================================================================
// Generation control
// =================================================================

bool UPCGService::GeneratePCGComponent(const FString& ActorNameOrPath, bool bForce)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("GeneratePCGComponent: could not resolve actor '%s'."), *ActorNameOrPath);
		return false;
	}

	UPCGComponent* Comp = Actor->FindComponentByClass<UPCGComponent>();
	if (!Comp)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("GeneratePCGComponent: actor '%s' has no UPCGComponent."),
			*Actor->GetActorLabel());
		return false;
	}

	Comp->Generate(bForce);
	UE_LOG(LogRevvyPCG, Log,
		TEXT("GeneratePCGComponent: dispatched Generate(bForce=%s) on '%s'."),
		bForce ? TEXT("true") : TEXT("false"), *Actor->GetActorLabel());
	return true;
}

bool UPCGService::CancelPCGGeneration(const FString& ActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CancelPCGGeneration: could not resolve actor '%s'."), *ActorNameOrPath);
		return false;
	}

	UPCGComponent* Comp = Actor->FindComponentByClass<UPCGComponent>();
	if (!Comp)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("CancelPCGGeneration: actor '%s' has no UPCGComponent."),
			*Actor->GetActorLabel());
		return false;
	}

	Comp->CancelGeneration();
	UE_LOG(LogRevvyPCG, Log,
		TEXT("CancelPCGGeneration: cancelled generation on '%s'."),
		*Actor->GetActorLabel());
	return true;
}

// =================================================================
// Query
// =================================================================

FString UPCGService::GetPCGComponentInfo(const FString& ActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		return FString();
	}

	UPCGComponent* Comp = Actor->FindComponentByClass<UPCGComponent>();
	if (!Comp)
	{
		UE_LOG(LogRevvyPCG, Warning,
			TEXT("GetPCGComponentInfo: actor '%s' has no UPCGComponent."),
			*Actor->GetActorLabel());
		return FString();
	}

	const UPCGGraph* Graph = Comp->GetGraph();
	const FString GraphPath = Graph ? Graph->GetPathName() : FString();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_path"), GraphPath);
	Root->SetBoolField  (TEXT("is_generating"), Comp->IsGenerating());
	Root->SetBoolField  (TEXT("is_partitioned"), Comp->IsPartitioned());
	Root->SetNumberField(TEXT("seed"), static_cast<double>(Comp->Seed));

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}
