// Copyright Buckley Builds LLC 2026 All Rights Reserved.

// Reading a Blueprint's structure, natively.
//
// This exists because the alternative was worse. Blueprint structure was only
// reachable by sending a Python snippet through execute_python_code, and that
// route carries three costs a native tool does not:
//
// 1. execute_python_code is annotated destructive, so IsSafeReadOnlyTool()
//    rejects it and a read-only session has *zero* Blueprint capability. Merely
//    reading a Blueprint should not require handing out write access to the
//    whole editor.
// 2. It needs Unreal's Python plugin enabled. A project that has it switched
//    off loses the feature entirely, for a read that needs no interpreter.
// 3. The asset path had to be escaped into generated source, because a quote in
//    an asset name would otherwise end the string literal and let whatever
//    followed run. Here the path is a parameter and never becomes code.
//
// Annotated read-only, so it survives a read-only session and skips the
// FScopedTransaction wrapper the registry puts around write tools.

#include "Core/ToolRegistry.h"
#include "PythonAPI/UBlueprintService.h"
#include "PythonAPI/UAssetDiscoveryService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace RevvyBlueprintTool
{
	static FString SerializeJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	static FString MakeError(const FString& Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetBoolField(TEXT("success"), false);
		Error->SetStringField(TEXT("error_code"), Code);
		Error->SetStringField(TEXT("error_message"), Message);
		return SerializeJson(Error);
	}

	static TSharedRef<FJsonObject> VariableToJson(const FBlueprintVariableInfo& Variable)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Variable.VariableName);
		Json->SetStringField(TEXT("type"), Variable.VariableType);
		Json->SetStringField(TEXT("category"), Variable.Category);
		Json->SetStringField(TEXT("default_value"), Variable.DefaultValue);
		// Reported separately rather than collapsed into one "editable" flag:
		// a public variable that is not exposed on the details panel is a real
		// and commonly confusing distinction, and merging them hides it.
		Json->SetBoolField(TEXT("is_public"), Variable.bIsPublic);
		Json->SetBoolField(TEXT("is_exposed"), Variable.bIsExposed);
		return Json;
	}

	static TSharedRef<FJsonObject> FunctionToJson(const FRevvyBlueprintFunctionInfo& Function)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Function.FunctionName);
		Json->SetStringField(TEXT("return_type"), Function.ReturnType);
		TArray<TSharedPtr<FJsonValue>> Parameters;
		for (const FString& Parameter : Function.Parameters)
		{
			Parameters.Add(MakeShared<FJsonValueString>(Parameter));
		}
		Json->SetArrayField(TEXT("parameters"), Parameters);
		Json->SetBoolField(TEXT("is_override"), Function.bIsOverride);
		Json->SetBoolField(TEXT("is_pure"), Function.bIsPure);
		return Json;
	}

	static TSharedRef<FJsonObject> ComponentToJson(const FBlueprintComponentInfo& Component)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Component.ComponentName);
		Json->SetStringField(TEXT("type"), Component.ComponentClass);
		Json->SetStringField(TEXT("parent"), Component.AttachParent);
		Json->SetBoolField(TEXT("is_root"), Component.bIsRootComponent);
		Json->SetBoolField(TEXT("is_scene_component"), Component.bIsSceneComponent);
		// Inherited components are the greyed-out ones from a C++ parent. Worth
		// marking: a reader who cannot see the distinction will wonder why some
		// of them cannot be changed.
		Json->SetBoolField(TEXT("is_inherited"), Component.bIsInherited);
		return Json;
	}
}

