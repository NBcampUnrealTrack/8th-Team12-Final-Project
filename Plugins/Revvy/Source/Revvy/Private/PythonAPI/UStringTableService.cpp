// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UStringTableService.h"
#include "ScopedTransaction.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/StringTableFactory.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogUStringTableService, Log, All);

// ============================================
// Helper Functions
// ============================================

UStringTable* UStringTableService::LoadStringTable(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}

	UStringTable* StringTable = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(Path));
	if (!StringTable)
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("Failed to load StringTable: %s"), *Path);
	}
	return StringTable;
}

// ============================================
// Discovery Actions
// ============================================

TArray<FString> UStringTableService::ListStringTables(const FString& PathFilter)
{
	TArray<FString> Results;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine.StringTable")));

	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PathFilter));
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	for (const FAssetData& AssetData : AssetDataList)
	{
		Results.Add(AssetData.GetObjectPathString());
	}

	Results.Sort();

	return Results;
}

bool UStringTableService::StringTableExists(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return false;
	}
	return UEditorAssetLibrary::DoesAssetExist(Path);
}

// ============================================
// Lifecycle Actions
// ============================================

FString UStringTableService::CreateStringTable(const FString& PackagePath, const FString& AssetName, const FString& Namespace)
{
	if (AssetName.IsEmpty())
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("CreateStringTable: AssetName is required"));
		return FString();
	}

	// Normalize the package path
	FString NormalizedPath = PackagePath;
	if (NormalizedPath.IsEmpty())
	{
		NormalizedPath = TEXT("/Game");
	}

	if (!NormalizedPath.StartsWith(TEXT("/Game")) &&
		!NormalizedPath.StartsWith(TEXT("/Engine")))
	{
		if (NormalizedPath.StartsWith(TEXT("/")))
		{
			NormalizedPath = TEXT("/Game") + NormalizedPath;
		}
		else
		{
			NormalizedPath = TEXT("/Game/") + NormalizedPath;
		}
	}

	// Avoid blocking overwrite dialog if the asset already exists
	const FString FullAssetPath = NormalizedPath / AssetName;
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("CreateStringTable: StringTable '%s' already exists at '%s'. Delete it first or use a different name."), *AssetName, *FullAssetPath);
		return FString();
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Prefer the editor factory (UStringTableFactory lives in UnrealEd).
	UStringTableFactory* Factory = NewObject<UStringTableFactory>();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, NormalizedPath, UStringTable::StaticClass(), Factory);
	if (!NewAsset)
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("CreateStringTable: Failed to create StringTable at %s/%s"), *NormalizedPath, *AssetName);
		return FString();
	}

	UStringTable* StringTable = Cast<UStringTable>(NewAsset);
	if (!StringTable)
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("CreateStringTable: Created asset is not a StringTable"));
		return FString();
	}

	// Apply the namespace if requested
	if (!Namespace.IsEmpty())
	{
		StringTable->GetMutableStringTable()->SetNamespace(Namespace);
	}

	StringTable->MarkPackageDirty();

	UE_LOG(LogUStringTableService, Display, TEXT("Created StringTable: %s"), *StringTable->GetPathName());

	return StringTable->GetPathName();
}

// ============================================
// Entry Read Actions
// ============================================

TArray<FStringTableEntryInfo> UStringTableService::GetEntries(const FString& Path)
{
	TArray<FStringTableEntryInfo> Results;

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return Results;
	}

	FStringTableConstRef Table = StringTable->GetStringTable();
	Table->EnumerateSourceStrings([&Results](const FString& InKey, const FString& InSourceString) -> bool
	{
		FStringTableEntryInfo Info;
		Info.Key = InKey;
		Info.SourceString = InSourceString;
		Results.Add(Info);
		return true; // continue enumeration
	});

	Results.Sort([](const FStringTableEntryInfo& A, const FStringTableEntryInfo& B)
	{
		return A.Key < B.Key;
	});

	return Results;
}

int32 UStringTableService::GetEntryCount(const FString& Path)
{
	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return 0;
	}

	int32 Count = 0;
	FStringTableConstRef Table = StringTable->GetStringTable();
	Table->EnumerateSourceStrings([&Count](const FString& InKey, const FString& InSourceString) -> bool
	{
		++Count;
		return true;
	});

	return Count;
}

FString UStringTableService::GetEntry(const FString& Path, const FString& Key)
{
	if (Key.IsEmpty())
	{
		return FString();
	}

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return FString();
	}

	FString SourceString;
	FStringTableConstRef Table = StringTable->GetStringTable();
	if (Table->GetSourceString(Key, SourceString))
	{
		return SourceString;
	}

	UE_LOG(LogUStringTableService, Warning, TEXT("GetEntry: Key '%s' not found in StringTable '%s'"), *Key, *Path);
	return FString();
}

// ============================================
// Entry Mutation Actions
// ============================================

bool UStringTableService::SetEntry(const FString& Path, const FString& Key, const FString& SourceString)
{
	if (Key.IsEmpty())
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("SetEntry: Key is required"));
		return false;
	}

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("StringTableService", "SetEntry", "Set String Table Entry"));
	StringTable->Modify();
	StringTable->GetMutableStringTable()->SetSourceString(Key, SourceString, FString());
	StringTable->MarkPackageDirty();

	return true;
}

bool UStringTableService::RemoveEntry(const FString& Path, const FString& Key)
{
	if (Key.IsEmpty())
	{
		return false;
	}

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return false;
	}

	FStringTableRef Table = StringTable->GetMutableStringTable();

	// Verify the key exists before mutating so we can report accurately.
	FString ExistingSource;
	if (!Table->GetSourceString(Key, ExistingSource))
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("RemoveEntry: Key '%s' not found in StringTable '%s'"), *Key, *Path);
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("StringTableService", "RemoveEntry", "Remove String Table Entry"));
	StringTable->Modify();
	Table->RemoveSourceString(Key);
	StringTable->MarkPackageDirty();

	return true;
}

bool UStringTableService::SetMetaData(const FString& Path, const FString& Key, const FString& MetaDataId, const FString& Value)
{
	if (Key.IsEmpty() || MetaDataId.IsEmpty())
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("SetMetaData: Key and MetaDataId are required"));
		return false;
	}

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return false;
	}

	FStringTableRef Table = StringTable->GetMutableStringTable();

	// Metadata is keyed off an existing source string entry; warn if missing.
	FString ExistingSource;
	if (!Table->GetSourceString(Key, ExistingSource))
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("SetMetaData: Key '%s' not found in StringTable '%s'. Add the entry with SetEntry first."), *Key, *Path);
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("StringTableService", "SetMetaData", "Set String Table Metadata"));
	StringTable->Modify();
	Table->SetMetaData(Key, FName(*MetaDataId), Value);
	StringTable->MarkPackageDirty();

	return true;
}

// ============================================
// Persistence
// ============================================

bool UStringTableService::SaveStringTable(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return false;
	}

	UStringTable* StringTable = LoadStringTable(Path);
	if (!StringTable)
	{
		return false;
	}

	const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(StringTable, /*bOnlyIfIsDirty=*/false);
	if (!bSaved)
	{
		UE_LOG(LogUStringTableService, Warning, TEXT("SaveStringTable: Failed to save StringTable '%s'"), *Path);
	}

	return bSaved;
}
