// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Module.h"
#include "Modules/ModuleManager.h"
#include "EditorSubsystem.h"
#include "Editor.h"
#include "Chat/AIChatCommands.h"
#include "Core/CheckpointService.h"
#include "Core/ToolRegistry.h"
#include "MCP/MCPServer.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Tools/PythonTools.h"
#include "IPythonScriptPlugin.h"
#include "Utils/RevvyPaths.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyModule, Log, All);
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "FModule"

// Console command to list all registered tools
static void ListRevvyTools()
{
	FToolRegistry& Registry = FToolRegistry::Get();
	if (!Registry.IsInitialized())
	{
		UE_LOG(LogRevvyModule, Warning, TEXT("Tool Registry not initialized"));
		return;
	}

	const TArray<FToolMetadata>& Tools = Registry.GetAllTools();
	UE_LOG(LogRevvyModule, Display, TEXT("=== Revvy Tool Registry ==="));
	UE_LOG(LogRevvyModule, Display, TEXT("Total tools: %d"), Tools.Num());
	
	for (const FToolMetadata& Tool : Tools)
	{
		UE_LOG(LogRevvyModule, Display, TEXT("  Tool: %s"), *Tool.Name);
		UE_LOG(LogRevvyModule, Display, TEXT("    Category: %s"), *Tool.Category);
		UE_LOG(LogRevvyModule, Display, TEXT("    Description: %s"), *Tool.Description);
		UE_LOG(LogRevvyModule, Display, TEXT("    Parameters: %d"), Tool.Parameters.Num());
		for (const FToolParameter& Param : Tool.Parameters)
		{
			UE_LOG(LogRevvyModule, Display, TEXT("      - %s (%s, %s)"), 
				*Param.Name, 
				*Param.Type, 
				Param.bRequired ? TEXT("required") : TEXT("optional"));
		}
	}
}

// Console command to test a tool
static void TestRevvyTool(const TArray<FString>& Args, FOutputDevice& Ar)
{
	if (Args.Num() < 1)
	{
		Ar.Log(TEXT("Usage: Revvy.TestTool <ToolName> [ParamName=Value ...]"));
		return;
	}

	FToolRegistry& Registry = FToolRegistry::Get();
	if (!Registry.IsInitialized())
	{
		Ar.Log(TEXT("Tool Registry not initialized"));
		return;
	}

	FString ToolName = Args[0];
	TMap<FString, FString> Parameters;

	// Parse parameters
	for (int32 i = 1; i < Args.Num(); ++i)
	{
		FString Arg = Args[i];
		int32 EqualsIndex;
		if (Arg.FindChar(TEXT('='), EqualsIndex))
		{
			FString ParamName = Arg.Left(EqualsIndex);
			FString ParamValue = Arg.Mid(EqualsIndex + 1);
			Parameters.Add(ParamName, ParamValue);
		}
	}

	Ar.Logf(TEXT("Executing tool: %s"), *ToolName);
	FString Result = Registry.ExecuteTool(ToolName, Parameters);
	Ar.Logf(TEXT("Result: %s"), *Result);
}

static FAutoConsoleCommand ListToolsCommand(
	TEXT("Revvy.ListTools"),
	TEXT("List all registered Revvy tools"),
	FConsoleCommandDelegate::CreateStatic(ListRevvyTools)
);

// Console command to refresh tool registry
static void RefreshRevvyTools()
{
	FToolRegistry& Registry = FToolRegistry::Get();
	Registry.Refresh();
	UE_LOG(LogRevvyModule, Display, TEXT("Tool Registry refreshed. Total tools: %d"), Registry.GetAllTools().Num());
}

static FAutoConsoleCommand RefreshToolsCommand(
	TEXT("Revvy.RefreshTools"),
	TEXT("Refresh the Revvy tool registry"),
	FConsoleCommandDelegate::CreateStatic(RefreshRevvyTools)
);

static FAutoConsoleCommandWithArgsAndOutputDevice TestToolCommand(
	TEXT("Revvy.TestTool"),
	TEXT("Test a Revvy tool: Revvy.TestTool <ToolName> [ParamName=Value ...]"),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(TestRevvyTool)
);

void FModule::StartupModule()
{
	UE_LOG(LogRevvyModule, Display, TEXT("Revvy Module has started"));

	// Skip all interactive services when running as a commandlet (e.g. -run=Cook).
	// The MCP HTTP server and Python services keep threads alive that prevent
	// UnrealEditor-Cmd.exe from exiting cleanly, causing UAT cook timeouts.
	if (IsRunningCommandlet())
	{
		return;
	}

	bServicesInitialized = true;

	// Clear screenshots directory from previous sessions to save disk space
	FRevvyPaths::ClearScreenshotsDir();

	// Initialize Chat Rich Text Styles (for markdown rendering)

	// Initialize Tool Registry (reflection-based tools)
	FToolRegistry::Get().Initialize();

	// Initialize pre-save checkpoints (AI-driven asset changes keep their previous on-disk version)
	FRevvyCheckpointService::Get().Initialize();

	// Initialize AI Chat commands
	FAIChatCommands::Initialize();

	// Initialize MCP Server (auto-starts if enabled in config)
	FMCPServer::Get().Initialize();

	// Register PreExit callback to cleanup Python references before Unreal GC
	FCoreDelegates::OnPreExit.AddRaw(this, &FModule::OnPreExit);
}

void FModule::ShutdownModule()
{
	if (!bServicesInitialized)
	{
		UE_LOG(LogRevvyModule, Display, TEXT("Revvy Module has shut down"));
		return;
	}

	// Unregister PreExit callback
	FCoreDelegates::OnPreExit.RemoveAll(this);

	// Shutdown MCP Server
	FMCPServer::Get().Shutdown();

	// Shutdown AI Chat commands
	FAIChatCommands::Shutdown();

	// Shutdown pre-save checkpoints (unbinds the package delegate)
	FRevvyCheckpointService::Get().Shutdown();

	// Shutdown Tool Registry
	FToolRegistry::Get().Shutdown();

	// Shutdown Chat Rich Text Styles

	UE_LOG(LogRevvyModule, Display, TEXT("Revvy Module has shut down"));
}

void FModule::OnPreExit()
{
	UE_LOG(LogRevvyModule, Display, TEXT("Revvy OnPreExit - cleaning up Python services"));
	
	// Release all C++ Python service instances
	// This is safe because we're just clearing our own pointers
	UPythonTools::Shutdown();
	
	// NOTE: We deliberately do NOT call into Python here.
	// During OnPreExit, the Python interpreter may already be partially shut down
	// or in an inconsistent state. Calling ExecPythonCommand can cause access
	// violations (reading address 0x28 = null pointer + offset).
	// The C++ cleanup above is sufficient - Python's own shutdown will handle
	// the rest of the garbage collection.
	
	UE_LOG(LogRevvyModule, Display, TEXT("Revvy OnPreExit - C++ cleanup complete"));
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FModule, Revvy) 
