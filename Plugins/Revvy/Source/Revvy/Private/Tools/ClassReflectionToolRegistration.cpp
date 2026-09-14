// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// lookup_class - native UClass reflection read straight from C++, no Python hop.
//
// discover_python_class answers "what does the *Python* surface of this class
// look like" - filtered through PyUnreal name mangling and limited to what the
// bindings expose. This tool answers the C++ question: the real inheritance
// chain, UPROPERTY specifiers, UFUNCTION signatures and editor tooltips, the
// way a Rider/UnrealLink user would read them. Input accepts prefixed
// ("UStaticMeshComponent"), bare ("StaticMeshComponent") and /Script path
// forms; bare names are resolved with the same prefix verification as
// get_blueprint_derived_classes so an arbitrary same-named class is never
// silently picked.
//

#include "Core/ToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace RevvyClassReflectionTool
{

// ---------------------------------------------------------------------------
// Parameter extraction (same shape as build_project / read_logs)
// ---------------------------------------------------------------------------

static FString ExtractParam(const TMap<FString, FString>& Params, const FString& FieldName)
{
	if (const FString* Direct = Params.Find(FieldName))
	{
		return *Direct;
	}

	FString Capitalized = FieldName;
	if (Capitalized.Len() > 0)
	{
		Capitalized[0] = FChar::ToUpper(Capitalized[0]);
	}
	if (const FString* Cap = Params.Find(Capitalized))
	{
		return *Cap;
	}

	const FString* ParamsJsonStr = Params.Find(TEXT("ParamsJson"));
	if (!ParamsJsonStr || ParamsJsonStr->IsEmpty())
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

static bool ExtractBoolParam(const TMap<FString, FString>& Params, const FString& FieldName, bool DefaultValue)
{
	const FString Value = ExtractParam(Params, FieldName);
	if (Value.IsEmpty())
	{
		return DefaultValue;
	}
	if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("0")))
	{
		return false;
	}
	return Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1"));
}

static int32 ExtractIntParam(const TMap<FString, FString>& Params, const FString& FieldName, int32 DefaultValue)
{
	const FString Value = ExtractParam(Params, FieldName);
	if (Value.IsEmpty())
	{
		return DefaultValue;
	}
	return FCString::Atoi(*Value);
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
{
	FString JsonString;
	if (!Object.IsValid())
	{
		return TEXT("{}");
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	return JsonString;
}

static FString BuildErrorResponse(const FString& ErrorCode, const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetBoolField(TEXT("success"), false);
	ErrorObj->SetStringField(TEXT("error_code"), ErrorCode);
	ErrorObj->SetStringField(TEXT("error"), ErrorMessage);
	return SerializeJson(ErrorObj);
}

// ---------------------------------------------------------------------------
// Class resolution
// ---------------------------------------------------------------------------

/** Prefixed C++ name: GetPrefixCPP() (A/U/F...) + GetName(). */
static FString PrefixedName(const UClass* Class)
{
	return FString(Class->GetPrefixCPP()) + Class->GetName();
}

/**
 * Resolve "/Script/Engine.Actor", "AActor" or "Actor" to a UClass.
 * Bare and prefixed names are verified against GetPrefixCPP()+GetName so a
 * same-named class from an arbitrary package is never silently picked - the
 * exact iteration used by get_blueprint_derived_classes (commit fd6915d).
 */
static UClass* ResolveClass(const FString& Input, FString& OutAmbiguityNote)
{
	if (Input.StartsWith(TEXT("/")))
	{
		return FindObject<UClass>(nullptr, *Input);
	}

	TArray<UClass*> Matches;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (Candidate->GetName() == Input || PrefixedName(Candidate) == Input)
		{
			Matches.Add(Candidate);
		}
	}

	if (Matches.Num() == 0)
	{
		return nullptr;
	}
	if (Matches.Num() > 1)
	{
		// Prefer a native class over Blueprint-generated ones with the same name.
		TArray<UClass*> Native;
		for (UClass* Match : Matches)
		{
			if (Match->IsNative())
			{
				Native.Add(Match);
			}
		}
		if (Native.Num() == 1)
		{
			return Native[0];
		}
		TArray<FString> Paths;
		for (UClass* Match : Matches)
		{
			Paths.Add(Match->GetPathName());
		}
		OutAmbiguityNote = FString::Printf(
			TEXT("Name '%s' is ambiguous between: %s. Pass a /Script path to pick one."),
			*Input, *FString::Join(Paths, TEXT(", ")));
		return nullptr;
	}
	return Matches[0];
}

// ---------------------------------------------------------------------------
// Reflection -> JSON
// ---------------------------------------------------------------------------

static FString FirstLine(const FString& Text)
{
	FString Line = Text;
	int32 NewlineIndex;
	if (Line.FindChar(TEXT('\n'), NewlineIndex))
	{
		Line.LeftInline(NewlineIndex);
	}
	return Line.TrimStartAndEnd();
}

static TArray<TSharedPtr<FJsonValue>> ToStringArray(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FString& Value : Values)
	{
		Array.Add(MakeShared<FJsonValueString>(Value));
	}
	return Array;
}

