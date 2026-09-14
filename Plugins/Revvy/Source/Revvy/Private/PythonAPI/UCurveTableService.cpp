// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UCurveTableService.h"
#include "Engine/CurveTable.h"
#include "Curves/RealCurve.h"
#include "Curves/RichCurve.h"
#include "Curves/SimpleCurve.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/CurveTableFactory.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogUCurveTableService, Log, All);

namespace
{
	// Context string used for FindCurve diagnostics.
	static const FString CurveTableContext = TEXT("UCurveTableService");

	FString CurveTableModeToString(ECurveTableMode Mode)
	{
		switch (Mode)
		{
		case ECurveTableMode::SimpleCurves: return TEXT("SimpleCurves");
		case ECurveTableMode::RichCurves:   return TEXT("RichCurves");
		case ECurveTableMode::Empty:        return TEXT("Empty");
		default:                            return TEXT("Empty");
		}
	}
}

// ============================================
// Helper Functions
// ============================================

UCurveTable* UCurveTableService::LoadCurveTable(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}

	UCurveTable* CurveTable = Cast<UCurveTable>(UEditorAssetLibrary::LoadAsset(Path));
	if (!CurveTable)
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("Failed to load CurveTable: %s"), *Path);
	}
	return CurveTable;
}

FRealCurve* UCurveTableService::FindRealCurve(UCurveTable* CurveTable, const FString& RowName, bool bWarnIfNotFound)
{
	if (!CurveTable || RowName.IsEmpty())
	{
		return nullptr;
	}

	FRealCurve* Curve = CurveTable->FindCurveUnchecked(FName(*RowName));
	if (!Curve && bWarnIfNotFound)
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("Row '%s' not found in CurveTable '%s'"), *RowName, *CurveTable->GetPathName());
	}
	return Curve;
}

void UCurveTableService::NotifyCurveTableChanged(UCurveTable* CurveTable)
{
	if (!CurveTable)
	{
		return;
	}

	CurveTable->OnCurveTableChanged().Broadcast();
	CurveTable->MarkPackageDirty();
}

// ============================================
// Discovery Actions
// ============================================

TArray<FString> UCurveTableService::ListCurveTables(const FString& PathFilter)
{
	TArray<FString> Results;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine.CurveTable")));

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

	Results.Sort([](const FString& A, const FString& B) { return A < B; });

	return Results;
}

bool UCurveTableService::CurveTableExists(const FString& Path)
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

FString UCurveTableService::CreateCurveTable(const FString& PackagePath, const FString& AssetName, const FString& CurveMode)
{
	if (AssetName.IsEmpty())
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("CreateCurveTable: AssetName is required"));
		return FString();
	}

	// Normalize package path
	FString NormalizedPath = PackagePath;
	if (NormalizedPath.IsEmpty())
	{
		NormalizedPath = TEXT("/Game/Curves");
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
		UE_LOG(LogUCurveTableService, Warning, TEXT("CreateCurveTable: CurveTable '%s' already exists at '%s'. Delete it first or use a different name."), *AssetName, *FullAssetPath);
		return FString();
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UCurveTableFactory* Factory = NewObject<UCurveTableFactory>();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, NormalizedPath, UCurveTable::StaticClass(), Factory);
	if (!NewAsset)
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("CreateCurveTable: Failed to create CurveTable at %s/%s"), *NormalizedPath, *AssetName);
		return FString();
	}

	UCurveTable* CurveTable = Cast<UCurveTable>(NewAsset);
	if (!CurveTable)
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("CreateCurveTable: Created asset is not a CurveTable"));
		return FString();
	}

	// The factory seeds a default row and an arbitrary mode. Reset to Empty so we can
	// deterministically establish the requested mode, then lock it in by adding and
	// removing a sentinel curve of the desired type (RemoveRow keeps the concrete mode).
	CurveTable->EmptyTable();

	const bool bSimple = CurveMode.Equals(TEXT("Simple"), ESearchCase::IgnoreCase) ||
						 CurveMode.Equals(TEXT("SimpleCurves"), ESearchCase::IgnoreCase) ||
						 CurveMode.Equals(TEXT("SimpleCurve"), ESearchCase::IgnoreCase);

	const FName SentinelRow(TEXT("__RevvyModeSeed"));
	if (bSimple)
	{
		CurveTable->AddSimpleCurve(SentinelRow);
	}
	else
	{
		// "Float" (default) and any unrecognized value => RichCurves
		CurveTable->AddRichCurve(SentinelRow);
	}
	CurveTable->RemoveRow(SentinelRow);

	NotifyCurveTableChanged(CurveTable);

	UE_LOG(LogUCurveTableService, Display, TEXT("Created CurveTable: %s (mode: %s)"),
		*CurveTable->GetPathName(), *CurveTableModeToString(CurveTable->GetCurveTableMode()));

	return CurveTable->GetPathName();
}

// ============================================
// Info Actions
// ============================================

