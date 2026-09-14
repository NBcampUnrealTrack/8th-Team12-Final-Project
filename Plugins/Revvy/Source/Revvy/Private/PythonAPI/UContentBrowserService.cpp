// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UContentBrowserService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyContentBrowser, Log, All);

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Module loader
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
// Editor / subsystem access
#include "Editor.h"

// ContentBrowserData module — virtual-path enumeration
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserDataFilter.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserItemData.h"
#include "ContentBrowserDataSource.h"

// ContentBrowser module — singleton for selection / focus / sync
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#endif

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON object to a compact FString. */
	FString ContentBrowserJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}

	/** Serialize a JSON array to a compact FString. */
	FString ContentBrowserJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Build a compact `{"error":"..."}` payload wrapped as a JSON array with the
	 * error object as the sole element — keeps the return shape consistent
	 * with the array-returning UFUNCTIONs while still carrying diagnostics. */
	FString MakeErrorArrayJson(const FString& Msg)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("error"), Msg);
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
		return ContentBrowserJsonArrayToString(Arr);
	}

#if WITH_EDITOR
	/** Parse the string filter into a CB2 item-type flag mask. */
	EContentBrowserItemTypeFilter ParseItemTypeFilter(const FString& InFilter)
	{
		const FString Norm = InFilter.TrimStartAndEnd();
		if (Norm.Equals(TEXT("Folder"), ESearchCase::IgnoreCase))
		{
			return EContentBrowserItemTypeFilter::IncludeFolders;
		}
		if (Norm.Equals(TEXT("File"), ESearchCase::IgnoreCase))
		{
			return EContentBrowserItemTypeFilter::IncludeFiles;
		}
		// Default / "All"
		return EContentBrowserItemTypeFilter::IncludeAll;
	}

	/** Derive the top-level mount root from a virtual path (e.g. "/All/Game/Foo" -> "/All"). */
	FString ExtractMountRoot(const FString& InVirtualPath)
	{
		if (InVirtualPath.IsEmpty() || InVirtualPath == TEXT("/"))
		{
			return TEXT("/");
		}
		FString Working = InVirtualPath;
		if (Working.StartsWith(TEXT("/")))
		{
			Working.RemoveAt(0, 1, EAllowShrinking::No);
		}
		int32 SlashIdx = INDEX_NONE;
		if (Working.FindChar(TEXT('/'), SlashIdx))
		{
			return FString::Printf(TEXT("/%s"), *Working.Left(SlashIdx));
		}
		return FString::Printf(TEXT("/%s"), *Working);
	}

	/** Serialize a single FContentBrowserItem to a JSON object. */
	TSharedPtr<FJsonObject> ItemToJson(const FContentBrowserItem& InItem)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

		const FString VirtualPath = InItem.GetVirtualPath().ToString();
		Obj->SetStringField(TEXT("virtual_path"),  VirtualPath);

		FName InternalPath;
		FString PhysicalPath;
		if (InItem.Legacy_TryGetPackagePath(InternalPath))
		{
			PhysicalPath = InternalPath.ToString();
		}
		Obj->SetStringField(TEXT("physical_path"), PhysicalPath);

		Obj->SetBoolField  (TEXT("is_folder"),     InItem.IsFolder());
		Obj->SetStringField(TEXT("display_name"),  InItem.GetDisplayName().ToString());
		Obj->SetStringField(TEXT("mount_root"),    ExtractMountRoot(VirtualPath));

		return Obj;
	}

	/** Serialize a single FContentBrowserItemData to a JSON object.
	 *  Used by EnumerateItemsAtPath which surfaces per-source ItemData
	 *  chunks (an FContentBrowserItem is an aggregate of ItemDatas). */
	TSharedPtr<FJsonObject> ItemDataToJson(const FContentBrowserItemData& InItemData)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

		const FString VirtualPath = InItemData.GetVirtualPath().ToString();
		Obj->SetStringField(TEXT("virtual_path"),  VirtualPath);

		const FName InternalPath = InItemData.GetInternalPath();
		Obj->SetStringField(TEXT("physical_path"), InternalPath.IsNone() ? FString() : InternalPath.ToString());

		Obj->SetBoolField  (TEXT("is_folder"),     InItemData.IsFolder());
		Obj->SetStringField(TEXT("display_name"),  InItemData.GetDisplayName().ToString());
		Obj->SetStringField(TEXT("mount_root"),    ExtractMountRoot(VirtualPath));

		return Obj;
	}

	/** Look up the CB2 data subsystem; logs and returns null when unavailable. */
	UContentBrowserDataSubsystem* GetContentBrowserDataSubsystemChecked(const TCHAR* CallSite)
	{
		if (!GEditor)
		{
			UE_LOG(LogRevvyContentBrowser, Warning, TEXT("%s: GEditor is null."), CallSite);
			return nullptr;
		}
		UContentBrowserDataSubsystem* Subsystem = GEditor->GetEditorSubsystem<UContentBrowserDataSubsystem>();
		if (!Subsystem)
		{
			UE_LOG(LogRevvyContentBrowser, Warning, TEXT("%s: UContentBrowserDataSubsystem unavailable."), CallSite);
		}
		return Subsystem;
	}