static TArray<FString> PropertySpecifiers(const FProperty* Property)
{
	TArray<FString> Specifiers;
	const bool bEdit = Property->HasAnyPropertyFlags(CPF_Edit);
	const bool bEditConst = Property->HasAnyPropertyFlags(CPF_EditConst);
	if (bEdit && !bEditConst) { Specifiers.Add(TEXT("EditAnywhere-ish")); }
	if (bEdit && bEditConst)  { Specifiers.Add(TEXT("VisibleAnywhere-ish")); }
	if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
	{
		Specifiers.Add(Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly)
			? TEXT("BlueprintReadOnly") : TEXT("BlueprintReadWrite"));
	}
	if (Property->HasAnyPropertyFlags(CPF_Config))    { Specifiers.Add(TEXT("Config")); }
	if (Property->HasAnyPropertyFlags(CPF_Transient)) { Specifiers.Add(TEXT("Transient")); }
	if (Property->HasAnyPropertyFlags(CPF_Net))       { Specifiers.Add(TEXT("Replicated")); }
	if (Property->HasAnyPropertyFlags(CPF_SaveGame))  { Specifiers.Add(TEXT("SaveGame")); }
	return Specifiers;
}

static TSharedPtr<FJsonObject> PropertyToJson(const FProperty* Property, bool bWithTooltips)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Property->GetName());
	Entry->SetStringField(TEXT("type"), Property->GetCPPType());
	const FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		Entry->SetStringField(TEXT("category"), Category);
	}
	const TArray<FString> Specifiers = PropertySpecifiers(Property);
	if (Specifiers.Num() > 0)
	{
		Entry->SetArrayField(TEXT("specifiers"), ToStringArray(Specifiers));
	}
	if (bWithTooltips)
	{
		const FString Tooltip = FirstLine(Property->GetMetaData(TEXT("ToolTip")));
		if (!Tooltip.IsEmpty())
		{
			Entry->SetStringField(TEXT("tooltip"), Tooltip);
		}
	}
	Entry->SetStringField(TEXT("owner"), Property->GetOwnerClass() ? Property->GetOwnerClass()->GetName() : FString());
	return Entry;
}

static FString FunctionSignature(const UFunction* Function)
{
	FString ReturnType = TEXT("void");
	TArray<FString> ParamList;
	for (TFieldIterator<FProperty> It(const_cast<UFunction*>(Function)); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		const FProperty* Param = *It;
		if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ReturnType = Param->GetCPPType();
			continue;
		}
		const bool bOut = Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ConstParm);
		ParamList.Add(FString::Printf(TEXT("%s%s %s"),
			bOut ? TEXT("out ") : TEXT(""), *Param->GetCPPType(), *Param->GetName()));
	}
	return FString::Printf(TEXT("%s %s(%s)"), *ReturnType, *Function->GetName(), *FString::Join(ParamList, TEXT(", ")));
}

