// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPythonBindingExportService.h"

// Editor / Engine
#include "Editor.h"

// UObject reflection
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// File IO
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPyExport, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON array to a compact FString. */
	FString PyExportJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** True if the class was cooked from the Revvy plugin module. */
	bool PyExportIsRevvyClass(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString PathName = Cls->GetPathName();
		return PathName.StartsWith(TEXT("/Script/Revvy."));
	}

	/** True if the class name matches the "U*Service" service convention. */
	bool PyExportIsServiceClassName(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString Name = Cls->GetName();
		const FString Prefix = Cls->GetPrefixCPP();
		return Prefix == TEXT("U") && Name.EndsWith(TEXT("Service"));
	}

	/** Gather every Revvy "U*Service" class currently loaded. Mirrors iter63. */
	void PyExportCollectRevvyServiceClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls) { continue; }
			if (!PyExportIsRevvyClass(Cls)) { continue; }
			if (!PyExportIsServiceClassName(Cls)) { continue; }
			OutClasses.Add(Cls);
		}
	}

	/** Convert a PascalCase UFUNCTION name to snake_case as Python sees it. */
	FString PascalToSnake(const FString& Pascal)
	{
		FString Out;
		Out.Reserve(Pascal.Len() + 8);
		for (int32 i = 0; i < Pascal.Len(); ++i)
		{
			const TCHAR Ch = Pascal[i];
			if (i > 0 && FChar::IsUpper(Ch))
			{
				Out.AppendChar(TEXT('_'));
			}
			Out.AppendChar(FChar::ToLower(Ch));
		}
		return Out;
	}

	/** Map a UE C++ type string to its Python (unreal.*) type stub form. */
	FString MapCppTypeToPython(const FString& CppType)
	{
		// Handle TArray<Inner> → list[mapped_inner]
		if (CppType.StartsWith(TEXT("TArray<")) && CppType.EndsWith(TEXT(">")))
		{
			const int32 InnerStart = 7; // after "TArray<"
			const int32 InnerLen   = CppType.Len() - InnerStart - 1;
			if (InnerLen > 0)
			{
				const FString Inner = CppType.Mid(InnerStart, InnerLen).TrimStartAndEnd();
				return FString::Printf(TEXT("list[%s]"), *MapCppTypeToPython(Inner));
			}
			return TEXT("list[Any]");
		}

		if (CppType == TEXT("FString"))       { return TEXT("str"); }
		if (CppType == TEXT("FName"))         { return TEXT("str"); }
		if (CppType == TEXT("FText"))         { return TEXT("str"); }
		if (CppType == TEXT("int32"))         { return TEXT("int"); }
		if (CppType == TEXT("int64"))         { return TEXT("int"); }
		if (CppType == TEXT("uint8"))         { return TEXT("int"); }
		if (CppType == TEXT("uint32"))        { return TEXT("int"); }
		if (CppType == TEXT("float"))         { return TEXT("float"); }
		if (CppType == TEXT("double"))        { return TEXT("float"); }
		if (CppType == TEXT("bool"))          { return TEXT("bool"); }
		if (CppType == TEXT("void"))          { return TEXT("None"); }
		if (CppType == TEXT("FVector"))       { return TEXT("unreal.Vector"); }
		if (CppType == TEXT("FVector2D"))     { return TEXT("unreal.Vector2D"); }
		if (CppType == TEXT("FRotator"))      { return TEXT("unreal.Rotator"); }
		if (CppType == TEXT("FTransform"))    { return TEXT("unreal.Transform"); }
		if (CppType == TEXT("FLinearColor"))  { return TEXT("unreal.LinearColor"); }
		if (CppType == TEXT("FColor"))        { return TEXT("unreal.Color"); }
		if (CppType == TEXT("FQuat"))         { return TEXT("unreal.Quat"); }

		return TEXT("Any");
	}

	/** Build one ".pyi" def-line for a single BlueprintCallable UFUNCTION. */
	FString BuildPyiDefLine(UFunction* Fn)
	{
		if (!Fn) { return FString(); }

		FString ReturnPy = TEXT("None");
		TArray<FString> ParamPieces;

		for (TFieldIterator<FProperty> PropIt(Fn); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) { continue; }
			if (!Prop->HasAnyPropertyFlags(CPF_Parm)) { continue; }

			const FString CppType = Prop->GetCPPType();
			const FString PyType  = MapCppTypeToPython(CppType);

			if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnPy = PyType;
			}
			else
			{
				const FString PName = PascalToSnake(Prop->GetName());
				ParamPieces.Add(FString::Printf(TEXT("%s: %s"), *PName, *PyType));
			}
		}

		const FString PyFnName = PascalToSnake(Fn->GetName());
		FString ParamsJoined   = FString::Join(ParamPieces, TEXT(", "));
		if (!ParamsJoined.IsEmpty())
		{
			ParamsJoined = FString::Printf(TEXT("cls, %s"), *ParamsJoined);
		}
		else
		{
			ParamsJoined = TEXT("cls");
		}

		return FString::Printf(TEXT("    @classmethod\n    def %s(%s) -> %s: ...\n"),
			*PyFnName, *ParamsJoined, *ReturnPy);
	}

	/** Compose the full .pyi document body for a single service class. */
	FString BuildPyiForClass(UClass* Cls)
	{
		if (!Cls) { return FString(); }

		FString Body;
		Body.Reserve(1024);

		Body += TEXT("# Auto-generated by UPythonBindingExportService — do not edit by hand.\n");
		Body += TEXT("from typing import Any\n");
		Body += TEXT("import unreal\n\n");
		Body += FString::Printf(TEXT("class %s:\n"), *Cls->GetName());

		int32 EmittedCount = 0;
		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			UFunction* Fn = *It;
			if (!Fn) { continue; }
			if (!Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { continue; }

			Body += BuildPyiDefLine(Fn);
			++EmittedCount;
		}

		if (EmittedCount == 0)
		{
			Body += TEXT("    pass\n");
		}

		return Body;
	}

	/** Resolve a bare or U-prefixed class name to a Revvy UClass, or nullptr. */
	UClass* PyExportResolveRevvyServiceClass(const FString& ServiceClassName)
	{
		if (ServiceClassName.IsEmpty()) { return nullptr; }

		FString BareName = ServiceClassName;
		if (BareName.StartsWith(TEXT("U")))
		{
			BareName.RemoveAt(0, 1);
		}
		const FString FullPath = FString::Printf(TEXT("/Script/Revvy.%s"), *BareName);
		return FindObject<UClass>(nullptr, *FullPath);
	}
}

