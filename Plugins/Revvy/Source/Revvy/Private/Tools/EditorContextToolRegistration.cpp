// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// get_editor_context - one snapshot of what the user is looking at right now.
//
// In-editor chat constantly hears deictic requests: "this blueprint", "the
// selected actors", "what I have open". This tool turns those into paths by
// aggregating the live editor state - loaded level, play session, the most
// recently activated asset editor, every open asset editor, the Content
// Browser selection, the viewport actor selection and (when a Blueprint
// editor has focus-worthy state) the selected graph nodes - into one
// read-only JSON snapshot. Call it FIRST whenever a request references
// "this/that/here" instead of a concrete asset path.
//

#include "Core/ToolRegistry.h"

#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PythonAPI/UAssetDiscoveryService.h"
#include "PythonAPI/UBlueprintService.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace RevvyEditorContextTool
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

static TSharedPtr<FJsonObject> AssetToJson(const FAssetData& Asset)
{
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
	Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
	Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
	return Entry;
}

static TArray<TSharedPtr<FJsonValue>> AssetsToJson(const TArray<FAssetData>& Assets, int32 MaxItems, bool& bOutTruncated)
{
	TArray<TSharedPtr<FJsonValue>> Array;
	for (const FAssetData& Asset : Assets)
	{
		if (Array.Num() >= MaxItems)
		{
			bOutTruncated = true;
			break;
		}
		Array.Add(MakeShared<FJsonValueObject>(AssetToJson(Asset)));
	}
	return Array;
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

static FString ComposeContextSnapshot(const TMap<FString, FString>& Params)
{
	const int32 MaxItems = FMath::Clamp(ExtractIntParam(Params, TEXT("max_items"), 25), 1, 200);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);

	if (!GEditor)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error_code"), TEXT("NO_EDITOR"));
		Result->SetStringField(TEXT("error"), TEXT("GEditor is unavailable."));
		return SerializeJson(Result);
	}

	// --- Level ---------------------------------------------------------------
	FString LevelName;
	if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
	{
		TSharedPtr<FJsonObject> Level = MakeShared<FJsonObject>();
		LevelName = EditorWorld->GetMapName();
		Level->SetStringField(TEXT("name"), LevelName);
		if (UPackage* Package = EditorWorld->GetOutermost())
		{
			Level->SetStringField(TEXT("path"), Package->GetName());
			Level->SetBoolField(TEXT("dirty"), Package->IsDirty());
		}
		Result->SetObjectField(TEXT("level"), Level);
	}

	// --- Play session --------------------------------------------------------
	const bool bSimulating = GEditor->bIsSimulatingInEditor;
	const bool bPlaying = GEditor->PlayWorld != nullptr && !bSimulating;
	Result->SetStringField(TEXT("play_session"),
		bPlaying ? TEXT("playing") : (bSimulating ? TEXT("simulating") : TEXT("stopped")));

	// --- Active asset (most recently activated editor) -----------------------
	FString ActiveAssetName;
	FAssetData ActiveAsset;
	bool bActiveIsBlueprint = false;
	if (UAssetDiscoveryService::GetActiveAsset(ActiveAsset) && ActiveAsset.IsValid())
	{
		Result->SetObjectField(TEXT("active_asset"), AssetToJson(ActiveAsset));
		ActiveAssetName = ActiveAsset.AssetName.ToString();
		bActiveIsBlueprint = ActiveAsset.AssetClassPath.GetAssetName().ToString().Contains(TEXT("Blueprint"));
	}

	// --- Open asset editors --------------------------------------------------
	{
		const TArray<FAssetData> OpenAssets = UAssetDiscoveryService::GetOpenAssets();
		bool bTruncated = false;
		Result->SetArrayField(TEXT("open_assets"), AssetsToJson(OpenAssets, MaxItems, bTruncated));
		Result->SetNumberField(TEXT("open_assets_total"), OpenAssets.Num());
		if (bTruncated)
		{
			Result->SetBoolField(TEXT("open_assets_truncated"), true);
		}
	}

	// --- Content Browser selection -------------------------------------------
	{
		const TArray<FAssetData> Selected = UAssetDiscoveryService::GetContentBrowserSelections();
		bool bTruncated = false;
		Result->SetArrayField(TEXT("content_browser_selection"), AssetsToJson(Selected, MaxItems, bTruncated));
		Result->SetNumberField(TEXT("content_browser_selection_total"), Selected.Num());
		if (bTruncated)
		{
			Result->SetBoolField(TEXT("content_browser_selection_truncated"), true);
		}
	}

	// --- Selected actors in the viewport -------------------------------------
	int32 SelectedActorTotal = 0;
	{
		TArray<TSharedPtr<FJsonValue>> Actors;
		if (USelection* Selection = GEditor->GetSelectedActors())
		{
			for (int32 Index = 0; Index < Selection->Num(); ++Index)
			{
				AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(Index));
				if (!Actor)
				{
					continue;
				}
				++SelectedActorTotal;
				if (Actors.Num() >= MaxItems)
				{
					continue;
				}
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("label"), Actor->GetActorLabel());
				Entry->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				const FVector Location = Actor->GetActorLocation();
				TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
				Loc->SetNumberField(TEXT("x"), FMath::RoundToDouble(Location.X * 10.0) / 10.0);
				Loc->SetNumberField(TEXT("y"), FMath::RoundToDouble(Location.Y * 10.0) / 10.0);
				Loc->SetNumberField(TEXT("z"), FMath::RoundToDouble(Location.Z * 10.0) / 10.0);
				Entry->SetObjectField(TEXT("location"), Loc);
				Actors.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
		Result->SetArrayField(TEXT("selected_actors"), Actors);
		Result->SetNumberField(TEXT("selected_actors_total"), SelectedActorTotal);
	}

	// --- Selected Blueprint graph nodes --------------------------------------
	// Empty BlueprintPath = "the first open Blueprint editor with a non-empty
	// selection" - exactly the deictic case this tool serves.
	int32 SelectedNodeTotal = 0;
	{
		const TArray<FBlueprintNodeInfo> Nodes = UBlueprintService::GetSelectedNodes(FString());
		SelectedNodeTotal = Nodes.Num();
		TArray<TSharedPtr<FJsonValue>> NodeArray;
		for (const FBlueprintNodeInfo& Node : Nodes)
		{
			if (NodeArray.Num() >= MaxItems)
			{
				break;
			}
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("title"), Node.NodeTitle);
			Entry->SetStringField(TEXT("type"), Node.NodeType);
			Entry->SetStringField(TEXT("node_id"), Node.NodeId);
			NodeArray.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("selected_blueprint_nodes"), NodeArray);
		Result->SetNumberField(TEXT("selected_blueprint_nodes_total"), SelectedNodeTotal);
	}

	// --- Human-oriented one-liner --------------------------------------------
	TArray<FString> SummaryParts;
	if (!LevelName.IsEmpty())
	{
		SummaryParts.Add(FString::Printf(TEXT("level '%s'"), *LevelName));
	}
	if (!ActiveAssetName.IsEmpty())
	{
		SummaryParts.Add(FString::Printf(TEXT("active asset '%s'%s"),
			*ActiveAssetName, bActiveIsBlueprint ? TEXT(" (Blueprint)") : TEXT("")));
	}
	if (SelectedActorTotal > 0)
	{
		SummaryParts.Add(FString::Printf(TEXT("%d actor(s) selected"), SelectedActorTotal));
	}
	if (SelectedNodeTotal > 0)
	{
		SummaryParts.Add(FString::Printf(TEXT("%d graph node(s) selected"), SelectedNodeTotal));
	}
	Result->SetStringField(TEXT("summary"),
		SummaryParts.Num() > 0 ? FString::Join(SummaryParts, TEXT(", ")) : TEXT("idle editor, nothing selected"));

	return SerializeJson(Result);
}

} // namespace RevvyEditorContextTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(get_editor_context,
	"One read-only snapshot of what the user is looking at RIGHT NOW: loaded level, play session state, "
	"the most recently activated asset editor (active_asset), all open asset editors, the Content Browser "
	"selection, the actors selected in the viewport, and the Blueprint graph nodes selected in an open "
	"Blueprint editor.\n"
	"\nCall this FIRST whenever a request says 'this blueprint', 'the selected actors', 'here', 'what I "
	"have open' - it resolves deixis into concrete asset paths you can feed to read_blueprint, "
	"manage_level_actors and friends.\n"
	"\nRESULT SHAPE:\n"
	"  level {name, path, dirty} | play_session: stopped|playing|simulating\n"
	"  active_asset {name, class, path}       <- most recently activated editor tab\n"
	"  open_assets[] / content_browser_selection[] {name, class, path} (+ *_total)\n"
	"  selected_actors[] {label, class, location{x,y,z}} (+ selected_actors_total)\n"
	"  selected_blueprint_nodes[] {title, type, node_id} (+ *_total)\n"
	"  summary: one human-readable line\n"
	"\nSelection lives only in the open editor UI - nothing here loads, modifies or saves any asset.",
	"Editor",
	TOOL_ANNOTATIONS_READ_ONLY("Get Editor Context"),
	TOOL_PARAMS(
		TOOL_PARAM("max_items", "Cap per list (open assets, selections, nodes). Default 25, max 200", "number", false)
	),
	{
		return RevvyEditorContextTool::ComposeContextSnapshot(Params);
	}
);
