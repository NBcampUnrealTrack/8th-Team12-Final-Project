// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Core/ToolRegistry.h"
#include "Core/CheckpointService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ScopedTransaction.h"
#include "Editor.h"

DEFINE_LOG_CATEGORY(LogToolRegistry);

// Config section and key for disabled tools
static const TCHAR* ConfigSection = TEXT("Revvy.Tools");
static const TCHAR* DisabledToolsKey = TEXT("DisabledTools");

FToolRegistry& FToolRegistry::Get()
{
	static FToolRegistry Instance;
	return Instance;
}

FToolRegistry::FToolRegistry()
	: bInitialized(false)
{
}

FToolRegistry::~FToolRegistry()
{
	Shutdown();
}

void FToolRegistry::Initialize()
{
	if (bInitialized)
	{
		UE_LOG(LogToolRegistry, Log, TEXT("Tool Registry already initialized with %d tools"), Tools.Num());
		return;
	}

	UE_LOG(LogToolRegistry, Log, TEXT("Initializing Tool Registry..."));
	
	// Load disabled tools from config
	LoadDisabledToolsFromConfig();
	
	// Process any tools that were registered before Initialize() was called
	ProcessPendingRegistrations();

	// MCP tool annotations: tag each registered tool with readOnlyHint/destructiveHint/
	// idempotentHint/openWorldHint so clients (Cursor, Claude Desktop, ChatGPT Dev Mode)
	// stop gating safe getters behind destructive-write confirmations.
	ApplyBuiltInAnnotations();

	// Structured output: attach JSON Schemas to pilot tools so clients can validate
	// + destructure responses without re-parsing markdown.
	ApplyBuiltInOutputSchemas();

	bInitialized = true;
	
	int32 EnabledCount = Tools.Num() - DisabledTools.Num();
	UE_LOG(LogToolRegistry, Log, TEXT("Tool Registry initialized with %d tools (%d enabled, %d disabled)"), 
		Tools.Num(), EnabledCount, DisabledTools.Num());
	
	// Log all registered tools
	for (const FToolMetadata& Tool : Tools)
	{
		bool bEnabled = !DisabledTools.Contains(Tool.Name);
		UE_LOG(LogToolRegistry, Log, TEXT("  Tool: %s (Category: %s) - %d params [%s]"), 
			*Tool.Name, *Tool.Category, Tool.Parameters.Num(),
			bEnabled ? TEXT("ENABLED") : TEXT("DISABLED"));
	}
}

void FToolRegistry::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	Tools.Empty();
	ToolNameToIndex.Empty();
	ToolExecuteFuncs.Empty();
	bInitialized = false;
	UE_LOG(LogToolRegistry, Log, TEXT("Tool Registry shut down"));
}

bool FToolRegistry::AnnotateTool(const FString& ToolName, const FToolAnnotations& Annotations)
{
	const int32* Index = ToolNameToIndex.Find(ToolName);
	if (!Index || !Tools.IsValidIndex(*Index))
	{
		UE_LOG(LogToolRegistry, Verbose, TEXT("AnnotateTool: tool '%s' not found"), *ToolName);
		return false;
	}
	Tools[*Index].Annotations = Annotations;
	return true;
}

bool FToolRegistry::SetToolOutputSchema(const FString& ToolName, TSharedPtr<FJsonObject> Schema)
{
	const int32* Index = ToolNameToIndex.Find(ToolName);
	if (!Index || !Tools.IsValidIndex(*Index))
	{
		UE_LOG(LogToolRegistry, Verbose, TEXT("SetToolOutputSchema: tool '%s' not found"), *ToolName);
		return false;
	}
	Tools[*Index].OutputSchema = Schema;
	return true;
}

