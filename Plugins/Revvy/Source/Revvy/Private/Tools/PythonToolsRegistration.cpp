// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Tools/PythonTools.h"
#include "Tools/PythonDiscoveryService.h"
#include "Core/ToolRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Helper function to extract a field from ParamsJson
static FString PythonToolsExtractParamFromJson(const TMap<FString, FString>& Params, const FString& FieldName)
{
	// First check if parameter exists directly (case-insensitive check for action/Action)
	const FString* DirectParam = Params.Find(FieldName);
	if (DirectParam)
	{
		return *DirectParam;
	}
	
	// Also check capitalized version (MCP server capitalizes 'action' to 'Action')
	FString CapitalizedField = FieldName;
	if (CapitalizedField.Len() > 0)
	{
		CapitalizedField[0] = FChar::ToUpper(CapitalizedField[0]);
	}
	DirectParam = Params.Find(CapitalizedField);
	if (DirectParam)
	{
		return *DirectParam;
	}

	// Otherwise, try to extract from ParamsJson
	const FString* ParamsJsonStr = Params.Find(TEXT("ParamsJson"));
	if (!ParamsJsonStr)
	{
		return FString();
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ParamsJsonStr);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return FString();
	}

	FString Value;
	if (JsonObj->TryGetStringField(FieldName, Value))
	{
		return Value;
	}

	return FString();
}

// Helper to extract with default value
static FString PythonToolsExtractParamWithDefault(const TMap<FString, FString>& Params, const FString& FieldName, const FString& DefaultValue)
{
	FString Value = PythonToolsExtractParamFromJson(Params, FieldName);
	return Value.IsEmpty() ? DefaultValue : Value;
}

// Helper to extract boolean
static bool PythonToolsExtractBoolParam(const TMap<FString, FString>& Params, const FString& FieldName, bool DefaultValue)
{
	FString Value = PythonToolsExtractParamFromJson(Params, FieldName);
	if (Value.IsEmpty())
	{
		return DefaultValue;
	}
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1"));
}

// Helper to extract integer
static int32 PythonToolsExtractIntParam(const TMap<FString, FString>& Params, const FString& FieldName, int32 DefaultValue)
{
	FString Value = PythonToolsExtractParamFromJson(Params, FieldName);
	if (Value.IsEmpty())
	{
		return DefaultValue;
	}
	return FCString::Atoi(*Value);
}

// Register execute_python_code tool
REGISTER_REVVY_TOOL(execute_python_code,
	"Execute Python code in Unreal Engine. IMPORTANT: Use 'import unreal' (lowercase). For subsystems use: unreal.get_editor_subsystem(unreal.LevelEditorSubsystem). Returns stdout, stderr, and execution status.",
	"Python",
	TOOL_PARAMS(
		TOOL_PARAM("code", "Python code to execute. Must start with 'import unreal' (lowercase). For editor subsystems use unreal.get_editor_subsystem()", "string", true)
	),
	{
		FString Code = PythonToolsExtractParamFromJson(Params, TEXT("code"));
		return UPythonTools::ExecutePythonCode(Code);
	}
);

// Register discover_python_module tool
REGISTER_REVVY_TOOL(discover_python_module,
	"Discover contents of a Python module. IMPORTANT: The module name is 'unreal' (lowercase, not 'Unreal'). Use this before execute_python_code to find available classes/functions.",
	"Python",
	TOOL_PARAMS(
		TOOL_PARAM("module_name", "Name of the Python module. Use 'unreal' (lowercase) for Unreal Engine APIs", "string", true),
		TOOL_PARAM("name_filter", "Filter results by name substring (case-insensitive). E.g. 'Blueprint' to find Blueprint-related items", "string", false),
		TOOL_PARAM("max_items", "Maximum items to return (default 100, 0 = unlimited). Use lower values to prevent context blowout", "number", false),
		TOOL_PARAM("include_classes", "Include classes in results (default true)", "boolean", false),
		TOOL_PARAM("include_functions", "Include functions in results (default true)", "boolean", false),
		TOOL_PARAM("case_sensitive", "Whether filtering is case-sensitive (default false)", "boolean", false)
	),
	{
		FString ModuleName = PythonToolsExtractParamFromJson(Params, TEXT("module_name"));
		FString NameFilter = PythonToolsExtractParamWithDefault(Params, TEXT("name_filter"), TEXT(""));
		int32 MaxItems = PythonToolsExtractIntParam(Params, TEXT("max_items"), 100);
		bool IncludeClasses = PythonToolsExtractBoolParam(Params, TEXT("include_classes"), true);
		bool IncludeFunctions = PythonToolsExtractBoolParam(Params, TEXT("include_functions"), true);
		bool CaseSensitive = PythonToolsExtractBoolParam(Params, TEXT("case_sensitive"), false);
		
		auto Service = UPythonTools::GetDiscoveryService();
		if (!Service.IsValid())
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(TEXT("success"), false);
			ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
			ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
			FString JsonString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
			FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
			return JsonString;
		}
		
		auto Result = Service->DiscoverUnrealModule(1, NameFilter, MaxItems, IncludeClasses, IncludeFunctions, CaseSensitive);
		if (Result.IsError())
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(TEXT("success"), false);
			ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
			ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());
			FString JsonString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
			FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
			return JsonString;
		}
		
		return UPythonTools::ConvertModuleInfoToJson(Result.GetValue());
	}
);