#endif // WITH_EDITOR
}

// =================================================================
// EnumerateAtVirtualPath
// =================================================================

FString UContentBrowserService::EnumerateAtVirtualPath(const FString& VirtualPath, const FString& ItemTypeFilter)
{
#if WITH_EDITOR
	UContentBrowserDataSubsystem* DataSS = GetContentBrowserDataSubsystemChecked(TEXT("EnumerateAtVirtualPath"));
	if (!DataSS)
	{
		return MakeErrorArrayJson(TEXT("UContentBrowserDataSubsystem unavailable"));
	}

	const FString QueryPath = VirtualPath.IsEmpty() ? FString(TEXT("/")) : VirtualPath;
	const EContentBrowserItemTypeFilter Filter = ParseItemTypeFilter(ItemTypeFilter);

	TArray<TSharedPtr<FJsonValue>> Entries;

	DataSS->EnumerateItemsAtPath(
		FName(*QueryPath),
		Filter,
		[&Entries](FContentBrowserItemData&& InItemData) -> bool
		{
			Entries.Add(MakeShared<FJsonValueObject>(ItemDataToJson(InItemData)));
			return true; // continue enumeration
		});

	UE_LOG(LogRevvyContentBrowser, Verbose,
		TEXT("EnumerateAtVirtualPath('%s', '%s') -> %d entries."),
		*QueryPath, *ItemTypeFilter, Entries.Num());

	return ContentBrowserJsonArrayToString(Entries);
#else
	UE_LOG(LogRevvyContentBrowser, Warning, TEXT("EnumerateAtVirtualPath: editor-only API called from non-editor build."));
	return MakeErrorArrayJson(TEXT("editor-only API"));
#endif
}

// =================================================================
// GetCurrentContentBrowserSelection
// =================================================================

FString UContentBrowserService::GetCurrentContentBrowserSelection()
{
#if WITH_EDITOR
	FContentBrowserModule* ContentBrowserModule =
		FModuleManager::GetModulePtr<FContentBrowserModule>("ContentBrowser");
	if (!ContentBrowserModule)
	{
		UE_LOG(LogRevvyContentBrowser, Warning, TEXT("GetCurrentContentBrowserSelection: ContentBrowser module not loaded."));
		return MakeErrorArrayJson(TEXT("ContentBrowser module not loaded"));
	}

	IContentBrowserSingleton& CBS = ContentBrowserModule->Get();

	// UE 5.8: IContentBrowserSingleton has no GetSelectedItems(). Query the selected
	// assets and folders separately and serialize each to the same JSON shape.
	TArray<FAssetData> SelectedAssets;
	CBS.GetSelectedAssets(SelectedAssets);

	TArray<FString> SelectedFolders;
	CBS.GetSelectedFolders(SelectedFolders);

	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(SelectedAssets.Num() + SelectedFolders.Num());

	for (const FAssetData& Asset : SelectedAssets)
	{
		const FString PackagePath = Asset.PackageName.ToString();
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("virtual_path"),  Asset.GetObjectPathString());
		Obj->SetStringField(TEXT("physical_path"), PackagePath);
		Obj->SetBoolField  (TEXT("is_folder"),     false);
		Obj->SetStringField(TEXT("display_name"),  Asset.AssetName.ToString());
		Obj->SetStringField(TEXT("mount_root"),    ExtractMountRoot(PackagePath));
		Entries.Add(MakeShared<FJsonValueObject>(Obj));
	}

	for (const FString& Folder : SelectedFolders)
	{
		FString DisplayName = Folder;
		int32 LastSlash = INDEX_NONE;
		if (Folder.FindLastChar(TEXT('/'), LastSlash) && LastSlash + 1 < Folder.Len())
		{
			DisplayName = Folder.RightChop(LastSlash + 1);
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("virtual_path"),  Folder);
		Obj->SetStringField(TEXT("physical_path"), Folder);
		Obj->SetBoolField  (TEXT("is_folder"),     true);
		Obj->SetStringField(TEXT("display_name"),  DisplayName);
		Obj->SetStringField(TEXT("mount_root"),    ExtractMountRoot(Folder));
		Entries.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return ContentBrowserJsonArrayToString(Entries);
#else
	UE_LOG(LogRevvyContentBrowser, Warning, TEXT("GetCurrentContentBrowserSelection: editor-only API called from non-editor build."));
	return MakeErrorArrayJson(TEXT("editor-only API"));
#endif
}