void FToolRegistry::ApplyBuiltInOutputSchemas()
{
	// Minimal pilot schemas: declare each tool's output shape as a JSON object with
	// `type=object` and the most-relevant top-level keys. Clients can still receive
	// looser data; the schema is a hint, not a hard contract.
	auto MakeObjectSchema = [](const TArray<TPair<FString, FString>>& Props) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
		for (const auto& P : Props)
		{
			TSharedPtr<FJsonObject> PropSchema = MakeShared<FJsonObject>();
			PropSchema->SetStringField(TEXT("type"), P.Value);
			PropsObj->SetObjectField(P.Key, PropSchema);
		}
		Schema->SetObjectField(TEXT("properties"), PropsObj);
		Schema->SetBoolField(TEXT("additionalProperties"), true);
		return Schema;
	};

	SetToolOutputSchema(TEXT("discover_python_module"), MakeObjectSchema({
		{TEXT("module"), TEXT("string")},
		{TEXT("classes"), TEXT("array")},
		{TEXT("functions"), TEXT("array")},
	}));
	SetToolOutputSchema(TEXT("discover_python_class"), MakeObjectSchema({
		{TEXT("class"), TEXT("string")},
		{TEXT("methods"), TEXT("array")},
		{TEXT("properties"), TEXT("array")},
		{TEXT("bases"), TEXT("array")},
	}));
	SetToolOutputSchema(TEXT("discover_python_function"), MakeObjectSchema({
		{TEXT("function"), TEXT("string")},
		{TEXT("signature"), TEXT("string")},
		{TEXT("docstring"), TEXT("string")},
	}));
	SetToolOutputSchema(TEXT("list_python_subsystems"), MakeObjectSchema({
		{TEXT("subsystems"), TEXT("array")},
	}));
	SetToolOutputSchema(TEXT("read_logs"), MakeObjectSchema({
		{TEXT("lines"), TEXT("array")},
		{TEXT("source"), TEXT("string")},
		{TEXT("truncated"), TEXT("boolean")},
	}));
	UE_LOG(LogToolRegistry, Log, TEXT("Built-in output schemas applied to 5 pilot tools"));
}

void FToolRegistry::ApplyBuiltInAnnotations()
{
	auto Annotate = [this](const TCHAR* Name, bool bReadOnly, bool bDestructive, bool bIdempotent, bool bOpenWorld, const TCHAR* Title, const TArray<FString>& ReadOnlyActions = {})
	{
		FToolAnnotations Ann;
		Ann.Title = Title;
		Ann.bReadOnlyHint = bReadOnly;
		Ann.bDestructiveHint = bDestructive;
		Ann.bIdempotentHint = bIdempotent;
		Ann.bOpenWorldHint = bOpenWorld;
		Ann.bAnnotated = true;
		Ann.ReadOnlyActions = ReadOnlyActions;
		AnnotateTool(Name, Ann);
	};

	// Read-only + local + idempotent (introspection): safe, agents can call freely.
	Annotate(TEXT("discover_python_module"),    /*ReadOnly*/ true,  /*Destructive*/ false, /*Idempotent*/ true,  /*OpenWorld*/ false, TEXT("Discover Python Module"));
	Annotate(TEXT("discover_python_class"),     true,  false, true,  false, TEXT("Discover Python Class"));
	Annotate(TEXT("discover_python_function"),  true,  false, true,  false, TEXT("Discover Python Function"));
	Annotate(TEXT("list_python_subsystems"),    true,  false, true,  false, TEXT("List Python Subsystems"));

	// Read-only but non-idempotent (log tail rolls; results can change between calls).
	Annotate(TEXT("read_logs"),                 true,  false, false, false, TEXT("Read Editor Logs"));

	// Mixed action verbs (manage_*): conservative — non-readOnly, but local + non-destructive
	// in the data-loss sense. Clients that gate strictly on destructiveHint can still allow.
	//
	// The trailing list names the actions a read-only session may still call, so its
	// queries stay reachable instead of the whole tool disappearing. Anything that
	// writes a file, saves a package, or opens an asset editor is deliberately absent:
	//   manage_asset  'open'              loads the package and opens an editor for it
	//   terrain_data  'get_water_features' always writes its result to disk
	//   terrain_data  'get_map_image' / 'generate_heightmap'  write to a caller-set path
	//   manage_skills 'load'              mutates and persists in-editor chat session state
	Annotate(TEXT("manage_skills"),             false, false, false, false, TEXT("Manage Skills"),
		{TEXT("list"), TEXT("suggest")});
	Annotate(TEXT("terrain_data"),              false, false, false, false, TEXT("Terrain Data Query"),
		{TEXT("preview_elevation"), TEXT("list_styles")});

	// Read-only but reaches the open web → openWorldHint = true.
	Annotate(TEXT("deep_research"),             true,  false, false, true,  TEXT("Deep Research (Web)"));

	// Genuinely destructive writes — explicit so clients know to ask before re-running.
	Annotate(TEXT("manage_asset"),              false, true,  false, false, TEXT("Manage Asset (incl. save/delete)"),
		{TEXT("search"), TEXT("find"), TEXT("list"), TEXT("help")});
	Annotate(TEXT("execute_python_code"),       false, true,  false, false, TEXT("Execute Python (arbitrary)"));

	int32 AnnotatedCount = 0;
	for (const FToolMetadata& T : Tools)
	{
		if (T.Annotations.bAnnotated)
		{
			++AnnotatedCount;
		}
	}
	UE_LOG(LogToolRegistry, Log, TEXT("Built-in MCP annotations applied: %d / %d tools annotated"), AnnotatedCount, Tools.Num());
}

