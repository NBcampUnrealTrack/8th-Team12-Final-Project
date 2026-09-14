// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UChooserService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyChooser, Log, All);

// Core / Editor
#include "Editor.h"
#include "Engine/World.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Package.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

// Chooser plugin — runtime
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "IObjectChooser.h"
#include "IHasContext.h"
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"

// StructUtils
#include "StructUtils/InstancedStruct.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// =================================================================
// Helpers
// =================================================================

namespace RevvyChooserServiceHelpers
{
	/**
	 * Return true if the Chooser plugin runtime module is loaded. All public
	 * entry points call this first so failures degrade gracefully instead of
	 * crashing if the plugin was disabled after the plugin authored against
	 * it was built (unusual but possible in packaged editor tooling).
	 */
	static bool IsChooserPluginAvailable()
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("Chooser")))
		{
			return true;
		}
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Chooser"));
		return Plugin.IsValid() && Plugin->IsEnabled();
	}

	static UChooserTable* LoadChooserTable(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		return Cast<UChooserTable>(Loaded);
	}

	static UClass* ResolveClassByPath(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}
		// Try direct load first (handles "/Script/Engine.AnimMontage" style)
		if (UClass* Direct = LoadObject<UClass>(nullptr, *ClassPath))
		{
			return Direct;
		}
		// Fallback to soft path resolution
		FSoftObjectPath SoftPath(ClassPath);
		if (UObject* Resolved = SoftPath.TryLoad())
		{
			return Cast<UClass>(Resolved);
		}
		return nullptr;
	}

	static FString ResultTypeToString(EObjectChooserResultType Type)
	{
		switch (Type)
		{
		case EObjectChooserResultType::ObjectResult:    return TEXT("ObjectResult");
		case EObjectChooserResultType::ClassResult:     return TEXT("ClassResult");
		case EObjectChooserResultType::NoPrimaryResult: return TEXT("NoPrimaryResult");
		default:                                        return TEXT("Unknown");
		}
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

	/**
	 * Extract the "result reference" from a row FInstancedStruct if the row
	 * is one of the well-known object-reference variants (asset, class).
	 * Returns empty string for unknown / nested row types.
	 */
	static FString GetRowResultReference(const FInstancedStruct& Row)
	{
		if (!Row.IsValid())
		{
			return FString();
		}
		const UScriptStruct* StructType = Row.GetScriptStruct();
		if (!StructType)
		{
			return FString();
		}

		if (StructType == FAssetChooser::StaticStruct())
		{
			const FAssetChooser& Asset = Row.Get<FAssetChooser>();
			return Asset.Asset ? Asset.Asset->GetPathName() : FString();
		}
		if (StructType == FClassChooser::StaticStruct())
		{
			const FClassChooser& ClassRow = Row.Get<FClassChooser>();
			return ClassRow.Class ? ClassRow.Class->GetPathName() : FString();
		}
		if (StructType == FEvaluateChooser::StaticStruct())
		{
			const FEvaluateChooser& Eval = Row.Get<FEvaluateChooser>();
			return Eval.Chooser ? Eval.Chooser->GetPathName() : FString();
		}
		return FString();
	}
} // namespace

// =================================================================
// Create
// =================================================================

FString UChooserService::CreateChooserTable(
	const FString& OutputAssetPath,
	const FString& ResultTypeClassPath)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("CreateChooserTable: Chooser plugin is not enabled/loaded"));
		return FString();
	}

	if (OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyChooser, Warning, TEXT("CreateChooserTable: empty OutputAssetPath"));
		return FString();
	}

	const FString PackagePath = FPackageName::GetLongPackagePath(OutputAssetPath);
	const FString AssetName   = FPackageName::GetShortName(OutputAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("CreateChooserTable: invalid OutputAssetPath '%s'"), *OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("CreateChooserTable: asset already exists at '%s'"), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("ChooserService", "CreateChooserTable", "Create Chooser Table"));

	// Create the package directly (avoids the ChooserEditor private factory
	// which pops a schema-picker dialog).
	const FString FullPackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("CreateChooserTable: failed to create package '%s'"), *FullPackageName);
		return FString();
	}

	UChooserTable* NewTable = NewObject<UChooserTable>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewTable)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("CreateChooserTable: failed to instantiate UChooserTable"));
		return FString();
	}

	// Configure result type (defaults to ObjectResult on a fresh signature).
	NewTable->ResultType = EObjectChooserResultType::ObjectResult;

	if (!ResultTypeClassPath.IsEmpty())
	{
		if (UClass* ResultClass = ResolveClassByPath(ResultTypeClassPath))
		{
			NewTable->OutputObjectType = ResultClass;
		}
		else
		{
			UE_LOG(LogRevvyChooser, Warning,
				TEXT("CreateChooserTable: could not resolve result class '%s' — leaving OutputObjectType null"),
				*ResultTypeClassPath);
		}
	}

#if WITH_EDITORONLY_DATA
	NewTable->Version = UChooserTable::CurrentVersion;
