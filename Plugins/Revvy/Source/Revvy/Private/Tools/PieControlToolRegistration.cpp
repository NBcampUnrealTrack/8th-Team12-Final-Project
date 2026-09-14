// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// pie_control - Play In Editor session control exposed as an MCP tool.
//
// Start/stop are DEFERRED by the engine: RequestPlaySession / RequestEndPlayMap
// queue the transition and the editor performs it on a following frame. Both
// actions therefore return {requested: true} and callers confirm the new state
// with action='status' a moment later.
//

#include "Core/ToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RevvyPieTool
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

	// MCP server capitalizes 'action' to 'Action' when it detects a multi-action tool.
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
// State helpers
// ---------------------------------------------------------------------------

static bool IsSessionActive()
{
	return GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);
}

static TSharedPtr<FJsonObject> ComposeStatusJson()
{
	TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
	Status->SetBoolField(TEXT("success"), true);

	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
	const bool bSimulating = GEditor && GEditor->bIsSimulatingInEditor;

	if (!PlayWorld && !bSimulating)
	{
		Status->SetStringField(TEXT("state"), TEXT("stopped"));
		Status->SetStringField(TEXT("note"),
			TEXT("No play session. Start/stop transitions are deferred a frame - if you just issued one, "
				"poll again."));
		return Status;
	}

	Status->SetStringField(TEXT("state"), bSimulating ? TEXT("simulating") : TEXT("playing"));
	if (PlayWorld)
	{
		Status->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(PlayWorld->GetMapName()));
		Status->SetNumberField(TEXT("play_time_seconds"),
			FMath::RoundToDouble(PlayWorld->GetTimeSeconds() * 10.0) / 10.0);
		Status->SetNumberField(TEXT("num_player_controllers"), PlayWorld->GetNumPlayerControllers());
		Status->SetBoolField(TEXT("is_paused"), PlayWorld->IsPaused());
	}
	return Status;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static FString HandleStart(const TMap<FString, FString>& Params)
{
	if (!GEditor)
	{
		return BuildErrorResponse(TEXT("NO_EDITOR"), TEXT("GEditor is unavailable."));
	}
	if (IsSessionActive())
	{
		return BuildErrorResponse(TEXT("PIE_ALREADY_RUNNING"),
			TEXT("A play session is already active. Use action='status' to inspect it or action='stop' first."));
	}

	const FString Map = ExtractParam(Params, TEXT("map")).TrimStartAndEnd();
	const bool bSimulate = ExtractBoolParam(Params, TEXT("simulate"), false);

	FRequestPlaySessionParams SessionParams;
	SessionParams.WorldType = bSimulate
		? EPlaySessionWorldType::SimulateInEditor
		: EPlaySessionWorldType::PlayInEditor;
	if (!Map.IsEmpty())
	{
		if (!FPackageName::IsValidLongPackageName(Map) || !FPackageName::DoesPackageExist(Map))
		{
			return BuildErrorResponse(TEXT("MAP_NOT_FOUND"),
				FString::Printf(TEXT("'%s' is not an existing map package. Pass a long package name like "
					"/Game/Maps/MyLevel, or omit 'map' to play the currently loaded level."), *Map));
		}
		SessionParams.GlobalMapOverride = Map;
	}

	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("requested"), true);
	Result->SetStringField(TEXT("mode"), bSimulate ? TEXT("simulate") : TEXT("play"));
	if (!Map.IsEmpty())
	{
		Result->SetStringField(TEXT("map"), Map);
	}
	Result->SetStringField(TEXT("note"),
		TEXT("The session starts on a following frame. Poll action='status' to confirm state='playing'."));
	return SerializeJson(Result);
}

static FString HandleStop()
{
	if (!GEditor)
	{
		return BuildErrorResponse(TEXT("NO_EDITOR"), TEXT("GEditor is unavailable."));
	}
	if (!IsSessionActive())
	{
		return BuildErrorResponse(TEXT("PIE_NOT_RUNNING"), TEXT("No play session is active."));
	}

	GEditor->RequestEndPlayMap();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("requested"), true);
	Result->SetStringField(TEXT("note"),
		TEXT("The session ends on a following frame. Poll action='status' to confirm state='stopped'."));
	return SerializeJson(Result);
}