// =================================================================
// ExportServiceToPyi
// =================================================================

FString UPythonBindingExportService::ExportServiceToPyi(const FString& ServiceClassName, const FString& OutputPath)
{
	if (ServiceClassName.IsEmpty() || OutputPath.IsEmpty())
	{
		UE_LOG(LogRevvyPyExport, Warning,
			TEXT("ExportServiceToPyi: empty ServiceClassName ('%s') or OutputPath ('%s')."),
			*ServiceClassName, *OutputPath);
		return FString();
	}

	UClass* Cls = PyExportResolveRevvyServiceClass(ServiceClassName);
	if (!Cls)
	{
		UE_LOG(LogRevvyPyExport, Warning,
			TEXT("ExportServiceToPyi: class '%s' not found under /Script/Revvy."),
			*ServiceClassName);
		return FString();
	}

	const FString Body = BuildPyiForClass(Cls);

	// Ensure the parent directory exists.
	const FString AbsPath = FPaths::ConvertRelativePathToFull(OutputPath);
	const FString DirPart = FPaths::GetPath(AbsPath);
	if (!DirPart.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*DirPart, /*Tree=*/true);
	}

	if (!FFileHelper::SaveStringToFile(Body, *AbsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRevvyPyExport, Error,
			TEXT("ExportServiceToPyi: failed to write '%s'."), *AbsPath);
		return FString();
	}

	UE_LOG(LogRevvyPyExport, Log,
		TEXT("ExportServiceToPyi: wrote %d bytes for '%s' → '%s'."),
		Body.Len(), *Cls->GetName(), *AbsPath);
	return AbsPath;
}

// =================================================================
// ExportAllServicesToPyi
// =================================================================

FString UPythonBindingExportService::ExportAllServicesToPyi(const FString& OutputDirectory)
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	if (OutputDirectory.IsEmpty())
	{
		UE_LOG(LogRevvyPyExport, Warning, TEXT("ExportAllServicesToPyi: empty OutputDirectory."));
		return PyExportJsonArrayToString(Entries);
	}

	const FString AbsDir = FPaths::ConvertRelativePathToFull(OutputDirectory);
	IFileManager::Get().MakeDirectory(*AbsDir, /*Tree=*/true);

	TArray<UClass*> ServiceClasses;
	PyExportCollectRevvyServiceClasses(ServiceClasses);

	Entries.Reserve(ServiceClasses.Num());
	for (UClass* Cls : ServiceClasses)
	{
		if (!Cls) { continue; }

		const FString ClassName = Cls->GetName();
		const FString FileName  = FString::Printf(TEXT("%s.pyi"), *PascalToSnake(ClassName));
		const FString FilePath  = FPaths::Combine(AbsDir, FileName);

		const FString WrittenPath = ExportServiceToPyi(ClassName, FilePath);
		if (WrittenPath.IsEmpty()) { continue; }

		const int64 SizeBytes = IFileManager::Get().FileSize(*WrittenPath);

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("service"), ClassName);
		Entry->SetStringField(TEXT("path"),    WrittenPath);
		Entry->SetNumberField(TEXT("size"),    static_cast<double>(SizeBytes));

		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	UE_LOG(LogRevvyPyExport, Log,
		TEXT("ExportAllServicesToPyi: wrote %d stub files → '%s'."),
		Entries.Num(), *AbsDir);
	return PyExportJsonArrayToString(Entries);
}