void FToolRegistry::RegisterTool(const FToolRegistration& Registration)
{
	// If not initialized yet, queue for later
	if (!bInitialized)
	{
		PendingRegistrations.Add(Registration);
		UE_LOG(LogToolRegistry, Verbose, TEXT("Queued tool for registration: %s"), *Registration.Name);
		return;
	}

	// Check if tool already exists
	if (ToolNameToIndex.Contains(Registration.Name))
	{
		UE_LOG(LogToolRegistry, Warning, TEXT("Tool '%s' already registered, skipping"), *Registration.Name);
		return;
	}

	// Create metadata
	FToolMetadata Metadata;
	Metadata.Name = Registration.Name;
	Metadata.Description = Registration.Description;
	Metadata.Category = Registration.Category;
	Metadata.Parameters = Registration.Parameters;
	Metadata.bInternalOnly = Registration.bInternalOnly;
	Metadata.Annotations = Registration.Annotations;

	// Add tool
	int32 Index = Tools.Add(Metadata);
	ToolNameToIndex.Add(Registration.Name, Index);
	ToolExecuteFuncs.Add(Registration.Name, Registration.ExecuteFunc);

	UE_LOG(LogToolRegistry, Log, TEXT("Registered tool: %s (Category: %s)"), *Registration.Name, *Registration.Category);
}

void FToolRegistry::ProcessPendingRegistrations()
{
	UE_LOG(LogToolRegistry, Log, TEXT("Processing %d pending tool registrations..."), PendingRegistrations.Num());
	
	for (const FToolRegistration& Registration : PendingRegistrations)
	{
		// Check if tool already exists
		if (ToolNameToIndex.Contains(Registration.Name))
		{
			UE_LOG(LogToolRegistry, Warning, TEXT("Tool '%s' already registered, skipping"), *Registration.Name);
			continue;
		}

		// Create metadata
		FToolMetadata Metadata;
		Metadata.Name = Registration.Name;
		Metadata.Description = Registration.Description;
		Metadata.Category = Registration.Category;
		Metadata.Parameters = Registration.Parameters;
		Metadata.bInternalOnly = Registration.bInternalOnly;
		Metadata.Annotations = Registration.Annotations;

		// Add tool
		int32 Index = Tools.Add(Metadata);
		ToolNameToIndex.Add(Registration.Name, Index);
		ToolExecuteFuncs.Add(Registration.Name, Registration.ExecuteFunc);

		UE_LOG(LogToolRegistry, Log, TEXT("Registered tool: %s (Category: %s, InternalOnly: %s)"), 
			*Registration.Name, *Registration.Category, Registration.bInternalOnly ? TEXT("Yes") : TEXT("No"));
	}
	
	PendingRegistrations.Empty();
}

void FToolRegistry::Refresh()
{
	UE_LOG(LogToolRegistry, Log, TEXT("Refreshing Tool Registry"));
	Tools.Empty();
	ToolNameToIndex.Empty();
	ToolExecuteFuncs.Empty();
	bInitialized = false;
	Initialize();
}

TArray<FToolMetadata> FToolRegistry::GetToolsByCategory(const FString& Category) const
{
	TArray<FToolMetadata> Result;
	for (const FToolMetadata& Tool : Tools)
	{
		if (Tool.Category == Category)
		{
			Result.Add(Tool);
		}
	}
	return Result;
}

