// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// manage_checkpoints - list and restore the automatic pre-save snapshots that
// FRevvyCheckpointService captures whenever a Revvy tool saves over an existing
// asset file. The capture side is passive (a package-save delegate); this tool
// is the read/restore surface.
//

#include "Core/ToolRegistry.h"
#include "Core/CheckpointService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RevvyCheckpointTool
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
// Actions
// ---------------------------------------------------------------------------

static FString HandleList(const TMap<FString, FString>& Params)
{
	const FString Filter = ExtractParam(Params, TEXT("package_filter")).TrimStartAndEnd();
	const int32 Limit = FMath::Clamp(ExtractIntParam(Params, TEXT("limit"), 25), 1, 500);

	const TArray<TSharedPtr<FJsonObject>> Entries =
		FRevvyCheckpointService::Get().ListCheckpoints(Filter, Limit);

	TArray<TSharedPtr<FJsonValue>> Array;
	for (const TSharedPtr<FJsonObject>& Entry : Entries)
	{
		Array.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("returned"), Array.Num());
	Result->SetArrayField(TEXT("checkpoints"), Array);
	if (Array.Num() == 0)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("No checkpoints recorded yet. They appear automatically the first time a Revvy tool "
				"saves over an existing asset file."));
	}
	return SerializeJson(Result);
}

static FString HandleRestore(const TMap<FString, FString>& Params)
{
	const FString Id = ExtractParam(Params, TEXT("id")).TrimStartAndEnd();
	if (Id.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_ID"),
			TEXT("The 'id' parameter is required. Find ids with action='list'."));
	}
	const bool bForce = ExtractBoolParam(Params, TEXT("force"), false);

	FString Error;
	FString RestoredPackage;
	if (!FRevvyCheckpointService::Get().RestoreCheckpoint(Id, bForce, Error, RestoredPackage))
	{
		return BuildErrorResponse(TEXT("RESTORE_FAILED"), Error);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("restored_package"), RestoredPackage);
	Result->SetStringField(TEXT("note"),
		TEXT("The on-disk file was replaced with the checkpoint and any loaded copy was reloaded. "
			"The overwritten (newer) version was itself checkpointed only if a Revvy tool saved it."));
	return SerializeJson(Result);
}

static FString HandlePrune(const TMap<FString, FString>& Params)
{
	const double MaxAgeDays = FMath::Clamp(
		(double)ExtractIntParam(Params, TEXT("max_age_days"), 14), 0.0, 365.0);
	const double MaxTotalMb = FMath::Clamp(
		(double)ExtractIntParam(Params, TEXT("max_total_mb"), 1024), 16.0, 65536.0);

	const int32 Removed = FRevvyCheckpointService::Get().PruneCheckpoints(MaxAgeDays, MaxTotalMb);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("removed_entries"), Removed);
	return SerializeJson(Result);
}

static FString GetCheckpointToolHelp()
{
	TSharedPtr<FJsonObject> Help = MakeShared<FJsonObject>();
	Help->SetBoolField(TEXT("success"), true);
	Help->SetStringField(TEXT("tool"), TEXT("manage_checkpoints"));
	Help->SetStringField(TEXT("description"),
		TEXT("Browse and restore the automatic pre-save snapshots Revvy keeps of asset files it "
			"overwrites. Capture is passive: whenever a Revvy tool execution saves over an existing "
			"file under Content/, the previous on-disk version is copied to Saved/Revvy/Checkpoints "
			"first. Editor transactions cover in-session undo; these checkpoints cover 'the save "
			"already happened'."));

	TArray<TSharedPtr<FJsonValue>> Actions;
	auto AddAction = [&Actions](const FString& Name, const FString& Desc, const FString& ParamsDesc)
	{
		TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
		ActionObj->SetStringField(TEXT("action"), Name);
		ActionObj->SetStringField(TEXT("description"), Desc);
		ActionObj->SetStringField(TEXT("parameters"), ParamsDesc);
		Actions.Add(MakeShared<FJsonValueObject>(ActionObj));
	};
	AddAction(TEXT("list"),
		TEXT("Checkpoints newest first: {id, timestamp, package, tool, original_file, size_bytes}."),
		TEXT("package_filter (substring, optional), limit (default 25)"));
	AddAction(TEXT("restore"),
		TEXT("Copy a checkpoint back over its original file and reload the package if loaded. Refuses "
			"when the package has unsaved in-memory edits unless force=true."),
		TEXT("id (REQUIRED), force (default false)"));
	AddAction(TEXT("prune"), TEXT("Delete old checkpoints beyond the given age/size budget."),
		TEXT("max_age_days (default 14), max_total_mb (default 1024)"));
	AddAction(TEXT("status"), TEXT("Capture mode, storage location, entry count and total size."), TEXT("None"));
	AddAction(TEXT("help"), TEXT("Show this message."), TEXT("None"));
	Help->SetArrayField(TEXT("actions"), Actions);

	TArray<TSharedPtr<FJsonValue>> Notes;
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Config lives in Saved/Config/Revvy.ini [Revvy.Checkpoints]: bEnabled, bRevvyToolScopeOnly "
			"(false = checkpoint every editor save), MaxAgeDays, MaxTotalMB.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("The first save of a brand-new asset records nothing - there is no previous version to keep.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Restore is by id, one asset at a time, and is itself the destructive direction - "
			"list first, restore deliberately.")));
	Help->SetArrayField(TEXT("notes"), Notes);

	return SerializeJson(Help);
}

