// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectSaveContext.h"

class FJsonObject;
class UPackage;

/**
 * Automatic pre-save checkpoints for AI-driven asset changes.
 *
 * Editor transactions give in-session undo, but once a Revvy tool SAVES a
 * package the previous on-disk version is gone - no ecosystem tool guards
 * that today. This service listens to UPackage::PreSavePackageWithContextEvent
 * and, whenever a save happens while a Revvy tool is executing (see
 * FToolRegistry's scope hook), copies the existing on-disk file into
 * Saved/Revvy/Checkpoints/<day>/ next to a manifest.jsonl line describing it.
 * The manage_checkpoints tool lists and restores those copies.
 *
 * Storage is bounded by config ([Revvy.Checkpoints] in Saved/Config/Revvy.ini):
 *   bEnabled            default true
 *   bRevvyToolScopeOnly default true  (false = checkpoint EVERY editor save)
 *   MaxAgeDays          default 14
 *   MaxTotalMB          default 1024
 * Pruning runs at Initialize and after every capture, whole day-directories
 * at a time, oldest first.
 */
class REVVY_API FRevvyCheckpointService
{
public:
	static FRevvyCheckpointService& Get();

	void Initialize();
	void Shutdown();

	/** Game-thread tool-scope stack, driven by FToolRegistry::ExecuteTool. */
	void PushToolScope(const FString& ToolName);
	void PopToolScope();
	FString CurrentToolScope() const;

	/**
	 * Copy SourceFile into today's checkpoint directory and append a manifest
	 * entry. Public so tests can exercise the capture path without a real
	 * package save. Returns false (without OutBackupId) when the source is
	 * unreadable or the copy fails.
	 */
	bool RecordBackup(const FString& PackageName, const FString& SourceFile, const FString& ToolName, FString& OutBackupId);

	/** Manifest entries, newest first. PackageFilter is a case-insensitive substring. */
	TArray<TSharedPtr<FJsonObject>> ListCheckpoints(const FString& PackageFilter, int32 Limit) const;

	/**
	 * Copy the backup identified by BackupId over its original file. Refuses
	 * when the package is loaded AND dirty unless bForce (in-memory edits would
	 * be silently lost). A loaded, clean package is reloaded from disk after
	 * the copy so the editor shows the restored content.
	 */
	bool RestoreCheckpoint(const FString& BackupId, bool bForce, FString& OutError, FString& OutRestoredPackage);

	/** Delete day-directories older than MaxAgeDays, then oldest-first beyond MaxTotalMegabytes. Returns removed entry count. */
	int32 PruneCheckpoints(double MaxAgeDays, double MaxTotalMegabytes);

	/** {enabled, scope_mode, root_dir, entries, total_bytes} for the status action. */
	TSharedPtr<FJsonObject> GetStatusJson() const;

	bool IsEnabled() const { return bEnabled; }
	FString CheckpointRootDir() const;

private:
	FRevvyCheckpointService() = default;

	void LoadConfig();
	void OnPreSavePackage(UPackage* Package, FObjectPreSaveContext Context);
	bool FindManifestEntry(const FString& BackupId, TSharedPtr<FJsonObject>& OutEntry) const;

	FDelegateHandle PreSaveHandle;
	TArray<FString> ToolScopeStack;
	int32 CaptureCounter = 0;
	bool bInitialized = false;
	bool bEnabled = true;
	bool bRevvyToolScopeOnly = true;
	double MaxAgeDays = 14.0;
	double MaxTotalMB = 1024.0;
};

/** RAII helper FToolRegistry wraps around every tool execution. */
struct FRevvyCheckpointToolScope
{
	explicit FRevvyCheckpointToolScope(const FString& ToolName)
	{
		FRevvyCheckpointService::Get().PushToolScope(ToolName);
	}
	~FRevvyCheckpointToolScope()
	{
		FRevvyCheckpointService::Get().PopToolScope();
	}
};
