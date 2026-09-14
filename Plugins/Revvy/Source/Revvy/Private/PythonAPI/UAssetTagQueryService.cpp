// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAssetTagQueryService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyAssetTag, Log, All);

// Editor / engine
#include "Editor.h"

// Asset registry
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"

// Gameplay tags
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"

// Asset load/save helpers
#include "EditorAssetLibrary.h"

// Transactions + reflection
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "RevvyAssetTagQueryService"

// =================================================================
// Local helpers
// =================================================================
namespace
{
	/** Access the AssetRegistry module (loads it if not yet loaded). */
	IAssetRegistry& AssetTagQueryGetAssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	}

	/** Serialize a JSON array to a compact FString. */
	FString AssetTagQueryJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/**
	 * Resolve AssetClassPath to a top-level asset path suitable for FARFilter::ClassPaths.
	 * Accepts either:
	 *   - empty         -> returns invalid FTopLevelAssetPath (skip filter)
	 *   - "Blueprint"   -> loads the UClass by short name
	 *   - "/Script/Engine.Blueprint" -> direct top-level path
	 */
	FTopLevelAssetPath ResolveClassPath(const FString& AssetClassPath)
	{
		if (AssetClassPath.IsEmpty())
		{
			return FTopLevelAssetPath();
		}

		// Fully-qualified top-level path (contains a '.').
		if (AssetClassPath.Contains(TEXT(".")))
		{
			return FTopLevelAssetPath(AssetClassPath);
		}

		// Short class name: resolve via UClass::TryFindTypeSlow / FindObject.
		if (UClass* Cls = FindObject<UClass>(nullptr, *AssetClassPath))
		{
			return FTopLevelAssetPath(Cls);
		}

		// Try engine-scoped fallback.
		const FString EngineScoped = FString::Printf(TEXT("/Script/Engine.%s"), *AssetClassPath);
		return FTopLevelAssetPath(EngineScoped);
	}

	/** Try to request a gameplay tag by name; returns invalid tag if unregistered. */
	FGameplayTag RequestTag(const FString& TagName)
	{
		if (TagName.IsEmpty())
		{
			return FGameplayTag();
		}
		// ErrorIfNotFound = false -> don't spam log for query-side probing.
		return FGameplayTag::RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound=*/false);
	}

	/**
	 * Extract owned gameplay tags from an FAssetData without loading the asset.
	 * Reads the "GameplayTags" asset registry tag if present.
	 */
	void ReadTagsFromAssetData(const FAssetData& AssetData, FGameplayTagContainer& OutContainer)
	{
		OutContainer.Reset();
		FString TagsRaw;
		if (AssetData.GetTagValue<FString>(FName(TEXT("GameplayTags")), TagsRaw))
		{
			OutContainer.FromExportString(TagsRaw);
		}
	}

	/**
	 * Load an asset and, if it implements IGameplayTagAssetInterface, populate
	 * OutContainer with its owned tags. Returns true if the interface path
	 * succeeded; false if we should fall back to AssetData tag values.
	 */
	bool ReadTagsFromLoadedAsset(const FAssetData& AssetData, FGameplayTagContainer& OutContainer)
	{
		OutContainer.Reset();
		UObject* Obj = AssetData.GetAsset();
		if (!Obj)
		{
			return false;
		}
		if (IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Obj))
		{
			TagIface->GetOwnedGameplayTags(OutContainer);
			return true;
		}
		return false;
	}

	/**
	 * Best-effort tag read that prefers the AssetData fast-path (no load) and
	 * falls back to loading the asset for IGameplayTagAssetInterface support.
	 */
	void ReadTagsForAsset(const FAssetData& AssetData, FGameplayTagContainer& OutContainer)
	{
		ReadTagsFromAssetData(AssetData, OutContainer);
		if (OutContainer.Num() == 0)
		{
			// Fall back to loading and using the interface.
			ReadTagsFromLoadedAsset(AssetData, OutContainer);
		}
	}

	/** Build FARFilter honoring AssetClassPath (empty = all). */
	FARFilter BuildFilter(const FString& AssetClassPath)
	{
		FARFilter Filter;
		Filter.bRecursiveClasses = true;
		const FTopLevelAssetPath ClassPath = ResolveClassPath(AssetClassPath);
		if (ClassPath.IsValid())
		{
			Filter.ClassPaths.Add(ClassPath);
		}
		return Filter;
	}

	/** JSON payload for a matched asset. */
	TSharedPtr<FJsonValue> MakeAssetHit(const FAssetData& AssetData)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		Obj->SetStringField(TEXT("class"),      AssetData.AssetClassPath.ToString());
		return MakeShared<FJsonValueObject>(Obj);
	}

	/**
	 * Locate the first FGameplayTagContainer UPROPERTY on Asset. Returns nullptr
	 * if none found. Populates OutContainerPtr with a live pointer into the
	 * object's memory when found.
	 */
	FStructProperty* FindGameplayTagContainerProperty(UObject* Asset, FGameplayTagContainer*& OutContainerPtr)
	{
		OutContainerPtr = nullptr;
		if (!Asset)
		{
			return nullptr;
		}
		UScriptStruct* ContainerStruct = FGameplayTagContainer::StaticStruct();
		for (TFieldIterator<FStructProperty> It(Asset->GetClass()); It; ++It)
		{
			FStructProperty* StructProp = *It;
			if (StructProp && StructProp->Struct == ContainerStruct)
			{
				OutContainerPtr = StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Asset);
				return StructProp;
			}
		}
		return nullptr;
	}
}