#endif

	FAssetRegistryModule::AssetCreated(NewTable);
	Package->SetDirtyFlag(true);
	NewTable->MarkPackageDirty();
	NewTable->PostEditChange();

	// Save asset so subsequent mutations act on a persisted table.
	UEditorAssetLibrary::SaveLoadedAsset(NewTable, /*bOnlyIfDirty*/ false);

	return NewTable->GetPathName();
}

// =================================================================
// Rows
// =================================================================

int32 UChooserService::AddChooserRow(const FString& ChooserAssetPath)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("AddChooserRow: Chooser plugin is not enabled/loaded"));
		return INDEX_NONE;
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("AddChooserRow: failed to load '%s'"), *ChooserAssetPath);
		return INDEX_NONE;
	}

#if WITH_EDITORONLY_DATA
	FScopedTransaction Tx(NSLOCTEXT("ChooserService", "AddChooserRow", "Add Chooser Row"));
	Table->Modify();

	// Default row: empty asset chooser (safe default, matches editor's
	// "add row" behavior for ObjectResult choosers).
	FInstancedStruct NewRow;
	NewRow.InitializeAs(FAssetChooser::StaticStruct());
	const int32 NewIndex = Table->ResultsStructs.Add(MoveTemp(NewRow));

	// Keep parallel DisabledRows array in sync
	if (Table->DisabledRows.Num() < Table->ResultsStructs.Num())
	{
		Table->DisabledRows.SetNum(Table->ResultsStructs.Num(), EAllowShrinking::No);
	}

	Table->PostEditChange();
	Table->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfDirty*/ false);
	return NewIndex;
#else
	UE_LOG(LogRevvyChooser, Warning,
		TEXT("AddChooserRow: ResultsStructs is editor-only data; cannot mutate in a runtime build"));
	return INDEX_NONE;
#endif
}

bool UChooserService::RemoveChooserRow(const FString& ChooserAssetPath, int32 RowIndex)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("RemoveChooserRow: Chooser plugin is not enabled/loaded"));
		return false;
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("RemoveChooserRow: failed to load '%s'"), *ChooserAssetPath);
		return false;
	}

#if WITH_EDITORONLY_DATA
	if (!Table->ResultsStructs.IsValidIndex(RowIndex))
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("RemoveChooserRow: index %d out of range [0, %d)"),
			RowIndex, Table->ResultsStructs.Num());
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("ChooserService", "RemoveChooserRow", "Remove Chooser Row"));
	Table->Modify();

	Table->ResultsStructs.RemoveAt(RowIndex);
	if (Table->DisabledRows.IsValidIndex(RowIndex))
	{
		Table->DisabledRows.RemoveAt(RowIndex);
	}

	Table->PostEditChange();
	Table->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfDirty*/ false);
	return true;
#else
	UE_LOG(LogRevvyChooser, Warning,
		TEXT("RemoveChooserRow: ResultsStructs is editor-only data; cannot mutate in a runtime build"));
	return false;
#endif
}

int32 UChooserService::DuplicateChooserRow(const FString& ChooserAssetPath, int32 SourceRowIndex)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("DuplicateChooserRow: Chooser plugin is not enabled/loaded"));
		return INDEX_NONE;
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("DuplicateChooserRow: failed to load '%s'"), *ChooserAssetPath);
		return INDEX_NONE;
	}

#if WITH_EDITORONLY_DATA
	if (!Table->ResultsStructs.IsValidIndex(SourceRowIndex))
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("DuplicateChooserRow: source index %d out of range [0, %d)"),
			SourceRowIndex, Table->ResultsStructs.Num());
		return INDEX_NONE;
	}

	FScopedTransaction Tx(NSLOCTEXT("ChooserService", "DuplicateChooserRow", "Duplicate Chooser Row"));
	Table->Modify();

	// FInstancedStruct is copy-constructible and deep-copies the underlying
	// script struct payload (5.4+).
	FInstancedStruct Copy = Table->ResultsStructs[SourceRowIndex];
	const int32 NewIndex = Table->ResultsStructs.Add(MoveTemp(Copy));

	if (Table->DisabledRows.IsValidIndex(SourceRowIndex))
	{
		const bool bSourceDisabled = Table->DisabledRows[SourceRowIndex];
		if (Table->DisabledRows.Num() < Table->ResultsStructs.Num())
		{
			Table->DisabledRows.Add(bSourceDisabled);
		}
	}
	else if (Table->DisabledRows.Num() < Table->ResultsStructs.Num())
	{
		Table->DisabledRows.SetNum(Table->ResultsStructs.Num(), EAllowShrinking::No);
	}

	Table->PostEditChange();
	Table->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Table, /*bOnlyIfDirty*/ false);
	return NewIndex;
#else
	UE_LOG(LogRevvyChooser, Warning,
		TEXT("DuplicateChooserRow: ResultsStructs is editor-only data; cannot mutate in a runtime build"));
	return INDEX_NONE;
#endif
}

// =================================================================
// Query
// =================================================================