const FToolMetadata* FToolRegistry::FindTool(const FString& ToolName) const
{
	const int32* IndexPtr = ToolNameToIndex.Find(ToolName);
	if (IndexPtr && Tools.IsValidIndex(*IndexPtr))
	{
		return &Tools[*IndexPtr];
	}
	return nullptr;
}

bool FToolRegistry::ValidateParameters(
	const FToolMetadata& Tool,
	const TMap<FString, FString>& Parameters,
	FString& OutError)
{
	// Check required parameters
	for (const FToolParameter& Param : Tool.Parameters)
	{
		if (Param.bRequired)
		{
			// First check if parameter exists directly
			if (Parameters.Contains(Param.Name))
			{
				continue;
			}

			// If not found directly, check if it's in ParamsJson
			const FString* ParamsJsonStr = Parameters.Find(TEXT("ParamsJson"));
			bool bFoundInJson = false;
			
			if (ParamsJsonStr)
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*ParamsJsonStr);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					if (JsonObj->HasField(*Param.Name))
					{
						bFoundInJson = true;
					}
				}
			}

			if (!bFoundInJson)
			{
				OutError = FString::Printf(TEXT("Missing required parameter: %s"), *Param.Name);
				return false;
			}
		}
	}
	return true;
}

FString FToolRegistry::ExecuteTool(
	const FString& ToolName,
	const TMap<FString, FString>& Parameters)
{
	// Check if tool is disabled FIRST
	if (!IsToolEnabled(ToolName))
	{
		UE_LOG(LogToolRegistry, Warning, TEXT("Attempted to execute disabled tool: %s"), *ToolName);
		
		TSharedPtr<FJsonObject> ErrorResult = MakeShareable(new FJsonObject);
		ErrorResult->SetBoolField(TEXT("success"), false);
		ErrorResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Tool '%s' is disabled"), *ToolName));
		ErrorResult->SetStringField(TEXT("error_code"), TEXT("TOOL_DISABLED"));

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(ErrorResult.ToSharedRef(), Writer);
		return OutputString;
	}
	
	const FToolMetadata* Tool = FindTool(ToolName);
	if (!Tool)
	{
		TSharedPtr<FJsonObject> ErrorResult = MakeShareable(new FJsonObject);
		ErrorResult->SetBoolField(TEXT("success"), false);
		ErrorResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Tool '%s' not found"), *ToolName));

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(ErrorResult.ToSharedRef(), Writer);
		return OutputString;
	}

	// Validate parameters
	FString ValidationError;
	if (!ValidateParameters(*Tool, Parameters, ValidationError))
	{
		TSharedPtr<FJsonObject> ErrorResult = MakeShareable(new FJsonObject);
		ErrorResult->SetBoolField(TEXT("success"), false);
		ErrorResult->SetStringField(TEXT("error"), ValidationError);

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(ErrorResult.ToSharedRef(), Writer);
		return OutputString;
	}

	// Execute
	FToolExecuteFunc* ExecuteFunc = ToolExecuteFuncs.Find(ToolName);
	if (ExecuteFunc && *ExecuteFunc)
	{
		UE_LOG(LogToolRegistry, Verbose, TEXT("Executing tool: %s"), *ToolName);

		// Wrap mutating tools in an editor transaction so every AI-driven change is a
		// single Ctrl+Z entry. Skip read-only-annotated tools and non-game-thread calls
		// (the transaction system is game-thread only).
		//
		// Never wrap execute_python_code: it runs arbitrary code, and a blanket
		// transaction records whatever that code touches into the undo buffer —
		// recorded play-world state keeps the PIE GameInstance referenced by the
		// TransBuffer after PIE ends, which is a fatal stale-world-reference error
		// at cleanup. Revvy services open their own scoped transactions internally,
		// so Python-driven edits keep undo support.
		//
		// Never open a transaction while PIE is active either — the engine does not
		// guard UTransBuffer against play worlds, so any Modify() on a play-world
		// object would be captured the same way.
		const bool bIsReadOnlyTool = Tool->Annotations.bAnnotated && Tool->Annotations.bReadOnlyHint;
		const bool bIsArbitraryExecution = ToolName == TEXT("execute_python_code");
		const bool bIsPIEActive = GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor);

		// While a tool executes, any package save it triggers gets its previous
		// on-disk version checkpointed (see FRevvyCheckpointService). The scope
		// covers read-only tools too - they should never save, but if one does,
		// that is exactly the save worth preserving evidence of.
		FRevvyCheckpointToolScope CheckpointScope(ToolName);

		if (!bIsReadOnlyTool && !bIsArbitraryExecution && !bIsPIEActive && IsInGameThread())
		{
			FScopedTransaction Transaction(FText::Format(
				NSLOCTEXT("Revvy", "RevvyToolTransaction", "Revvy: {0}"),
				FText::FromString(ToolName)));
			return (*ExecuteFunc)(Parameters);
		}

		return (*ExecuteFunc)(Parameters);
	}

	TSharedPtr<FJsonObject> ErrorResult = MakeShareable(new FJsonObject);
	ErrorResult->SetBoolField(TEXT("success"), false);
	ErrorResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Tool '%s' has no execute function"), *ToolName));

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(ErrorResult.ToSharedRef(), Writer);
	return OutputString;
}

