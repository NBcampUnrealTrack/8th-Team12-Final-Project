// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UMaterialParameterCollectionService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyMPC, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"

// Materials
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

// Factory (editor-only)
#if WITH_EDITOR
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#endif

// Asset tools
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"

// Undo/redo
#include "ScopedTransaction.h"

// Path helpers
#include "Misc/PackageName.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString MPCJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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

	/** Convert an FString error message to a {"error":"..."} JSON payload. */
	FString MPCMakeErrorJson(const FString& Msg)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("error"), Msg);
		return MPCJsonObjectToString(Obj);
	}

	/** Load an MPC asset by path with logging on failure. */
	UMaterialParameterCollection* LoadMPC(const FString& AssetPath, const TCHAR* Caller)
	{
		if (AssetPath.IsEmpty())
		{
			UE_LOG(LogRevvyMPC, Warning, TEXT("%s: empty MPCAssetPath"), Caller);
			return nullptr;
		}

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(Loaded);
		if (!MPC)
		{
			UE_LOG(LogRevvyMPC, Warning,
				TEXT("%s: failed to load UMaterialParameterCollection at '%s'"),
				Caller, *AssetPath);
			return nullptr;
		}
		return MPC;
	}

	/** Fetch the editor world's MPC instance, or nullptr with logging. */
	UMaterialParameterCollectionInstance* GetEditorInstance(
		UMaterialParameterCollection* MPC, const TCHAR* Caller)
	{
	#if WITH_EDITOR
		if (!GEditor)
		{
			UE_LOG(LogRevvyMPC, Warning, TEXT("%s: GEditor is null"), Caller);
			return nullptr;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			UE_LOG(LogRevvyMPC, Warning, TEXT("%s: editor world is null"), Caller);
			return nullptr;
		}
		UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(MPC);
		if (!Instance)
		{
			UE_LOG(LogRevvyMPC, Warning,
				TEXT("%s: world has no instance for MPC '%s'"),
				Caller, *MPC->GetPathName());
		}
		return Instance;
	#else
		UE_LOG(LogRevvyMPC, Warning, TEXT("%s: unavailable in non-editor builds"), Caller);
		return nullptr;
	#endif
	}
}

// =================================================================
// Create
// =================================================================

FString UMaterialParameterCollectionService::CreateMPC(const FString& OutputAssetPath)
{
#if WITH_EDITOR
	if (OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyMPC, Warning, TEXT("CreateMPC: empty OutputAssetPath"));
		return FString();
	}

	// Split "/Game/Foo/Bar" into ("/Game/Foo", "Bar")
	const FString PackagePath = FPackageName::GetLongPackagePath(OutputAssetPath);
	const FString AssetName   = FPackageName::GetShortName(OutputAssetPath);
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("CreateMPC: invalid OutputAssetPath '%s'"), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("MaterialParameterCollectionService", "CreateMPC", "Create Material Parameter Collection"));

	FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UMaterialParameterCollectionFactoryNew* Factory = NewObject<UMaterialParameterCollectionFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(
		AssetName, PackagePath, UMaterialParameterCollection::StaticClass(), Factory);

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(NewAsset);
	if (!MPC)
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("CreateMPC: AssetTools.CreateAsset returned null for '%s'"), *OutputAssetPath);
		return FString();
	}

	UEditorAssetLibrary::SaveLoadedAsset(MPC, /*bOnlyIfDirty=*/false);
	return MPC->GetPathName();
#else
	UE_LOG(LogRevvyMPC, Warning, TEXT("CreateMPC: unavailable in non-editor builds"));
	return FString();
#endif
}

// =================================================================
// Parameters (defaults, persisted)
// =================================================================

bool UMaterialParameterCollectionService::AddScalarParameter(
	const FString& MPCAssetPath, const FString& ParamName, float DefaultValue)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("AddScalarParameter"));
	if (!MPC)
	{
		return false;
	}
	if (ParamName.IsEmpty())
	{
		UE_LOG(LogRevvyMPC, Warning, TEXT("AddScalarParameter: empty ParamName"));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("MaterialParameterCollectionService", "AddScalarParameter", "Add MPC Scalar Parameter"));
	MPC->Modify();

	FCollectionScalarParameter NewParam;
	NewParam.ParameterName = FName(*ParamName);
	NewParam.DefaultValue  = DefaultValue;
	NewParam.Id            = FGuid::NewGuid();

	MPC->ScalarParameters.Add(NewParam);
	MPC->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(MPC, /*bOnlyIfDirty=*/false);
	return true;
}

bool UMaterialParameterCollectionService::AddVectorParameter(
	const FString& MPCAssetPath, const FString& ParamName, FLinearColor DefaultValue)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("AddVectorParameter"));
	if (!MPC)
	{
		return false;
	}
	if (ParamName.IsEmpty())
	{
		UE_LOG(LogRevvyMPC, Warning, TEXT("AddVectorParameter: empty ParamName"));
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("MaterialParameterCollectionService", "AddVectorParameter", "Add MPC Vector Parameter"));
	MPC->Modify();

	FCollectionVectorParameter NewParam;
	NewParam.ParameterName = FName(*ParamName);
	NewParam.DefaultValue  = DefaultValue;
	NewParam.Id            = FGuid::NewGuid();

	MPC->VectorParameters.Add(NewParam);
	MPC->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(MPC, /*bOnlyIfDirty=*/false);
	return true;
}