FString UChooserService::ListChooserRows(const FString& ChooserAssetPath)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("ListChooserRows: Chooser plugin is not enabled/loaded"));
		return FString();
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("ListChooserRows: failed to load '%s'"), *ChooserAssetPath);
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> Rows;

#if WITH_EDITORONLY_DATA
	for (int32 Idx = 0; Idx < Table->ResultsStructs.Num(); ++Idx)
	{
		const FInstancedStruct& Row = Table->ResultsStructs[Idx];
		TSharedRef<FJsonObject> RowObj = MakeShared<FJsonObject>();
		RowObj->SetNumberField(TEXT("index"), Idx);

		const bool bValid = Row.IsValid();
		RowObj->SetBoolField(TEXT("is_valid"), bValid);

		if (bValid)
		{
			const UScriptStruct* StructType = Row.GetScriptStruct();
			RowObj->SetStringField(TEXT("row_type"),
				StructType ? StructType->GetName() : FString());
			RowObj->SetStringField(TEXT("result_path"), GetRowResultReference(Row));
		}
		else
		{
			RowObj->SetStringField(TEXT("row_type"), FString());
			RowObj->SetStringField(TEXT("result_path"), FString());
		}

		Rows.Add(MakeShared<FJsonValueObject>(RowObj));
	}
#else
	// Cooked build: use CookedResults array
	for (int32 Idx = 0; Idx < Table->CookedResults.Num(); ++Idx)
	{
		const FInstancedStruct& Row = Table->CookedResults[Idx];
		TSharedRef<FJsonObject> RowObj = MakeShared<FJsonObject>();
		RowObj->SetNumberField(TEXT("index"), Idx);
		RowObj->SetBoolField(TEXT("is_valid"), Row.IsValid());
		const UScriptStruct* StructType = Row.GetScriptStruct();
		RowObj->SetStringField(TEXT("row_type"),
			StructType ? StructType->GetName() : FString());
		RowObj->SetStringField(TEXT("result_path"), GetRowResultReference(Row));
		Rows.Add(MakeShared<FJsonValueObject>(RowObj));
	}
#endif

	return SerializeJsonArray(Rows);
}

FString UChooserService::GetChooserTableInfo(const FString& ChooserAssetPath)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("GetChooserTableInfo: Chooser plugin is not enabled/loaded"));
		return FString();
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("GetChooserTableInfo: failed to load '%s'"), *ChooserAssetPath);
		return FString();
	}

	TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("result_type"), ResultTypeToString(Table->ResultType));
	Info->SetStringField(TEXT("output_class_path"),
		Table->OutputObjectType ? Table->OutputObjectType->GetPathName() : FString());

#if WITH_EDITORONLY_DATA
	Info->SetNumberField(TEXT("row_count"), Table->ResultsStructs.Num());
#else
	Info->SetNumberField(TEXT("row_count"), Table->CookedResults.Num());
#endif
	Info->SetNumberField(TEXT("column_count"), Table->ColumnsStructs.Num());

	TArray<TSharedPtr<FJsonValue>> Columns;
	for (int32 Idx = 0; Idx < Table->ColumnsStructs.Num(); ++Idx)
	{
		const FInstancedStruct& Col = Table->ColumnsStructs[Idx];
		TSharedRef<FJsonObject> ColObj = MakeShared<FJsonObject>();
		ColObj->SetNumberField(TEXT("index"), Idx);
		const UScriptStruct* ColStruct = Col.IsValid() ? Col.GetScriptStruct() : nullptr;
		ColObj->SetStringField(TEXT("type"),
			ColStruct ? ColStruct->GetName() : FString());
		Columns.Add(MakeShared<FJsonValueObject>(ColObj));
	}
	Info->SetArrayField(TEXT("columns"), Columns);

	return SerializeJson(Info);
}

// =================================================================
// Evaluate
// =================================================================

FString UChooserService::EvaluateChooser(
	const FString& ChooserAssetPath,
	UObject* ContextObject)
{
	using namespace RevvyChooserServiceHelpers;

	if (!IsChooserPluginAvailable())
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("EvaluateChooser: Chooser plugin is not enabled/loaded"));
		return FString();
	}

	UChooserTable* Table = LoadChooserTable(ChooserAssetPath);
	if (!Table)
	{
		UE_LOG(LogRevvyChooser, Warning,
			TEXT("EvaluateChooser: failed to load '%s'"), *ChooserAssetPath);
		return FString();
	}

	FChooserEvaluationContext EvalCtx;
	if (ContextObject)
	{
		EvalCtx.AddObjectParam(ContextObject);
	}

	UObject* Result = nullptr;
	UChooserTable::EvaluateChooser(
		EvalCtx,
		Table,
		FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda(
			[&Result](UObject* SelectedObject) -> FObjectChooserBase::EIteratorStatus
			{
				Result = SelectedObject;
				return FObjectChooserBase::EIteratorStatus::Stop;
			}));

	if (!Result)
	{
		UE_LOG(LogRevvyChooser, Log,
			TEXT("EvaluateChooser: '%s' returned no result"), *ChooserAssetPath);
		return FString();
	}

	return Result->GetPathName();
}