TArray<FToolMetadata> FToolRegistry::GetEnabledTools() const
{
	TArray<FToolMetadata> Result;
	UE_LOG(LogToolRegistry, Verbose, TEXT("GetEnabledTools: Total tools=%d, Disabled count=%d"), Tools.Num(), DisabledTools.Num());
	
	for (const FToolMetadata& Tool : Tools)
	{
		bool bDisabled = DisabledTools.Contains(Tool.Name);
		if (!bDisabled)
		{
			Result.Add(Tool);
		}
		else
		{
			UE_LOG(LogToolRegistry, Log, TEXT("  SKIPPING disabled tool: %s"), *Tool.Name);
		}
	}
	
	UE_LOG(LogToolRegistry, Verbose, TEXT("GetEnabledTools: Returning %d enabled tools"), Result.Num());
	return Result;
}

bool FToolRegistry::IsToolEnabled(const FString& ToolName) const
{
	return !DisabledTools.Contains(ToolName);
}

void FToolRegistry::SetToolEnabled(const FString& ToolName, bool bEnabled)
{
	bool bChanged = false;
	
	UE_LOG(LogToolRegistry, Log, TEXT("SetToolEnabled called: Tool='%s', bEnabled=%s, CurrentlyDisabled=%s"), 
		*ToolName, 
		bEnabled ? TEXT("true") : TEXT("false"),
		DisabledTools.Contains(ToolName) ? TEXT("true") : TEXT("false"));
	
	if (bEnabled)
	{
		// Enabling a tool - remove from disabled set
		if (DisabledTools.Contains(ToolName))
		{
			DisabledTools.Remove(ToolName);
			bChanged = true;
			UE_LOG(LogToolRegistry, Log, TEXT("Enabled tool: %s (removed from disabled set)"), *ToolName);
		}
		else
		{
			UE_LOG(LogToolRegistry, Log, TEXT("Tool %s already enabled, no change needed"), *ToolName);
		}
	}
	else
	{
		// Disabling a tool - add to disabled set
		if (!DisabledTools.Contains(ToolName))
		{
			DisabledTools.Add(ToolName);
			bChanged = true;
			UE_LOG(LogToolRegistry, Log, TEXT("Disabled tool: %s (added to disabled set)"), *ToolName);
		}
		else
		{
			UE_LOG(LogToolRegistry, Log, TEXT("Tool %s already disabled, no change needed"), *ToolName);
		}
	}
	
	if (bChanged)
	{
		UE_LOG(LogToolRegistry, Log, TEXT("Tool state changed, saving to config..."));
		SaveDisabledToolsToConfig();
		OnToolEnabledChanged.Broadcast(ToolName, bEnabled);
	}
}