// =================================================================
// Query: FindAssetsWithTag
// =================================================================
FString UAssetTagQueryService::FindAssetsWithTag(const FString& TagName, const FString& AssetClassPath)
{
	TArray<TSharedPtr<FJsonValue>> Hits;

	const FGameplayTag QueryTag = RequestTag(TagName);
	if (!QueryTag.IsValid())
	{
		UE_LOG(LogRevvyAssetTag, Warning,
			TEXT("FindAssetsWithTag: tag '%s' is not registered — returning empty."), *TagName);
		return AssetTagQueryJsonArrayToString(Hits);
	}

	FARFilter Filter = BuildFilter(AssetClassPath);
	TArray<FAssetData> Assets;
	AssetTagQueryGetAssetRegistry().GetAssets(Filter, Assets);

	for (const FAssetData& AssetData : Assets)
	{
		FGameplayTagContainer Tags;
		ReadTagsForAsset(AssetData, Tags);
		if (Tags.HasTag(QueryTag))
		{
			Hits.Add(MakeAssetHit(AssetData));
		}
	}

	UE_LOG(LogRevvyAssetTag, Verbose,
		TEXT("FindAssetsWithTag('%s', '%s'): %d hit(s) across %d candidate(s)."),
		*TagName, *AssetClassPath, Hits.Num(), Assets.Num());

	return AssetTagQueryJsonArrayToString(Hits);
}

// =================================================================
// Query: FindAssetsWithAnyTag
// =================================================================
FString UAssetTagQueryService::FindAssetsWithAnyTag(const TArray<FString>& TagNames, const FString& AssetClassPath)
{
	TArray<TSharedPtr<FJsonValue>> Hits;

	FGameplayTagContainer QueryTags;
	for (const FString& Name : TagNames)
	{
		FGameplayTag T = RequestTag(Name);
		if (T.IsValid())
		{
			QueryTags.AddTag(T);
		}
		else
		{
			UE_LOG(LogRevvyAssetTag, Warning,
				TEXT("FindAssetsWithAnyTag: skipping unregistered tag '%s'."), *Name);
		}
	}
	if (QueryTags.Num() == 0)
	{
		return AssetTagQueryJsonArrayToString(Hits);
	}

	FARFilter Filter = BuildFilter(AssetClassPath);
	TArray<FAssetData> Assets;
	AssetTagQueryGetAssetRegistry().GetAssets(Filter, Assets);

	for (const FAssetData& AssetData : Assets)
	{
		FGameplayTagContainer Tags;
		ReadTagsForAsset(AssetData, Tags);
		if (Tags.HasAny(QueryTags))
		{
			Hits.Add(MakeAssetHit(AssetData));
		}
	}

	return AssetTagQueryJsonArrayToString(Hits);
}

// =================================================================
// Query: FindAssetsWithAllTags
// =================================================================
FString UAssetTagQueryService::FindAssetsWithAllTags(const TArray<FString>& TagNames, const FString& AssetClassPath)
{
	TArray<TSharedPtr<FJsonValue>> Hits;

	FGameplayTagContainer QueryTags;
	for (const FString& Name : TagNames)
	{
		FGameplayTag T = RequestTag(Name);
		if (T.IsValid())
		{
			QueryTags.AddTag(T);
		}
		else
		{
			UE_LOG(LogRevvyAssetTag, Warning,
				TEXT("FindAssetsWithAllTags: tag '%s' is not registered — ALL query cannot match anything."),
				*Name);
			return AssetTagQueryJsonArrayToString(Hits);
		}
	}
	if (QueryTags.Num() == 0)
	{
		return AssetTagQueryJsonArrayToString(Hits);
	}

	FARFilter Filter = BuildFilter(AssetClassPath);
	TArray<FAssetData> Assets;
	AssetTagQueryGetAssetRegistry().GetAssets(Filter, Assets);

	for (const FAssetData& AssetData : Assets)
	{
		FGameplayTagContainer Tags;
		ReadTagsForAsset(AssetData, Tags);
		if (Tags.HasAll(QueryTags))
		{
			Hits.Add(MakeAssetHit(AssetData));
		}
	}

	return AssetTagQueryJsonArrayToString(Hits);
}

