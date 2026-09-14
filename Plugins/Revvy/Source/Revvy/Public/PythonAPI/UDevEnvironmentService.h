// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UDevEnvironmentService.generated.h"

/**
 * Development-environment diagnostics and repair for the Revvy Python-facing
 * service layer. Backs the one-click artist setup flow (SetupUnrealDev.bat):
 * inside the editor the AI can diagnose a broken C++ toolchain, regenerate
 * Visual Studio project files, and rebuild the editor target without the
 * artist touching a terminal.
 *
 * All UFUNCTIONs are static + BlueprintCallable, reachable from Python as
 * `unreal.DevEnvironmentService.<snake_case_name>()`.
 *
 * Truth source for toolchain requirements is the engine's own
 * Engine/Config/Windows/Windows_SDK.json (minimum + banned MSVC versions,
 * Windows SDK range) — the same file UnrealBuildTool reads — so the checks
 * stay correct across engine upgrades.
 *
 * Long operations (GenerateProjectFiles / BuildEditorTarget) launch UBT
 * detached and return immediately; poll GetSetupStatus() for progress.
 * Only one detached task may run at a time.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.DevEnvironmentService
 *   env = json.loads(svc.check_environment())
 *   if not env["toolchain"]["ok"]:
 *       svc.run_setup_bootstrap(False)     # opens the one-click console
 *   svc.generate_project_files()
 *   while json.loads(svc.get_setup_status())["running"]: ...
 */
UCLASS(BlueprintType)
class REVVY_API UDevEnvironmentService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Full environment diagnosis. Read-only, safe to call any time.
	 *
	 * @return JSON {success,
	 *   engine: {root, version},
	 *   project: {path, name, has_source, project_files_exist},
	 *   requirements: {min_msvc, banned_msvc: [..], min_windows_sdk},
	 *   toolchain: {ok, installations: [{product, msvc_versions: [..]}],
	 *               valid_versions: [..], rejected: [{version, reason}]},
	 *   windows_sdk: {ok, versions: [..]},
	 *   bootstrap_script: str}   // full path to SetupUnrealDev.bat
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|DevEnvironment|Query")
	static FString CheckEnvironment();

	/**
	 * Regenerate Visual Studio project files via UBT
	 * (`Build.bat -projectfiles -project=<uproject> -game -waitmutex`).
	 * Installed (launcher) engines have no GenerateProjectFiles.bat — this is
	 * the canonical path. Launches detached; poll GetSetupStatus().
	 *
	 * @return JSON {success, started: bool, task, log_file} or
	 *         {success:false, error} when a task is already running or the
	 *         project has no Source folder.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|DevEnvironment|Edit")
	static FString GenerateProjectFiles();

	/**
	 * Build the project's editor target
	 * (`Build.bat <Target> Win64 <Configuration> -project=<uproject> -waitmutex`).
	 * Target auto-detected from Source\*Editor.Target.cs; content-only projects
	 * build the stock UnrealEditor target (compiles project plugin modules).
	 * Launches detached; poll GetSetupStatus(). NOTE: building while the editor
	 * holds module DLLs may fail with locked-file errors — prefer the external
	 * bootstrap for full rebuilds.
	 *
	 * @param Configuration "Development" (default), "DebugGame", or "Shipping".
	 * @return JSON {success, started: bool, task, target, log_file} or
	 *         {success:false, error}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|DevEnvironment|Edit")
	static FString BuildEditorTarget(const FString& Configuration = TEXT("Development"));

	/**
	 * Status of the last detached task started by this service.
	 *
	 * @param TailLines Log lines to return from the end (default 40, clamped [0,400]).
	 * @return JSON {success, task, running: bool, exit_code: int|-1, log_file,
	 *         log_tail, log_readable: bool}. task empty when nothing was started
	 *         this session; log_readable=false + exit_code!=0 hints the child
	 *         failed before it could write the log.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|DevEnvironment|Query")
	static FString GetSetupStatus(int32 TailLines = 40);

	/**
	 * Launch the one-click bootstrap (SetupUnrealDev.bat) in a visible console.
	 * This is the path that can INSTALL Visual Studio Build Tools (it shows one
	 * UAC prompt); the in-editor service never elevates on its own.
	 *
	 * Consent gate: when bCheckOnly=false an in-editor Yes/No dialog asks the
	 * artist first — an AI call alone can never start an elevated install.
	 * Declined → {success:false, error:"user_declined"}. Unattended sessions
	 * refuse full runs (error "user_confirmation_required").
	 *
	 * @param bCheckOnly true → pass -CheckOnly (diagnose, no install/build; no dialog).
	 * @return JSON {success, started: bool, script} or {success:false, error}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|DevEnvironment|Edit")
	static FString RunSetupBootstrap(bool bCheckOnly = false);

private:
	/** Load min/banned MSVC + min Windows SDK from the engine's Windows_SDK.json (with UE 5.8 fallbacks). */
	static void LoadEngineRequirements(FString& OutMinMsvc, TArray<FString>& OutBannedRanges, FString& OutMinWinSdk);

	/** Absolute path to Engine/Build/BatchFiles/Build.bat. */
	static FString GetBuildBatPath();

	/** Absolute path to this plugin's SetupUnrealDev.bat ("" if missing). */
	static FString GetBootstrapScriptPath();

	/** Launch `cmd /s /c "<command> > <log> 2>&1"` detached, tracking it as the active task. */
	static bool LaunchDetached(const FString& TaskName, const FString& Command, FString& OutLogFile, FString& OutError);
};
