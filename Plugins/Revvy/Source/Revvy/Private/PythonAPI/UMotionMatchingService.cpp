// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UMotionMatchingService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyMotionMatch, Log, All);

// Core / Editor
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

// Animation
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"

// PoseSearch plugin — runtime
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchIndex.h"
#if WITH_EDITOR
#include "PoseSearch/PoseSearchDerivedData.h"
#endif

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// =================================================================
// Helpers
// =================================================================

namespace RevvyMotionMatchingServiceHelpers
{
	/**
	 * Return true if the PoseSearch plugin runtime module is loaded. All
	 * public entry points call this first so failures degrade gracefully
	 * with a Warning log instead of crashing when the plugin was disabled
	 * or unloaded at runtime.
	 */
	static bool IsPoseSearchPluginAvailable()
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PoseSearch")))
		{
			return true;
		}
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PoseSearch"));
		return Plugin.IsValid() && Plugin->IsEnabled();
	}

	static UPoseSearchDatabase* LoadPoseSearchDatabase(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		return Cast<UPoseSearchDatabase>(Loaded);
	}

	static UPoseSearchSchema* LoadPoseSearchSchema(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		return Cast<UPoseSearchSchema>(Loaded);
	}

	static UObject* LoadAnimationAssetGeneric(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		// PoseSearch accepts UAnimSequence, UAnimMontage, UAnimComposite,
		// UBlendSpace, UMultiAnimAsset — all derive from UAnimationAsset.
		if (Loaded && Loaded->IsA<UAnimationAsset>())
		{
			return Loaded;
		}
		return nullptr;
	}

	static FString PoseSearchModeToString(EPoseSearchMode Mode)
	{
		switch (Mode)
		{
		case EPoseSearchMode::BruteForce: return TEXT("BruteForce");
		case EPoseSearchMode::PCAKDTree:  return TEXT("PCAKDTree");
		case EPoseSearchMode::VPTree:     return TEXT("VPTree");
		case EPoseSearchMode::EventOnly:  return TEXT("EventOnly");
		default:                          return TEXT("Unknown");
		}
	}

	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
} // namespace

// =================================================================
// Create
// =================================================================

FString UMotionMatchingService::CreatePoseSearchDatabase(
	const FString& OutputAssetPath,
	const FString& SchemaAssetPath)
{
	using namespace RevvyMotionMatchingServiceHelpers;

	if (!IsPoseSearchPluginAvailable())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: PoseSearch plugin is not enabled/loaded"));
		return FString();
	}

	if (OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: empty OutputAssetPath"));
		return FString();
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(OutputAssetPath);
	const FString AssetName   = FPackageName::GetShortName(OutputAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: invalid OutputAssetPath '%s'"), *OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: asset already exists at '%s'"), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("MotionMatchingService", "CreatePoseSearchDatabase",
		"Create Pose Search Database"));

	const FString FullPackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: failed to create package '%s'"), *FullPackageName);
		return FString();
	}

	UPoseSearchDatabase* NewDb = NewObject<UPoseSearchDatabase>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewDb)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("CreatePoseSearchDatabase: failed to instantiate UPoseSearchDatabase"));
		return FString();
	}

	// Optional schema binding.
	if (!SchemaAssetPath.IsEmpty())
	{
		if (UPoseSearchSchema* Schema = LoadPoseSearchSchema(SchemaAssetPath))
		{
			NewDb->Schema = Schema;
		}
		else
		{
			UE_LOG(LogRevvyMotionMatch, Warning,
				TEXT("CreatePoseSearchDatabase: could not resolve UPoseSearchSchema at '%s' — leaving Schema null"),
				*SchemaAssetPath);
		}
	}

	FAssetRegistryModule::AssetCreated(NewDb);
	Package->SetDirtyFlag(true);
	NewDb->MarkPackageDirty();
	NewDb->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(NewDb, /*bOnlyIfDirty*/ false);

	return NewDb->GetPathName();
}

// =================================================================
// Author
// =================================================================

bool UMotionMatchingService::AddAnimToPoseSearchDb(
	const FString& DatabasePath,
	const FString& AnimSequencePath)
{
	using namespace RevvyMotionMatchingServiceHelpers;

	if (!IsPoseSearchPluginAvailable())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("AddAnimToPoseSearchDb: PoseSearch plugin is not enabled/loaded"));
		return false;
	}

	UPoseSearchDatabase* Db = LoadPoseSearchDatabase(DatabasePath);
	if (!Db)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("AddAnimToPoseSearchDb: failed to load database '%s'"), *DatabasePath);
		return false;
	}

	UObject* AnimAsset = LoadAnimationAssetGeneric(AnimSequencePath);
	if (!AnimAsset)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("AddAnimToPoseSearchDb: failed to load animation asset '%s' (must be a UAnimationAsset subclass)"),
			*AnimSequencePath);
		return false;
	}