void FToolRegistry::SetDisabledToolsAndSave(const TSet<FString>& NewDisabledTools)
{
	UE_LOG(LogToolRegistry, Log, TEXT("=== SetDisabledToolsAndSave called ==="));
	UE_LOG(LogToolRegistry, Log, TEXT("Old disabled count: %d, New disabled count: %d"), DisabledTools.Num(), NewDisabledTools.Num());
	
	// Log what's changing
	for (const FString& Tool : NewDisabledTools)
	{
		if (!DisabledTools.Contains(Tool))
		{
			UE_LOG(LogToolRegistry, Log, TEXT("  NEWLY DISABLED: %s"), *Tool);
		}
	}
	for (const FString& Tool : DisabledTools)
	{
		if (!NewDisabledTools.Contains(Tool))
		{
			UE_LOG(LogToolRegistry, Log, TEXT("  NEWLY ENABLED: %s"), *Tool);
		}
	}
	
	// Replace the entire set
	DisabledTools = NewDisabledTools;
	
	// Force save regardless of whether anything changed
	SaveDisabledToolsToConfig();
	
	UE_LOG(LogToolRegistry, Log, TEXT("=== SetDisabledToolsAndSave complete ==="));
}

void FToolRegistry::LoadDisabledToolsFromConfig()
{
	DisabledTools.Empty();
	
	// Use INI config file in Saved/Config directory
	FString ConfigPath = FPaths::ProjectSavedDir() / TEXT("Config") / TEXT("Revvy.ini");
	
	UE_LOG(LogToolRegistry, Log, TEXT("=== LOADING DISABLED TOOLS ==="));
	UE_LOG(LogToolRegistry, Log, TEXT("Config path: %s"), *ConfigPath);
	
	// Read from INI file
	FString DisabledToolsStr;
	if (GConfig->GetString(ConfigSection, DisabledToolsKey, DisabledToolsStr, ConfigPath))
	{
		UE_LOG(LogToolRegistry, Log, TEXT("INI value: [%s]"), *DisabledToolsStr);
		
		// Parse comma-separated list
		TArray<FString> ToolNames;
		DisabledToolsStr.ParseIntoArray(ToolNames, TEXT(","), true);
		
		for (const FString& ToolName : ToolNames)
		{
			FString TrimmedName = ToolName.TrimStartAndEnd();
			if (!TrimmedName.IsEmpty())
			{
				DisabledTools.Add(TrimmedName);
				UE_LOG(LogToolRegistry, Log, TEXT("  Added to DisabledTools: [%s]"), *TrimmedName);
			}
		}
		
		UE_LOG(LogToolRegistry, Log, TEXT("Total disabled tools loaded: %d"), DisabledTools.Num());
	}
	else
	{
		UE_LOG(LogToolRegistry, Log, TEXT("No disabled tools found in config - first run or all tools enabled"));
	}
	UE_LOG(LogToolRegistry, Log, TEXT("=== END LOADING DISABLED TOOLS ==="));
}

void FToolRegistry::SaveDisabledToolsToConfig()
{
	// Use INI config file in Saved/Config directory
	FString ConfigPath = FPaths::ProjectSavedDir() / TEXT("Config") / TEXT("Revvy.ini");
	
	UE_LOG(LogToolRegistry, Log, TEXT("=== SaveDisabledToolsToConfig ==="));
	UE_LOG(LogToolRegistry, Log, TEXT("Config path: %s"), *ConfigPath);
	
	// Build comma-separated string
	FString DisabledToolsStr;
	for (const FString& ToolName : DisabledTools)
	{
		if (!DisabledToolsStr.IsEmpty())
		{
			DisabledToolsStr += TEXT(",");
		}
		DisabledToolsStr += ToolName;
	}
	
	UE_LOG(LogToolRegistry, Log, TEXT("DisabledTools string: [%s]"), *DisabledToolsStr);
	
	// Write directly to file to ensure it actually saves
	FString FileContent = FString::Printf(TEXT("[%s]\n%s=%s\n"), ConfigSection, DisabledToolsKey, *DisabledToolsStr);
	
	if (FFileHelper::SaveStringToFile(FileContent, *ConfigPath))
	{
		UE_LOG(LogToolRegistry, Log, TEXT("Successfully wrote to file: %s"), *ConfigPath);
	}
	else
	{
		UE_LOG(LogToolRegistry, Error, TEXT("FAILED to write to file: %s"), *ConfigPath);
	}
	
	// Also update GConfig so in-memory state is consistent
	GConfig->SetString(ConfigSection, DisabledToolsKey, *DisabledToolsStr, ConfigPath);
	
	UE_LOG(LogToolRegistry, Log, TEXT("Saved %d disabled tools"), DisabledTools.Num());
}