bool UCurveTableService::GetCurveTableInfo(const FString& Path, FCurveTableInfo& OutInfo)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	OutInfo.Name = CurveTable->GetName();
	OutInfo.Path = CurveTable->GetPathName();
	OutInfo.Mode = CurveTableModeToString(CurveTable->GetCurveTableMode());

	for (const TPair<FName, FRealCurve*>& Row : CurveTable->GetRowMap())
	{
		OutInfo.RowNames.Add(Row.Key.ToString());
	}

	OutInfo.RowNames.Sort([](const FString& A, const FString& B) { return A < B; });

	return true;
}

TArray<FString> UCurveTableService::ListRows(const FString& Path)
{
	TArray<FString> Results;

	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return Results;
	}

	for (const TPair<FName, FRealCurve*>& Row : CurveTable->GetRowMap())
	{
		Results.Add(Row.Key.ToString());
	}

	Results.Sort([](const FString& A, const FString& B) { return A < B; });

	return Results;
}

// ============================================
// Row Operations
// ============================================

bool UCurveTableService::AddRow(const FString& Path, const FString& RowName)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	if (RowName.IsEmpty())
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("AddRow: Row name is required"));
		return false;
	}

	const FName Row(*RowName);

	// Reject duplicates
	if (CurveTable->FindCurveUnchecked(Row))
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("AddRow: Row '%s' already exists"), *RowName);
		return false;
	}

	// Add a curve matching the table's current storage mode. An Empty table defaults
	// to RichCurves (the "Float" curve mode).
	if (CurveTable->GetCurveTableMode() == ECurveTableMode::SimpleCurves)
	{
		CurveTable->AddSimpleCurve(Row);
	}
	else
	{
		CurveTable->AddRichCurve(Row);
	}

	NotifyCurveTableChanged(CurveTable);

	return true;
}

bool UCurveTableService::RemoveRow(const FString& Path, const FString& RowName)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	if (RowName.IsEmpty())
	{
		return false;
	}

	const FName Row(*RowName);
	if (!CurveTable->FindCurveUnchecked(Row))
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("RemoveRow: Row '%s' not found"), *RowName);
		return false;
	}

	CurveTable->RemoveRow(Row);

	NotifyCurveTableChanged(CurveTable);

	return true;
}

// ============================================
// Key Operations
// ============================================

bool UCurveTableService::SetKey(const FString& Path, const FString& RowName, float Time, float Value)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	FRealCurve* Curve = FindRealCurve(CurveTable, RowName);
	if (!Curve)
	{
		return false;
	}

	// UpdateOrAddKey is virtual on FRealCurve and works for both rich and simple curves.
	Curve->UpdateOrAddKey(Time, Value);

	NotifyCurveTableChanged(CurveTable);

	return true;
}

TArray<FCurveKeyInfo> UCurveTableService::GetKeys(const FString& Path, const FString& RowName)
{
	TArray<FCurveKeyInfo> Results;

	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return Results;
	}

	FRealCurve* Curve = FindRealCurve(CurveTable, RowName);
	if (!Curve)
	{
		return Results;
	}

	// Iterate handles polymorphically and read the time/value pair for each key.
	for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
	{
		const FKeyHandle KeyHandle = *It;
		const TPair<float, float> TimeValue = Curve->GetKeyTimeValuePair(KeyHandle);

		FCurveKeyInfo KeyInfo;
		KeyInfo.Time = TimeValue.Key;
		KeyInfo.Value = TimeValue.Value;
		Results.Add(KeyInfo);
	}

	// Keys may not be returned in time order; sort for predictable output.
	Results.Sort([](const FCurveKeyInfo& A, const FCurveKeyInfo& B) { return A.Time < B.Time; });

	return Results;
}

float UCurveTableService::EvaluateCurve(const FString& Path, const FString& RowName, float Time)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return 0.0f;
	}

	FRealCurve* Curve = FindRealCurve(CurveTable, RowName);
	if (!Curve)
	{
		return 0.0f;
	}

	return Curve->Eval(Time);
}

bool UCurveTableService::DeleteRowKeys(const FString& Path, const FString& RowName)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	FRealCurve* Curve = FindRealCurve(CurveTable, RowName);
	if (!Curve)
	{
		return false;
	}

	// Reset clears all keys while keeping the row in the table.
	Curve->Reset();

	NotifyCurveTableChanged(CurveTable);

	return true;
}

// ============================================
// Persistence
// ============================================

bool UCurveTableService::SaveCurveTable(const FString& Path)
{
	UCurveTable* CurveTable = LoadCurveTable(Path);
	if (!CurveTable)
	{
		return false;
	}

	const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(CurveTable, /*bOnlyIfDirty=*/false);
	if (!bSaved)
	{
		UE_LOG(LogUCurveTableService, Warning, TEXT("SaveCurveTable: Failed to save '%s'"), *Path);
	}
	return bSaved;
}