REGISTER_REVVY_TOOL_ANNOTATED(read_blueprint,
	"Read the structure of a Blueprint asset: parent class, components (with attachment parents), "
	"variables, functions and event dispatchers. Read-only - never modifies, compiles or saves the "
	"asset. Takes an asset path such as /Game/Player/BP_Player.",
	"Blueprints",
	TOOL_ANNOTATIONS_READ_ONLY("Read Blueprint Structure"),
	TOOL_PARAMS(
		TOOL_PARAM("blueprint_path", "Asset path of the Blueprint, e.g. /Game/Player/BP_Player", "string", true)
	),
	{
		// No 'action' parameter, so MCPServer leaves Params flat and there is
		// nothing to re-expand out of ParamsJson.
		const FString* PathParam = Params.Find(TEXT("blueprint_path"));
		const FString BlueprintPath = PathParam ? *PathParam : FString();
		if (BlueprintPath.IsEmpty())
		{
			return RevvyBlueprintTool::MakeError(
				TEXT("MISSING_PARAMETER"),
				TEXT("blueprint_path is required."));
		}

		FBlueprintDetailedInfo Info;
		if (!UBlueprintService::GetBlueprintInfo(BlueprintPath, Info))
		{
			// Named rather than generic: the overwhelmingly common cause is a
			// path that is right on disk but wrong as an asset path, and
			// "failed" alone does not point at that.
			return RevvyBlueprintTool::MakeError(
				TEXT("BLUEPRINT_NOT_FOUND"),
				FString::Printf(
					TEXT("No Blueprint at '%s'. Asset paths look like /Game/Folder/BP_Name."),
					*BlueprintPath));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("name"), Info.BlueprintName);
		Result->SetStringField(TEXT("path"), Info.BlueprintPath);
		Result->SetStringField(TEXT("parent_class"), Info.ParentClass);
		Result->SetBoolField(TEXT("is_widget"), Info.bIsWidgetBlueprint);

		TArray<TSharedPtr<FJsonValue>> Variables;
		for (const FBlueprintVariableInfo& Variable : Info.Variables)
		{
			Variables.Add(MakeShared<FJsonValueObject>(RevvyBlueprintTool::VariableToJson(Variable)));
		}
		Result->SetArrayField(TEXT("variables"), Variables);

		TArray<TSharedPtr<FJsonValue>> Functions;
		for (const FRevvyBlueprintFunctionInfo& Function : Info.Functions)
		{
			Functions.Add(MakeShared<FJsonValueObject>(RevvyBlueprintTool::FunctionToJson(Function)));
		}
		Result->SetArrayField(TEXT("functions"), Functions);

		// GetComponentHierarchy rather than Info.Components: the hierarchy call
		// is the one that fills AttachParent, and attachment is the part of a
		// component list that actually carries meaning. Falls back to the flat
		// list so a hierarchy failure costs parents, not components.
		TArray<FBlueprintComponentInfo> Components =
			UBlueprintService::GetComponentHierarchy(BlueprintPath);
		if (Components.Num() == 0)
		{
			Components = Info.Components;
		}
		TArray<TSharedPtr<FJsonValue>> ComponentsJson;
		for (const FBlueprintComponentInfo& Component : Components)
		{
			ComponentsJson.Add(MakeShared<FJsonValueObject>(RevvyBlueprintTool::ComponentToJson(Component)));
		}
		Result->SetArrayField(TEXT("components"), ComponentsJson);

		// There is no dedicated dispatcher reader. A dispatcher shows up as a
		// graph whose kind is DelegateSignature, which is the only exposed way
		// to enumerate them.
		TArray<TSharedPtr<FJsonValue>> Dispatchers;
		for (const FBlueprintGraphInfo& Graph : UBlueprintService::ListGraphs(BlueprintPath))
		{
			if (Graph.GraphKind == TEXT("DelegateSignature"))
			{
				Dispatchers.Add(MakeShared<FJsonValueString>(Graph.GraphName));
			}
		}
		Result->SetArrayField(TEXT("event_dispatchers"), Dispatchers);

		return RevvyBlueprintTool::SerializeJson(Result);
	}
);

// The data behind Rider/UnrealLink's "derived Blueprint classes" annotation:
// which Blueprint assets extend a given native C++ class. Registry-only, so
// unloaded Blueprints are found and nothing loads to answer. Read-only
// annotated so a safe session (and the /mcp/safe endpoint) can ask.
REGISTER_REVVY_TOOL_ANNOTATED(get_blueprint_derived_classes,
	"List Blueprint assets that derive from a native C++ class (directly or through Blueprint "
	"parents). Answers from the Asset Registry without loading assets. Read-only. Accepts the "
	"class name with or without its U/A prefix, e.g. 'UDSCurrencyDisplayWidget' or "
	"'DSCurrencyDisplayWidget'.",
	"Blueprints",
	TOOL_ANNOTATIONS_READ_ONLY("Find Derived Blueprints"),
	TOOL_PARAMS(
		TOOL_PARAM("native_class_name", "Native C++ class name, prefixed or bare", "string", true)
	),
	{
		const FString* NameParam = Params.Find(TEXT("native_class_name"));
		const FString NativeClassName = NameParam ? NameParam->TrimStartAndEnd() : FString();
		if (NativeClassName.IsEmpty())
		{
			return RevvyBlueprintTool::MakeError(
				TEXT("MISSING_PARAMETER"),
				TEXT("native_class_name is required."));
		}

		const TArray<FAssetData> Derived =
			UAssetDiscoveryService::GetBlueprintsDerivedFromNativeClass(NativeClassName);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("native_class"), NativeClassName);
		Result->SetNumberField(TEXT("count"), Derived.Num());

		TArray<TSharedPtr<FJsonValue>> Blueprints;
		for (const FAssetData& Asset : Derived)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
			Json->SetStringField(TEXT("package_name"), Asset.PackageName.ToString());
			Json->SetStringField(TEXT("object_path"), Asset.GetObjectPathString());
			Blueprints.Add(MakeShared<FJsonValueObject>(Json));
		}
		Result->SetArrayField(TEXT("blueprints"), Blueprints);

		return RevvyBlueprintTool::SerializeJson(Result);
	}
);