// =================================================================
// Query: GetAssetTags
// =================================================================
FString UAssetTagQueryService::GetAssetTags(const FString& AssetPath)
{
	TArray<TSharedPtr<FJsonValue>> Out;

	if (AssetPath.IsEmpty())
	{
		return AssetTagQueryJsonArrayToString(Out);
	}

	// Try loaded-object interface first (most authoritative).
	FGameplayTagContainer Container;
	if (UObject* Obj = LoadObject<UObject>(nullptr, *AssetPath))
	{
		if (IGameplayTagAssetInterface* TagIface = Cast<IGameplayTagAssetInterface>(Obj))
		{
			TagIface->GetOwnedGameplayTags(Container);
		}
	}

	// Fall back to AssetRegistry tag lookup.
	if (Container.Num() == 0)
	{
		IAssetRegistry& AR = AssetTagQueryGetAssetRegistry();
		const FSoftObjectPath SoftPath(AssetPath);
		FAssetData AssetData = AR.GetAssetByObjectPath(SoftPath);
		if (AssetData.IsValid())
		{
			ReadTagsFromAssetData(AssetData, Container);
		}
	}

	for (const FGameplayTag& Tag : Container)
	{
		Out.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	return AssetTagQueryJsonArrayToString(Out);
}

// =================================================================
// Mutate: AddTagToAsset
// =================================================================
bool UAssetTagQueryService::AddTagToAsset(const FString& AssetPath, const FString& TagName)
{
	if (AssetPath.IsEmpty() || TagName.IsEmpty())
	{
		UE_LOG(LogRevvyAssetTag, Warning, TEXT("AddTagToAsset: empty asset path or tag name."));
		return false;
	}

	const FGameplayTag Tag = RequestTag(TagName);
	if (!Tag.IsValid())
	{
		UE_LOG(LogRevvyAssetTag, Warning,
			TEXT("AddTagToAsset: tag '%s' is not registered — refusing to add."), *TagName);
		return false;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		UE_LOG(LogRevvyAssetTag, Warning, TEXT("AddTagToAsset: failed to load asset at '%s'."), *AssetPath);
		return false;
	}

	FGameplayTagContainer* ContainerPtr = nullptr;
	FStructProperty* Prop = FindGameplayTagContainerProperty(Asset, ContainerPtr);
	if (!Prop || !ContainerPtr)
	{
		UE_LOG(LogRevvyAssetTag, Warning,
			TEXT("AddTagToAsset: asset '%s' (class %s) has no FGameplayTagContainer UPROPERTY — no-op."),
			*AssetPath, *Asset->GetClass()->GetName());
		return false;
	}

	if (ContainerPtr->HasTagExact(Tag))
	{
		// Already present — treat as success without mutating.
		return true;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("AddTagToAsset", "Add Gameplay Tag to Asset"));
		Asset->Modify();
		ContainerPtr->AddTag(Tag);
		Asset->MarkPackageDirty();
	}

	UEditorAssetLibrary::SaveLoadedAsset(Asset);

	UE_LOG(LogRevvyAssetTag, Log, TEXT("AddTagToAsset: added '%s' to '%s'."), *TagName, *AssetPath);
	return true;
}

// =================================================================
// Mutate: RemoveTagFromAsset
// =================================================================
bool UAssetTagQueryService::RemoveTagFromAsset(const FString& AssetPath, const FString& TagName)
{
	if (AssetPath.IsEmpty() || TagName.IsEmpty())
	{
		UE_LOG(LogRevvyAssetTag, Warning, TEXT("RemoveTagFromAsset: empty asset path or tag name."));
		return false;
	}

	const FGameplayTag Tag = RequestTag(TagName);
	if (!Tag.IsValid())
	{
		UE_LOG(LogRevvyAssetTag, Warning,
			TEXT("RemoveTagFromAsset: tag '%s' is not registered — nothing to remove."), *TagName);
		return false;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		UE_LOG(LogRevvyAssetTag, Warning, TEXT("RemoveTagFromAsset: failed to load asset at '%s'."), *AssetPath);
		return false;
	}

	FGameplayTagContainer* ContainerPtr = nullptr;
	FStructProperty* Prop = FindGameplayTagContainerProperty(Asset, ContainerPtr);
	if (!Prop || !ContainerPtr)
	{
		UE_LOG(LogRevvyAssetTag, Warning,
			TEXT("RemoveTagFromAsset: asset '%s' (class %s) has no FGameplayTagContainer UPROPERTY — no-op."),
			*AssetPath, *Asset->GetClass()->GetName());
		return false;
	}

	if (!ContainerPtr->HasTagExact(Tag))
	{
		// Tag not present — treat as success (idempotent).
		return true;
	}

	{
		FScopedTransaction Transaction(LOCTEXT("RemoveTagFromAsset", "Remove Gameplay Tag from Asset"));
		Asset->Modify();
		ContainerPtr->RemoveTag(Tag);
		Asset->MarkPackageDirty();
	}

	UEditorAssetLibrary::SaveLoadedAsset(Asset);

	UE_LOG(LogRevvyAssetTag, Log, TEXT("RemoveTagFromAsset: removed '%s' from '%s'."), *TagName, *AssetPath);
	return true;
}

#undef LOCTEXT_NAMESPACE