// =================================================================
// FocusContentBrowserToVirtualPath
// =================================================================

bool UContentBrowserService::FocusContentBrowserToVirtualPath(const FString& VirtualPath)
{
#if WITH_EDITOR
	if (VirtualPath.IsEmpty())
	{
		UE_LOG(LogRevvyContentBrowser, Warning, TEXT("FocusContentBrowserToVirtualPath: VirtualPath is empty."));
		return false;
	}

	FContentBrowserModule* ContentBrowserModule =
		FModuleManager::GetModulePtr<FContentBrowserModule>("ContentBrowser");
	if (!ContentBrowserModule)
	{
		UE_LOG(LogRevvyContentBrowser, Warning, TEXT("FocusContentBrowserToVirtualPath: ContentBrowser module not loaded."));
		return false;
	}

	IContentBrowserSingleton& CBS = ContentBrowserModule->Get();

	CBS.FocusPrimaryContentBrowser(/*bFocusSearch=*/true);

	TArray<FString> Folders;
	Folders.Add(VirtualPath);
	CBS.SyncBrowserToFolders(Folders);

	UE_LOG(LogRevvyContentBrowser, Verbose,
		TEXT("FocusContentBrowserToVirtualPath: synced to '%s'."), *VirtualPath);
	return true;
#else
	UE_LOG(LogRevvyContentBrowser, Warning, TEXT("FocusContentBrowserToVirtualPath: editor-only API called from non-editor build."));
	return false;
#endif
}

// =================================================================
// GetVirtualPathTreeRoots
// =================================================================

FString UContentBrowserService::GetVirtualPathTreeRoots()
{
#if WITH_EDITOR
	UContentBrowserDataSubsystem* DataSS = GetContentBrowserDataSubsystemChecked(TEXT("GetVirtualPathTreeRoots"));
	if (!DataSS)
	{
		return MakeErrorArrayJson(TEXT("UContentBrowserDataSubsystem unavailable"));
	}

	TArray<TSharedPtr<FJsonValue>> Entries;

	// The CB2 virtual-path tree is rooted at "/". Enumerating folders at
	// "/" yields the top-level mount points contributed by each active
	// UContentBrowserDataSource (Game, Engine, Plugins, Filters, etc.).
	DataSS->EnumerateItemsAtPath(
		FName(TEXT("/")),
		EContentBrowserItemTypeFilter::IncludeFolders,
		[&Entries](FContentBrowserItemData&& InItemData) -> bool
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			const FString VirtualPath = InItemData.GetVirtualPath().ToString();
			Obj->SetStringField(TEXT("root"), VirtualPath);

			const UContentBrowserDataSource* Owner = InItemData.GetOwnerDataSource();
			const FString SourceName = Owner ? Owner->GetName() : FString();
			Obj->SetStringField(TEXT("source_name"), SourceName);

			Entries.Add(MakeShared<FJsonValueObject>(Obj));
			return true;
		});

	UE_LOG(LogRevvyContentBrowser, Verbose,
		TEXT("GetVirtualPathTreeRoots -> %d roots."), Entries.Num());

	return ContentBrowserJsonArrayToString(Entries);
#else
	UE_LOG(LogRevvyContentBrowser, Warning, TEXT("GetVirtualPathTreeRoots: editor-only API called from non-editor build."));
	return MakeErrorArrayJson(TEXT("editor-only API"));
#endif
}