/** Single entry point - keeps the REGISTER macro body free of top-level commas. */
static FString ExecuteCheckpointTool(const TMap<FString, FString>& Params)
{
	const FString Action = ExtractParam(Params, TEXT("action")).TrimStartAndEnd().ToLower();

	if (Action.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_ACTION"),
			TEXT("The 'action' parameter is required. Use action=help for documentation."));
	}
	if (Action == TEXT("help"))
	{
		return GetCheckpointToolHelp();
	}
	if (Action == TEXT("list"))
	{
		return HandleList(Params);
	}
	if (Action == TEXT("restore"))
	{
		return HandleRestore(Params);
	}
	if (Action == TEXT("prune"))
	{
		return HandlePrune(Params);
	}
	if (Action == TEXT("status"))
	{
		return SerializeJson(FRevvyCheckpointService::Get().GetStatusJson());
	}

	return BuildErrorResponse(TEXT("UNKNOWN_ACTION"),
		FString::Printf(TEXT("Unknown action: %s. Valid: list, restore, prune, status, help."), *Action));
}

/**
 * MCP annotations.
 *
 * 'restore' overwrites the current on-disk asset with an older version and
 * 'prune' deletes backups - genuinely destructive, so destructiveHint is true
 * and clients get to gate it. ReadOnlyActions keeps list/status reachable from
 * a read-only MCP session.
 */
static FToolAnnotations MakeCheckpointToolAnnotations()
{
	FToolAnnotations Annotations;
	Annotations.Title = TEXT("Manage Checkpoints (pre-save snapshots)");
	Annotations.bReadOnlyHint = false;
	Annotations.bDestructiveHint = true;
	Annotations.bIdempotentHint = false;
	Annotations.bOpenWorldHint = false;
	Annotations.bAnnotated = true;
	Annotations.ReadOnlyActions = { TEXT("list"), TEXT("status"), TEXT("help") };
	return Annotations;
}

} // namespace RevvyCheckpointTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(manage_checkpoints,
	"Browse and restore the automatic pre-save snapshots Revvy keeps of every asset file a Revvy tool "
	"overwrites - the durable safety net BEHIND editor undo. When a tool saves over an existing file "
	"under Content/, the previous on-disk version is copied to Saved/Revvy/Checkpoints first, "
	"automatically.\n"
	"\nACTIONS:\n"
	"  list     : checkpoints newest first ({id, timestamp, package, tool, size_bytes}).\n"
	"  restore  : copy checkpoint 'id' back over the original and reload the package. Refuses over "
	"unsaved in-memory edits unless force=true.\n"
	"  prune    : delete beyond an age/size budget.\n"
	"  status   : capture mode, storage dir, entry count, total size.\n"
	"  help     : full documentation.\n"
	"\nTYPICAL RECOVERY:\n"
	"  1. action='list', package_filter='BP_Player'\n"
	"  2. action='restore', id='<from list>'\n"
	"\nCapture is automatic and passive; this tool never creates checkpoints itself. The first save of "
	"a brand-new asset records nothing (no previous version exists).",
	"Safety",
	RevvyCheckpointTool::MakeCheckpointToolAnnotations(),
	TOOL_PARAMS(
		TOOL_PARAM("action", "Operation: list, restore, prune, status, help", "string", true),
		TOOL_PARAM("package_filter", "[list] Case-insensitive substring matched against the package path", "string", false),
		TOOL_PARAM("limit", "[list] Maximum entries to return (default 25, max 500)", "number", false),
		TOOL_PARAM("id", "[restore] Checkpoint id from action='list'", "string", false),
		TOOL_PARAM("force", "[restore] Discard unsaved in-memory edits on the target package (default false)", "boolean", false),
		TOOL_PARAM("max_age_days", "[prune] Delete checkpoints older than this (default 14)", "number", false),
		TOOL_PARAM("max_total_mb", "[prune] Keep total storage under this many megabytes (default 1024)", "number", false)
	),
	{
		return RevvyCheckpointTool::ExecuteCheckpointTool(Params);
	}
);