// Register discover_python_class tool
REGISTER_REVVY_TOOL(discover_python_class,
	"Discover methods and attributes of a Python class with optional filtering.",
	"Python",
	TOOL_PARAMS(
		TOOL_PARAM("class_name", "Fully qualified class name, or a comma-separated list to discover several classes in one call (e.g. 'unreal.BlueprintService, unreal.MaterialService')", "string", true),
		TOOL_PARAM("method_filter", "Filter methods by name substring (case-insensitive). '|' ORs keywords: 'create|delete|compile' matches any of them", "string", false),
		TOOL_PARAM("max_methods", "Maximum methods to return (default 0 = no explicit cap). Large classes are still bounded by a response budget: docstrings are shortened first, then trailing methods are held back, and the reply says so via docstrings_trimmed/truncated", "number", false),
		TOOL_PARAM("include_inherited", "Include inherited methods (default false). Set true for all methods including base class", "boolean", false),
		TOOL_PARAM("include_private", "Include private methods starting with _ (default false)", "boolean", false)
	),
	{
		FString ClassName = PythonToolsExtractParamFromJson(Params, TEXT("class_name"));
		FString MethodFilter = PythonToolsExtractParamWithDefault(Params, TEXT("method_filter"), TEXT(""));
		int32 MaxMethods = PythonToolsExtractIntParam(Params, TEXT("max_methods"), 0);
		bool IncludeInherited = PythonToolsExtractBoolParam(Params, TEXT("include_inherited"), false);
		bool IncludePrivate = PythonToolsExtractBoolParam(Params, TEXT("include_private"), false);
		
		auto Service = UPythonTools::GetDiscoveryService();
		if (!Service.IsValid())
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(TEXT("success"), false);
			ErrorObj->SetStringField(TEXT("error_code"), TEXT("PYTHON_SERVICE_UNAVAILABLE"));
			ErrorObj->SetStringField(TEXT("error_message"), TEXT("Python discovery service is not available"));
			FString JsonString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
			FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
			return JsonString;
		}
		
		// Comma-separated class list → batched discovery: one call, N classes.
		TArray<FString> ClassNames;
		ClassName.ParseIntoArray(ClassNames, TEXT(","), true);
		for (FString& Name : ClassNames)
		{
			Name.TrimStartAndEndInline();
		}
		ClassNames.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });

		if (ClassNames.Num() <= 1)
		{
			auto Result = Service->DiscoverClass(ClassName.TrimStartAndEnd(), MethodFilter, MaxMethods, IncludeInherited, IncludePrivate);
			if (Result.IsError())
			{
				TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
				ErrorObj->SetBoolField(TEXT("success"), false);
				ErrorObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
				ErrorObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());
				FString JsonString;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
				FJsonSerializer::Serialize(ErrorObj.ToSharedRef(), Writer);
				return JsonString;
			}
			return UPythonTools::ConvertClassInfoToJson(Result.GetValue());
		}

		// Multi-class: emit {"success": true, "classes": [<per-class result>...]}.
		// Per-class errors are embedded so one bad name doesn't sink the batch.
		TArray<TSharedPtr<FJsonValue>> ClassResults;
		for (const FString& Name : ClassNames)
		{
			auto Result = Service->DiscoverClass(Name, MethodFilter, MaxMethods, IncludeInherited, IncludePrivate);
			TSharedPtr<FJsonObject> EntryObj;
			if (Result.IsError())
			{
				EntryObj = MakeShared<FJsonObject>();
				EntryObj->SetStringField(TEXT("name"), Name);
				EntryObj->SetBoolField(TEXT("success"), false);
				EntryObj->SetStringField(TEXT("error_code"), Result.GetErrorCode());
				EntryObj->SetStringField(TEXT("error_message"), Result.GetErrorMessage());
			}
			else
			{
				const FString EntryJson = UPythonTools::ConvertClassInfoToJson(Result.GetValue());
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EntryJson);
				if (!FJsonSerializer::Deserialize(Reader, EntryObj) || !EntryObj.IsValid())
				{
					EntryObj = MakeShared<FJsonObject>();
					EntryObj->SetStringField(TEXT("name"), Name);
					EntryObj->SetBoolField(TEXT("success"), false);
					EntryObj->SetStringField(TEXT("error_message"), TEXT("Failed to serialize class info"));
				}
			}
			ClassResults.Add(MakeShared<FJsonValueObject>(EntryObj));
		}

		TSharedPtr<FJsonObject> BatchObj = MakeShared<FJsonObject>();
		BatchObj->SetBoolField(TEXT("success"), true);
		BatchObj->SetArrayField(TEXT("classes"), ClassResults);
		FString BatchJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BatchJson);
		FJsonSerializer::Serialize(BatchObj.ToSharedRef(), Writer);
		return BatchJson;
	}
);

// Register discover_python_function tool
REGISTER_REVVY_TOOL(discover_python_function,
	"Get signature and documentation for a Python function.",
	"Python",
	TOOL_PARAMS(
		TOOL_PARAM("function_name", "Fully qualified function name (e.g. 'unreal.load_asset'). Alias: function_path", "string", true)
	),
	{
		// Support both function_name and function_path parameter names
		FString FunctionName = PythonToolsExtractParamFromJson(Params, TEXT("function_name"));
		if (FunctionName.IsEmpty())
		{
			FunctionName = PythonToolsExtractParamFromJson(Params, TEXT("function_path"));
		}
		return UPythonTools::DiscoverPythonFunction(FunctionName);
	}
);

// Register list_python_subsystems tool
REGISTER_REVVY_TOOL(list_python_subsystems,
	"List all Unreal Engine editor subsystems. Access via: unreal.get_editor_subsystem(unreal.SubsystemName). Example: subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)",
	"Python",
	TOOL_PARAMS(),
	{
		return UPythonTools::ListPythonSubsystems();
	}
);