#if WITH_EDITOR
	FScopedTransaction Tx(NSLOCTEXT("MotionMatchingService", "AddAnimToPoseSearchDb",
		"Add Animation To Pose Search Database"));
	Db->Modify();

	// 5.7+ preferred: FPoseSearchDatabaseAnimationAsset { UObject* AnimAsset; }.
	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = AnimAsset;
	Db->AddAnimationAsset(Entry);

	Db->PostEditChange();
	Db->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Db, /*bOnlyIfDirty*/ false);
	return true;
#else
	UE_LOG(LogRevvyMotionMatch, Warning,
		TEXT("AddAnimToPoseSearchDb: UPoseSearchDatabase::AddAnimationAsset is editor-only"));
	return false;
#endif
}

bool UMotionMatchingService::BuildPoseSearchDb(const FString& DatabasePath)
{
	using namespace RevvyMotionMatchingServiceHelpers;

	if (!IsPoseSearchPluginAvailable())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("BuildPoseSearchDb: PoseSearch plugin is not enabled/loaded"));
		return false;
	}

	UPoseSearchDatabase* Db = LoadPoseSearchDatabase(DatabasePath);
	if (!Db)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("BuildPoseSearchDb: failed to load database '%s'"), *DatabasePath);
		return false;
	}

	if (!Db->Schema)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("BuildPoseSearchDb: database '%s' has no Schema assigned — cannot build"),
			*DatabasePath);
		return false;
	}

#if WITH_EDITOR
	FScopedTransaction Tx(NSLOCTEXT("MotionMatchingService", "BuildPoseSearchDb",
		"Build Pose Search Database"));

	using namespace UE::PoseSearch;

	const EAsyncBuildIndexResult Result =
		FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
			Db,
			ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);

	Db->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Db, /*bOnlyIfDirty*/ false);

	switch (Result)
	{
	case EAsyncBuildIndexResult::Success:
		UE_LOG(LogRevvyMotionMatch, Log,
			TEXT("BuildPoseSearchDb: '%s' built successfully"), *DatabasePath);
		return true;
	case EAsyncBuildIndexResult::InProgress:
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("BuildPoseSearchDb: '%s' still in progress despite WaitForCompletion"),
			*DatabasePath);
		return false;
	case EAsyncBuildIndexResult::Failed:
	default:
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("BuildPoseSearchDb: '%s' derived-data build failed"), *DatabasePath);
		return false;
	}
#else
	UE_LOG(LogRevvyMotionMatch, Warning,
		TEXT("BuildPoseSearchDb: index build is editor-only"));
	return false;
#endif
}

// =================================================================
// Query
// =================================================================

FString UMotionMatchingService::GetPoseSearchDbStats(const FString& DatabasePath)
{
	using namespace RevvyMotionMatchingServiceHelpers;

	if (!IsPoseSearchPluginAvailable())
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("GetPoseSearchDbStats: PoseSearch plugin is not enabled/loaded"));
		return FString();
	}

	UPoseSearchDatabase* Db = LoadPoseSearchDatabase(DatabasePath);
	if (!Db)
	{
		UE_LOG(LogRevvyMotionMatch, Warning,
			TEXT("GetPoseSearchDbStats: failed to load '%s'"), *DatabasePath);
		return FString();
	}

	TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("database_path"), Db->GetPathName());
	Info->SetStringField(TEXT("schema_path"),
		Db->Schema ? Db->Schema->GetPathName() : FString());
	Info->SetNumberField(TEXT("anim_asset_count"), Db->GetNumAnimationAssets());
	Info->SetStringField(TEXT("pose_search_mode"),
		PoseSearchModeToString(Db->PoseSearchMode));

	const UE::PoseSearch::FSearchIndex& Index = Db->GetSearchIndex();
	const int32 PoseCount = Index.GetNumPoses();
	Info->SetNumberField(TEXT("pose_count"), PoseCount);
	Info->SetBoolField(TEXT("is_built"), Index.GetNumPoses() > 0);

	return SerializeJson(Info);
}
