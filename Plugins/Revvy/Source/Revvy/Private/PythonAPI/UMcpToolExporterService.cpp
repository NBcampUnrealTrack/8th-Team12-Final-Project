// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UMcpToolExporterService.h"

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
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyMcpExporter, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON array of tool objects to a pretty-printed FString. */
	FString JsonArrayToPrettyString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** True if the class was cooked from the Revvy plugin module. */
	bool McpExportIsRevvyClass(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString PathName = Cls->GetPathName();
		return PathName.StartsWith(TEXT("/Script/Revvy."));
	}

	/** True if the class name matches the "U*Service" service convention. */
	bool McpExportIsServiceClassName(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString Name   = Cls->GetName();
		const FString Prefix = Cls->GetPrefixCPP();
		return Prefix == TEXT("U") && Name.EndsWith(TEXT("Service"));
	}

	/** Gather every Revvy "U*Service" class currently loaded. */
	void McpExportCollectRevvyServiceClasses(TArray<UClass*>& OutClasses)
	{
		OutClasses.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Cls = *It;
			if (!Cls) { continue; }
			if (!McpExportIsRevvyClass(Cls)) { continue; }
			if (!McpExportIsServiceClassName(Cls)) { continue; }
			OutClasses.Add(Cls);
		}
	}

	/** Resolve a bare or U-prefixed class name to a Revvy UClass, or nullptr. */
	UClass* McpExportResolveRevvyServiceClass(const FString& ServiceClassName)
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

	/**
	 * Build a JSON Schema type node from a UE C++ type string.
	 *
	 * Returns an FJsonObject so callers can attach "description" or other
	 * annotations without re-parsing a string.
	 */
	TSharedPtr<FJsonObject> MakeJsonSchemaTypeNode(const FString& CppType)
	{
		TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();

		// TArray<Inner> → { "type": "array", "items": <inner> }
		if (CppType.StartsWith(TEXT("TArray<")) && CppType.EndsWith(TEXT(">")))
		{
			const int32 InnerStart = 7; // after "TArray<"
			const int32 InnerLen   = CppType.Len() - InnerStart - 1;
			if (InnerLen > 0)
			{
				const FString Inner = CppType.Mid(InnerStart, InnerLen).TrimStartAndEnd();
				Node->SetStringField(TEXT("type"), TEXT("array"));
				Node->SetObjectField(TEXT("items"), MakeJsonSchemaTypeNode(Inner));
				return Node;
			}
			Node->SetStringField(TEXT("type"), TEXT("array"));
			return Node;
		}

		// FVector → fixed-length numeric array [x, y, z]
		if (CppType == TEXT("FVector"))
		{
			Node->SetStringField(TEXT("type"), TEXT("array"));
			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("number"));
			Node->SetObjectField(TEXT("items"), Items);
			Node->SetNumberField(TEXT("minItems"), 3);
			Node->SetNumberField(TEXT("maxItems"), 3);
			return Node;
		}

		if (CppType == TEXT("FVector2D"))
		{
			Node->SetStringField(TEXT("type"), TEXT("array"));
			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("number"));
			Node->SetObjectField(TEXT("items"), Items);
			Node->SetNumberField(TEXT("minItems"), 2);
			Node->SetNumberField(TEXT("maxItems"), 2);
			return Node;
		}

		if (CppType == TEXT("FRotator"))
		{
			Node->SetStringField(TEXT("type"), TEXT("array"));
			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("number"));
			Node->SetObjectField(TEXT("items"), Items);
			Node->SetNumberField(TEXT("minItems"), 3);
			Node->SetNumberField(TEXT("maxItems"), 3);
			return Node;
		}

		if (CppType == TEXT("FQuat") || CppType == TEXT("FLinearColor") || CppType == TEXT("FColor"))
		{
			Node->SetStringField(TEXT("type"), TEXT("array"));
			TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
			Items->SetStringField(TEXT("type"), TEXT("number"));
			Node->SetObjectField(TEXT("items"), Items);
			Node->SetNumberField(TEXT("minItems"), 4);
			Node->SetNumberField(TEXT("maxItems"), 4);
			return Node;
		}

		// Scalar types → JSON Schema primitive type
		if (CppType == TEXT("FString") || CppType == TEXT("FName") || CppType == TEXT("FText"))
		{
			Node->SetStringField(TEXT("type"), TEXT("string"));
			return Node;
		}
		if (CppType == TEXT("int32") || CppType == TEXT("int64") ||
			CppType == TEXT("uint8") || CppType == TEXT("uint32") || CppType == TEXT("uint64"))
		{
			Node->SetStringField(TEXT("type"), TEXT("integer"));
			return Node;
		}
		if (CppType == TEXT("float") || CppType == TEXT("double"))
		{
			Node->SetStringField(TEXT("type"), TEXT("number"));
			return Node;
		}
		if (CppType == TEXT("bool"))
		{
			Node->SetStringField(TEXT("type"), TEXT("boolean"));
			return Node;
		}

		// Unknown / complex struct → open object
		Node->SetStringField(TEXT("type"), TEXT("object"));
		return Node;
	}

	/**
	 * Build one MCP tool JSON object for a single BlueprintCallable UFUNCTION.
	 *
	 * Shape: { "name", "description", "inputSchema": { "type", "properties", "required" } }
	 */
	TSharedPtr<FJsonObject> BuildMcpToolObject(UClass* OwnerClass, UFunction* Fn)
	{
		if (!OwnerClass || !Fn) { return nullptr; }

		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();

		// name: "<ServiceName>_<UfnName>"
		const FString ToolName = FString::Printf(TEXT("%s_%s"), *OwnerClass->GetName(), *Fn->GetName());
		Tool->SetStringField(TEXT("name"), ToolName);

		// description: "<Category> - <Tooltip>"
		const FString Category = Fn->GetMetaData(TEXT("Category"));
		const FString Tooltip  = Fn->GetMetaData(TEXT("ToolTip"));
		FString Description;
		if (!Category.IsEmpty() && !Tooltip.IsEmpty())
		{
			Description = FString::Printf(TEXT("%s - %s"), *Category, *Tooltip);
		}
		else if (!Category.IsEmpty())
		{
			Description = Category;
		}
		else if (!Tooltip.IsEmpty())
		{
			Description = Tooltip;
		}
		else
		{
			Description = Fn->GetName();
		}
		Tool->SetStringField(TEXT("description"), Description);

		// inputSchema
		TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;

		for (TFieldIterator<FProperty> PropIt(Fn); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) { continue; }
			if (!Prop->HasAnyPropertyFlags(CPF_Parm)) { continue; }
			if (Prop->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
			// Skip out params — they are not consumed as tool inputs.
			if (Prop->HasAnyPropertyFlags(CPF_OutParm) && !Prop->HasAnyPropertyFlags(CPF_ReferenceParm))
			{
				continue;
			}

			const FString ParamName = Prop->GetName();
			const FString CppType   = Prop->GetCPPType();

			TSharedPtr<FJsonObject> TypeNode = MakeJsonSchemaTypeNode(CppType);
			TypeNode->SetStringField(TEXT("description"), ParamName);

			Properties->SetObjectField(ParamName, TypeNode);

			// Determine "required": param has no default value metadata.
			const FString DefaultMetaKey = FString::Printf(TEXT("CPP_Default_%s"), *ParamName);
			const bool bHasDefault = Fn->HasMetaData(*DefaultMetaKey);
			if (!bHasDefault)
			{
				Required.Add(MakeShared<FJsonValueString>(ParamName));
			}
		}

		InputSchema->SetObjectField(TEXT("properties"), Properties);
		InputSchema->SetArrayField(TEXT("required"), Required);

		Tool->SetObjectField(TEXT("inputSchema"), InputSchema);
		return Tool;
	}

	/** Collect every BlueprintCallable UFUNCTION on the given class as MCP tool objects. */
	void CollectMcpToolsForClass(UClass* Cls, TArray<TSharedPtr<FJsonValue>>& OutTools)
	{
		if (!Cls) { return; }

		for (TFieldIterator<UFunction> It(Cls); It; ++It)
		{
			UFunction* Fn = *It;
			if (!Fn) { continue; }
			if (!Fn->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { continue; }

			TSharedPtr<FJsonObject> ToolObj = BuildMcpToolObject(Cls, Fn);
			if (!ToolObj.IsValid()) { continue; }

			OutTools.Add(MakeShared<FJsonValueObject>(ToolObj));
		}
	}

	/** Write a pretty-printed JSON array to disk, creating parent dirs as needed. */
	bool WriteJsonArrayToFile(const TArray<TSharedPtr<FJsonValue>>& Tools, const FString& AbsPath)
	{
		const FString DirPart = FPaths::GetPath(AbsPath);
		if (!DirPart.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*DirPart, /*Tree=*/true);
		}

		const FString Body = JsonArrayToPrettyString(Tools);
		return FFileHelper::SaveStringToFile(Body, *AbsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

// =================================================================
// ExportServiceAsMcpToolsJson
// =================================================================

FString UMcpToolExporterService::ExportServiceAsMcpToolsJson(const FString& ServiceClassName, const FString& OutputPath)
{
	if (ServiceClassName.IsEmpty() || OutputPath.IsEmpty())
	{
		UE_LOG(LogRevvyMcpExporter, Warning,
			TEXT("ExportServiceAsMcpToolsJson: empty ServiceClassName ('%s') or OutputPath ('%s')."),
			*ServiceClassName, *OutputPath);
		return FString();
	}

	UClass* Cls = McpExportResolveRevvyServiceClass(ServiceClassName);
	if (!Cls)
	{
		UE_LOG(LogRevvyMcpExporter, Warning,
			TEXT("ExportServiceAsMcpToolsJson: class '%s' not found under /Script/Revvy."),
			*ServiceClassName);
		return FString();
	}

	TArray<TSharedPtr<FJsonValue>> Tools;
	CollectMcpToolsForClass(Cls, Tools);

	const FString AbsPath = FPaths::ConvertRelativePathToFull(OutputPath);
	if (!WriteJsonArrayToFile(Tools, AbsPath))
	{
		UE_LOG(LogRevvyMcpExporter, Error,
			TEXT("ExportServiceAsMcpToolsJson: failed to write '%s'."), *AbsPath);
		return FString();
	}

	UE_LOG(LogRevvyMcpExporter, Log,
		TEXT("ExportServiceAsMcpToolsJson: wrote %d MCP tool defs for '%s' → '%s'."),
		Tools.Num(), *Cls->GetName(), *AbsPath);
	return AbsPath;
}

// =================================================================
// ExportAllServicesAsMcpToolsJson
// =================================================================

FString UMcpToolExporterService::ExportAllServicesAsMcpToolsJson(const FString& OutputPath)
{
	if (OutputPath.IsEmpty())
	{
		UE_LOG(LogRevvyMcpExporter, Warning, TEXT("ExportAllServicesAsMcpToolsJson: empty OutputPath."));
		return FString();
	}

	TArray<UClass*> ServiceClasses;
	McpExportCollectRevvyServiceClasses(ServiceClasses);

	TArray<TSharedPtr<FJsonValue>> AllTools;
	for (UClass* Cls : ServiceClasses)
	{
		CollectMcpToolsForClass(Cls, AllTools);
	}

	const FString AbsPath = FPaths::ConvertRelativePathToFull(OutputPath);
	if (!WriteJsonArrayToFile(AllTools, AbsPath))
	{
		UE_LOG(LogRevvyMcpExporter, Error,
			TEXT("ExportAllServicesAsMcpToolsJson: failed to write '%s'."), *AbsPath);
		return FString();
	}

	UE_LOG(LogRevvyMcpExporter, Log,
		TEXT("ExportAllServicesAsMcpToolsJson: wrote %d MCP tool defs across %d services → '%s'."),
		AllTools.Num(), ServiceClasses.Num(), *AbsPath);
	return AbsPath;
}