bool UMaterialParameterCollectionService::SetScalarParameterDefault(
	const FString& MPCAssetPath, const FString& ParamName, float NewDefault)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("SetScalarParameterDefault"));
	if (!MPC)
	{
		return false;
	}

	const FName TargetName(*ParamName);
	FCollectionScalarParameter* Found = MPC->ScalarParameters.FindByPredicate(
		[TargetName](const FCollectionScalarParameter& P)
		{
			return P.ParameterName == TargetName;
		});

	if (!Found)
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("SetScalarParameterDefault: scalar parameter '%s' not found in '%s'"),
			*ParamName, *MPCAssetPath);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("MaterialParameterCollectionService", "SetScalarParameterDefault", "Set MPC Scalar Default"));
	MPC->Modify();
	Found->DefaultValue = NewDefault;
	MPC->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(MPC, /*bOnlyIfDirty=*/false);
	return true;
}

bool UMaterialParameterCollectionService::SetVectorParameterDefault(
	const FString& MPCAssetPath, const FString& ParamName, FLinearColor NewDefault)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("SetVectorParameterDefault"));
	if (!MPC)
	{
		return false;
	}

	const FName TargetName(*ParamName);
	FCollectionVectorParameter* Found = MPC->VectorParameters.FindByPredicate(
		[TargetName](const FCollectionVectorParameter& P)
		{
			return P.ParameterName == TargetName;
		});

	if (!Found)
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("SetVectorParameterDefault: vector parameter '%s' not found in '%s'"),
			*ParamName, *MPCAssetPath);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("MaterialParameterCollectionService", "SetVectorParameterDefault", "Set MPC Vector Default"));
	MPC->Modify();
	Found->DefaultValue = NewDefault;
	MPC->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(MPC, /*bOnlyIfDirty=*/false);
	return true;
}

// =================================================================
// Runtime (editor world instance overrides, not persisted)
// =================================================================

bool UMaterialParameterCollectionService::SetScalarAtRuntime(
	const FString& MPCAssetPath, const FString& ParamName, float Value)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("SetScalarAtRuntime"));
	if (!MPC)
	{
		return false;
	}

	UMaterialParameterCollectionInstance* Instance = GetEditorInstance(MPC, TEXT("SetScalarAtRuntime"));
	if (!Instance)
	{
		return false;
	}

	const bool bOk = Instance->SetScalarParameterValue(FName(*ParamName), Value);
	if (!bOk)
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("SetScalarAtRuntime: scalar '%s' not found in MPC '%s'"),
			*ParamName, *MPCAssetPath);
	}
	return bOk;
}

bool UMaterialParameterCollectionService::SetVectorAtRuntime(
	const FString& MPCAssetPath, const FString& ParamName, FLinearColor Value)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("SetVectorAtRuntime"));
	if (!MPC)
	{
		return false;
	}

	UMaterialParameterCollectionInstance* Instance = GetEditorInstance(MPC, TEXT("SetVectorAtRuntime"));
	if (!Instance)
	{
		return false;
	}

	const bool bOk = Instance->SetVectorParameterValue(FName(*ParamName), Value);
	if (!bOk)
	{
		UE_LOG(LogRevvyMPC, Warning,
			TEXT("SetVectorAtRuntime: vector '%s' not found in MPC '%s'"),
			*ParamName, *MPCAssetPath);
	}
	return bOk;
}

// =================================================================
// Query
// =================================================================

FString UMaterialParameterCollectionService::ListMPCParameters(const FString& MPCAssetPath)
{
	UMaterialParameterCollection* MPC = LoadMPC(MPCAssetPath, TEXT("ListMPCParameters"));
	if (!MPC)
	{
		return MPCMakeErrorJson(FString::Printf(TEXT("failed to load MPC at '%s'"), *MPCAssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> ScalarsArr;
	ScalarsArr.Reserve(MPC->ScalarParameters.Num());
	for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    P.ParameterName.ToString());
		Entry->SetNumberField(TEXT("default"), P.DefaultValue);
		ScalarsArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TArray<TSharedPtr<FJsonValue>> VectorsArr;
	VectorsArr.Reserve(MPC->VectorParameters.Num());
	for (const FCollectionVectorParameter& P : MPC->VectorParameters)
	{
		TSharedPtr<FJsonObject> DefaultObj = MakeShared<FJsonObject>();
		DefaultObj->SetNumberField(TEXT("r"), P.DefaultValue.R);
		DefaultObj->SetNumberField(TEXT("g"), P.DefaultValue.G);
		DefaultObj->SetNumberField(TEXT("b"), P.DefaultValue.B);
		DefaultObj->SetNumberField(TEXT("a"), P.DefaultValue.A);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),    P.ParameterName.ToString());
		Entry->SetObjectField(TEXT("default"), DefaultObj);
		VectorsArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("scalars"), ScalarsArr);
	Root->SetArrayField(TEXT("vectors"), VectorsArr);
	return MPCJsonObjectToString(Root);
}