static TSharedPtr<FJsonObject> FunctionToJson(const UFunction* Function, bool bWithTooltips)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Function->GetName());
	Entry->SetStringField(TEXT("signature"), FunctionSignature(Function));

	TArray<FString> Specifiers;
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
	{
		Specifiers.Add(Function->HasAnyFunctionFlags(FUNC_BlueprintPure)
			? TEXT("BlueprintPure") : TEXT("BlueprintCallable"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	{
		Specifiers.Add(Function->HasAnyFunctionFlags(FUNC_Native)
			? TEXT("BlueprintNativeEvent") : TEXT("BlueprintImplementableEvent"));
	}
	if (Function->HasAnyFunctionFlags(FUNC_Static)) { Specifiers.Add(TEXT("Static")); }
	if (Function->HasAnyFunctionFlags(FUNC_Const))  { Specifiers.Add(TEXT("Const")); }
	if (Function->HasAnyFunctionFlags(FUNC_Net))
	{
		Specifiers.Add(Function->HasAnyFunctionFlags(FUNC_NetServer)
			? TEXT("Server")
			: (Function->HasAnyFunctionFlags(FUNC_NetClient) ? TEXT("Client") : TEXT("NetMulticast")));
	}
	if (Specifiers.Num() > 0)
	{
		Entry->SetArrayField(TEXT("specifiers"), ToStringArray(Specifiers));
	}

	const FString Category = Function->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty())
	{
		Entry->SetStringField(TEXT("category"), Category);
	}
	if (bWithTooltips)
	{
		const FString Tooltip = FirstLine(Function->GetMetaData(TEXT("ToolTip")));
		if (!Tooltip.IsEmpty())
		{
			Entry->SetStringField(TEXT("tooltip"), Tooltip);
		}
	}
	Entry->SetStringField(TEXT("owner"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetName() : FString());
	return Entry;
}

// ---------------------------------------------------------------------------
// Tool body
// ---------------------------------------------------------------------------

static FString ExecuteLookupClass(const TMap<FString, FString>& Params)
{
	const FString ClassName = ExtractParam(Params, TEXT("class_name")).TrimStartAndEnd();
	if (ClassName.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_CLASS"),
			TEXT("The 'class_name' parameter is required - e.g. 'AActor', 'StaticMeshComponent' or '/Script/Engine.Actor'."));
	}

	const bool bIncludeInherited = ExtractBoolParam(Params, TEXT("include_inherited"), false);
	const bool bWithTooltips = ExtractBoolParam(Params, TEXT("include_tooltips"), true);
	const FString MemberFilter = ExtractParam(Params, TEXT("member_filter")).TrimStartAndEnd();
	const int32 MaxMembers = FMath::Clamp(ExtractIntParam(Params, TEXT("max_members"), 100), 1, 1000);

	FString AmbiguityNote;
	UClass* Class = ResolveClass(ClassName, AmbiguityNote);
	if (!Class)
	{
		if (!AmbiguityNote.IsEmpty())
		{
			return BuildErrorResponse(TEXT("AMBIGUOUS_CLASS"), AmbiguityNote);
		}
		return BuildErrorResponse(TEXT("CLASS_NOT_FOUND"),
			FString::Printf(TEXT("No loaded UClass matches '%s'. Blueprint classes must be loaded to reflect; "
				"for those pass the generated-class path (e.g. /Game/BP_Thing.BP_Thing_C)."), *ClassName));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("class"), Class->GetName());
	Result->SetStringField(TEXT("cpp_name"), PrefixedName(Class));
	Result->SetStringField(TEXT("path"), Class->GetPathName());
	Result->SetBoolField(TEXT("native"), Class->IsNative());

	const FString Module = FirstLine(Class->GetMetaData(TEXT("ModuleRelativePath")));
	if (!Module.IsEmpty())
	{
		Result->SetStringField(TEXT("header"), Module);
	}

	TArray<FString> ClassSpecifiers;
	if (Class->HasAnyClassFlags(CLASS_Abstract))   { ClassSpecifiers.Add(TEXT("Abstract")); }
	if (Class->HasAnyClassFlags(CLASS_Deprecated)) { ClassSpecifiers.Add(TEXT("Deprecated")); }
	if (Class->GetBoolMetaData(TEXT("IsBlueprintBase")) || Class->HasMetaData(TEXT("BlueprintSpawnableComponent")))
	{
		ClassSpecifiers.Add(TEXT("Blueprintable"));
	}
	if (ClassSpecifiers.Num() > 0)
	{
		Result->SetArrayField(TEXT("class_specifiers"), ToStringArray(ClassSpecifiers));
	}
	if (bWithTooltips)
	{
		const FString Tooltip = FirstLine(Class->GetMetaData(TEXT("ToolTip")));
		if (!Tooltip.IsEmpty())
		{
			Result->SetStringField(TEXT("tooltip"), Tooltip);
		}
	}

	// Inheritance chain up to UObject.
	TArray<TSharedPtr<FJsonValue>> Chain;
	for (const UClass* Super = Class->GetSuperClass(); Super; Super = Super->GetSuperClass())
	{
		Chain.Add(MakeShared<FJsonValueString>(PrefixedName(Super)));
	}
	Result->SetArrayField(TEXT("inheritance_chain"), Chain);

	const EFieldIteratorFlags::SuperClassFlags SuperFlags =
		bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;

	// Properties.
	{
		int32 Matched = 0;
		TArray<TSharedPtr<FJsonValue>> Properties;
		for (TFieldIterator<FProperty> It(Class, SuperFlags); It; ++It)
		{
			if (!MemberFilter.IsEmpty() && !It->GetName().Contains(MemberFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			++Matched;
			if (Properties.Num() >= MaxMembers)
			{
				continue;
			}
			Properties.Add(MakeShared<FJsonValueObject>(PropertyToJson(*It, bWithTooltips)));
		}
		Result->SetArrayField(TEXT("properties"), Properties);
		Result->SetNumberField(TEXT("properties_total"), Matched);
	}

	// Functions.
	{
		int32 Matched = 0;
		TArray<TSharedPtr<FJsonValue>> Functions;
		for (TFieldIterator<UFunction> It(Class, SuperFlags); It; ++It)
		{
			if (!MemberFilter.IsEmpty() && !It->GetName().Contains(MemberFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			++Matched;
			if (Functions.Num() >= MaxMembers)
			{
				continue;
			}
			Functions.Add(MakeShared<FJsonValueObject>(FunctionToJson(*It, bWithTooltips)));
		}
		Result->SetArrayField(TEXT("functions"), Functions);
		Result->SetNumberField(TEXT("functions_total"), Matched);
	}

	return SerializeJson(Result);
}

} // namespace RevvyClassReflectionTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(lookup_class,
	"Reflect a UClass straight from native C++: inheritance chain, UPROPERTY list with types/specifiers/"
	"tooltips, UFUNCTION signatures with Blueprint exposure - the answer to 'what can I actually call or "
	"set on this class'. Complements discover_python_class (which shows the Python-binding surface; this "
	"shows the C++ truth, including members the bindings hide).\n"
	"\nINPUT FORMS: 'AActor' (prefixed), 'Actor' (bare - prefix-verified, ambiguity is an error, never a "
	"guess), '/Script/Engine.Actor' (exact), '/Game/BP_Thing.BP_Thing_C' (loaded Blueprint class).\n"
	"\nDEFAULTS: own members only (include_inherited=false; the inheritance_chain tells you where to look "
	"next), tooltips on, 100 members per list.\n"
	"\nTYPICAL USES:\n"
	"  - before writing C++/Python against an unfamiliar class\n"
	"  - resolving 'does this property exist and is it BlueprintReadWrite?'\n"
	"  - member_filter='Health' to find everything health-related on a class",
	"Discovery",
	TOOL_ANNOTATIONS_READ_ONLY("Lookup Class (C++ reflection)"),
	TOOL_PARAMS(
		TOOL_PARAM("class_name", "Class to reflect: 'AActor', 'Actor', '/Script/Engine.Actor' or a loaded Blueprint generated class path", "string", true),
		TOOL_PARAM("include_inherited", "Include members from super classes (default false)", "boolean", false),
		TOOL_PARAM("member_filter", "Case-insensitive substring filter applied to property and function names", "string", false),
		TOOL_PARAM("include_tooltips", "Include first-line editor tooltips (default true)", "boolean", false),
		TOOL_PARAM("max_members", "Cap per list (default 100, max 1000); totals are always reported", "number", false)
	),
	{
		return RevvyClassReflectionTool::ExecuteLookupClass(Params);
	}
);