static FString GetPieToolHelp()
{
	TSharedPtr<FJsonObject> Help = MakeShared<FJsonObject>();
	Help->SetBoolField(TEXT("success"), true);
	Help->SetStringField(TEXT("tool"), TEXT("pie_control"));
	Help->SetStringField(TEXT("description"),
		TEXT("Start, stop and inspect the editor's Play In Editor session. Transitions are deferred a "
			"frame by the engine, so start/stop return {requested: true} and 'status' confirms."));

	TArray<TSharedPtr<FJsonValue>> Actions;
	auto AddAction = [&Actions](const FString& Name, const FString& Desc, const FString& ParamsDesc)
	{
		TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
		ActionObj->SetStringField(TEXT("action"), Name);
		ActionObj->SetStringField(TEXT("description"), Desc);
		ActionObj->SetStringField(TEXT("parameters"), ParamsDesc);
		Actions.Add(MakeShared<FJsonValueObject>(ActionObj));
	};
	AddAction(TEXT("start"),
		TEXT("Request a play session in the active viewport."),
		TEXT("map (optional long package name, e.g. /Game/Maps/MyLevel; default: current level), "
			"simulate (default false: possess a player; true: simulate without possessing)"));
	AddAction(TEXT("stop"), TEXT("Request the active session to end."), TEXT("None"));
	AddAction(TEXT("status"),
		TEXT("Current state: stopped | playing | simulating, plus map, play_time_seconds, "
			"num_player_controllers, is_paused."),
		TEXT("None"));
	AddAction(TEXT("help"), TEXT("Show this message."), TEXT("None"));
	Help->SetArrayField(TEXT("actions"), Actions);

	TArray<TSharedPtr<FJsonValue>> Notes;
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("While a session is active, mutating Revvy tools skip their editor transaction wrapper - "
			"prefer making edits after action='stop'.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Unsaved level changes are played as-is; PIE does not save anything.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Combine with capture_from_pose / describe_scene to see what the running game looks like.")));
	Help->SetArrayField(TEXT("notes"), Notes);

	return SerializeJson(Help);
}

/** Single entry point - keeps the REGISTER macro body free of top-level commas. */
static FString ExecutePieTool(const TMap<FString, FString>& Params)
{
	const FString Action = ExtractParam(Params, TEXT("action")).TrimStartAndEnd().ToLower();

	if (Action.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_ACTION"),
			TEXT("The 'action' parameter is required. Use action=help for documentation."));
	}
	if (Action == TEXT("help"))
	{
		return GetPieToolHelp();
	}
	if (Action == TEXT("start") || Action == TEXT("play") || Action == TEXT("simulate"))
	{
		// 'simulate' as an action is sugar for start with simulate=true.
		if (Action == TEXT("simulate"))
		{
			TMap<FString, FString> Adjusted = Params;
			Adjusted.Add(TEXT("simulate"), TEXT("true"));
			return HandleStart(Adjusted);
		}
		return HandleStart(Params);
	}
	if (Action == TEXT("stop") || Action == TEXT("end"))
	{
		return HandleStop();
	}
	if (Action == TEXT("status"))
	{
		return SerializeJson(ComposeStatusJson());
	}

	return BuildErrorResponse(TEXT("UNKNOWN_ACTION"),
		FString::Printf(TEXT("Unknown action: %s. Valid: start, stop, status, help."), *Action));
}

/**
 * MCP annotations.
 *
 * Starting/stopping PIE changes editor session state but persists nothing - no asset,
 * package or source file is touched, so destructiveHint stays false. Repeating 'start'
 * while running errors rather than stacking sessions, hence not idempotent.
 * ReadOnlyActions keeps 'status' reachable from a read-only MCP session.
 */
static FToolAnnotations MakePieToolAnnotations()
{
	FToolAnnotations Annotations;
	Annotations.Title = TEXT("Play In Editor Control");
	Annotations.bReadOnlyHint = false;
	Annotations.bDestructiveHint = false;
	Annotations.bIdempotentHint = false;
	Annotations.bOpenWorldHint = false;
	Annotations.bAnnotated = true;
	Annotations.ReadOnlyActions = { TEXT("status"), TEXT("help") };
	return Annotations;
}

} // namespace RevvyPieTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(pie_control,
	"Start, stop and inspect Play In Editor sessions - run the game inside the editor to verify gameplay "
	"changes actually behave, then stop and keep editing.\n"
	"\nTransitions are DEFERRED: the engine performs the start/stop on a following frame, so both return "
	"{requested: true}; confirm with action='status'.\n"
	"\nACTIONS:\n"
	"  start   : play the current level (or 'map' override) in the active viewport. simulate=true to "
	"simulate without possessing a player.\n"
	"  stop    : end the active session.\n"
	"  status  : stopped | playing | simulating, plus map, play_time_seconds, num_player_controllers, is_paused.\n"
	"  help    : full documentation.\n"
	"\nTYPICAL LOOP:\n"
	"  1. edit gameplay code / Blueprints\n"
	"  2. action='start'\n"
	"  3. action='status'  (confirm playing; optionally capture_from_pose to look at it)\n"
	"  4. action='stop'  -> keep editing\n"
	"\nNothing is saved by PIE; unsaved level changes play as-is.",
	"Editor",
	RevvyPieTool::MakePieToolAnnotations(),
	TOOL_PARAMS(
		TOOL_PARAM("action", "Operation: start, stop, status, help", "string", true),
		TOOL_PARAM("map", "[start] Long package name of the map to play (e.g. /Game/Maps/MyLevel). Default: the currently loaded level", "string", false),
		TOOL_PARAM("simulate", "[start] true = Simulate In Editor (no player possession). Default false = Play In Editor", "boolean", false)
	),
	{
		return RevvyPieTool::ExecutePieTool(Params);
	}
);
