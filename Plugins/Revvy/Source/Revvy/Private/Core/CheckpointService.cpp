// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Core/CheckpointService.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCheckpoint, Log, All);

namespace
{
	FString DayDirName(const FDateTime& Time)
	{
		return FString::Printf(TEXT("%04d%02d%02d"), Time.GetYear(), Time.GetMonth(), Time.GetDay());
	}

	FString SerializeEntry(const TSharedPtr<FJsonObject>& Entry)
	{
		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Entry.ToSharedRef(), Writer);
		return Json;
	}

	/** Manifest lines of one day directory, in file order (oldest first). */
	TArray<TSharedPtr<FJsonObject>> ReadManifest(const FString& DayDir)
	{
		TArray<TSharedPtr<FJsonObject>> Entries;
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *(DayDir / TEXT("manifest.jsonl"))))
		{
			return Entries;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> Entry;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
			if (FJsonSerializer::Deserialize(Reader, Entry) && Entry.IsValid())
			{
				Entries.Add(Entry);
			}
		}
		return Entries;
	}

	/** Day directories under the root, sorted ascending (oldest first). */
	TArray<FString> SortedDayDirs(const FString& Root)
	{
		TArray<FString> Dirs;
		IFileManager::Get().IterateDirectory(*Root, [&Dirs](const TCHAR* Path, bool bIsDirectory)
		{
			if (bIsDirectory)
			{
				Dirs.Add(FString(Path));
			}
			return true;
		});
		Dirs.Sort();
		return Dirs;
	}

	int64 DirectorySizeBytes(const FString& Dir)
	{
		int64 Total = 0;
		IFileManager::Get().IterateDirectoryRecursively(*Dir, [&Total](const TCHAR* Path, bool bIsDirectory)
		{
			if (!bIsDirectory)
			{
				Total += IFileManager::Get().FileSize(Path);
			}
			return true;
		});
		return Total;
	}
}

FRevvyCheckpointService& FRevvyCheckpointService::Get()
{
	static FRevvyCheckpointService Instance;
	return Instance;
}

FString FRevvyCheckpointService::CheckpointRootDir() const
{
	return FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("Checkpoints");
}

void FRevvyCheckpointService::LoadConfig()
{
	const FString ConfigPath = FPaths::ProjectSavedDir() / TEXT("Config") / TEXT("Revvy.ini");
	const TCHAR* Section = TEXT("Revvy.Checkpoints");
	GConfig->GetBool(Section, TEXT("bEnabled"), bEnabled, ConfigPath);
	GConfig->GetBool(Section, TEXT("bRevvyToolScopeOnly"), bRevvyToolScopeOnly, ConfigPath);
	GConfig->GetDouble(Section, TEXT("MaxAgeDays"), MaxAgeDays, ConfigPath);
	GConfig->GetDouble(Section, TEXT("MaxTotalMB"), MaxTotalMB, ConfigPath);
	MaxAgeDays = FMath::Clamp(MaxAgeDays, 1.0, 365.0);
	MaxTotalMB = FMath::Clamp(MaxTotalMB, 16.0, 65536.0);
}

void FRevvyCheckpointService::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	LoadConfig();
	PreSaveHandle = UPackage::PreSavePackageWithContextEvent.AddRaw(
		this, &FRevvyCheckpointService::OnPreSavePackage);
	bInitialized = true;

	const int32 Pruned = PruneCheckpoints(MaxAgeDays, MaxTotalMB);
	UE_LOG(LogRevvyCheckpoint, Log,
		TEXT("Checkpoint service up (enabled=%d, revvy_scope_only=%d, pruned %d stale entries)"),
		bEnabled ? 1 : 0, bRevvyToolScopeOnly ? 1 : 0, Pruned);
}

void FRevvyCheckpointService::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}
	UPackage::PreSavePackageWithContextEvent.Remove(PreSaveHandle);
	PreSaveHandle.Reset();
	bInitialized = false;
}

void FRevvyCheckpointService::PushToolScope(const FString& ToolName)
{
	if (IsInGameThread())
	{
		ToolScopeStack.Push(ToolName);
	}
}

void FRevvyCheckpointService::PopToolScope()
{
	if (IsInGameThread() && ToolScopeStack.Num() > 0)
	{
		ToolScopeStack.Pop();
	}
}

FString FRevvyCheckpointService::CurrentToolScope() const
{
	return ToolScopeStack.Num() > 0 ? ToolScopeStack.Last() : FString();
}

void FRevvyCheckpointService::OnPreSavePackage(UPackage* Package, FObjectPreSaveContext Context)
{
	if (!bEnabled || !Package || !IsInGameThread())
	{
		return;
	}
	if (Context.IsCooking())
	{
		return;
	}
	if (bRevvyToolScopeOnly && ToolScopeStack.IsEmpty())
	{
		return;
	}

	const FString Target = Context.GetTargetFilename();
	if (Target.IsEmpty() || !FPaths::FileExists(Target))
	{
		// First save of a new asset - there is no previous version to preserve.
		return;
	}

	// Only guard content saved in place. Autosaves, cooked output and temp
	// targets land outside Content/ and must not be captured.
	const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	if (!FPaths::ConvertRelativePathToFull(Target).StartsWith(ContentDir))
	{
		return;
	}

	FString BackupId;
	RecordBackup(Package->GetName(), Target, CurrentToolScope(), BackupId);
}

bool FRevvyCheckpointService::RecordBackup(
	const FString& PackageName, const FString& SourceFile, const FString& ToolName, FString& OutBackupId)
{
	const FDateTime Now = FDateTime::Now();
	const FString DayDir = CheckpointRootDir() / DayDirName(Now);
	IFileManager::Get().MakeDirectory(*DayDir, /*Tree=*/true);

	const FString Id = FString::Printf(TEXT("%s-%02d%02d%02d-%d"),
		*DayDirName(Now), Now.GetHour(), Now.GetMinute(), Now.GetSecond(), ++CaptureCounter);
	const FString BackupFile = DayDir / FString::Printf(TEXT("%s_%s"),
		*Id, *FPaths::GetCleanFilename(SourceFile));

	if (IFileManager::Get().Copy(*BackupFile, *SourceFile) != COPY_OK)
	{
		UE_LOG(LogRevvyCheckpoint, Warning, TEXT("Checkpoint copy failed: %s -> %s"), *SourceFile, *BackupFile);
		return false;
	}

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("id"), Id);
	Entry->SetStringField(TEXT("timestamp"), Now.ToIso8601());
	Entry->SetStringField(TEXT("package"), PackageName);
	Entry->SetStringField(TEXT("tool"), ToolName.IsEmpty() ? FString(TEXT("(editor)")) : ToolName);
	Entry->SetStringField(TEXT("original_file"), FPaths::ConvertRelativePathToFull(SourceFile));
	Entry->SetStringField(TEXT("backup_file"), FPaths::ConvertRelativePathToFull(BackupFile));
	Entry->SetNumberField(TEXT("size_bytes"), (double)IFileManager::Get().FileSize(*BackupFile));

	FFileHelper::SaveStringToFile(SerializeEntry(Entry) + LINE_TERMINATOR,
		*(DayDir / TEXT("manifest.jsonl")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);

	OutBackupId = Id;
	UE_LOG(LogRevvyCheckpoint, Log, TEXT("Checkpointed %s (%s) as %s"), *PackageName, *ToolName, *Id);

	PruneCheckpoints(MaxAgeDays, MaxTotalMB);
	return true;
}

TArray<TSharedPtr<FJsonObject>> FRevvyCheckpointService::ListCheckpoints(
	const FString& PackageFilter, int32 Limit) const
{
	TArray<TSharedPtr<FJsonObject>> All;
	for (const FString& DayDir : SortedDayDirs(CheckpointRootDir()))
	{
		All.Append(ReadManifest(DayDir));
	}
	// Manifest order is oldest first; answer newest first.
	Algo::Reverse(All);

	TArray<TSharedPtr<FJsonObject>> Filtered;
	for (const TSharedPtr<FJsonObject>& Entry : All)
	{
		if (!PackageFilter.IsEmpty())
		{
			FString Package;
			Entry->TryGetStringField(TEXT("package"), Package);
			if (!Package.Contains(PackageFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}
		Filtered.Add(Entry);
		if (Filtered.Num() >= Limit)
		{
			break;
		}
	}
	return Filtered;
}

bool FRevvyCheckpointService::FindManifestEntry(const FString& BackupId, TSharedPtr<FJsonObject>& OutEntry) const
{
	for (const FString& DayDir : SortedDayDirs(CheckpointRootDir()))
	{
		for (const TSharedPtr<FJsonObject>& Entry : ReadManifest(DayDir))
		{
			FString Id;
			if (Entry->TryGetStringField(TEXT("id"), Id) && Id == BackupId)
			{
				OutEntry = Entry;
				return true;
			}
		}
	}
	return false;
}

bool FRevvyCheckpointService::RestoreCheckpoint(
	const FString& BackupId, bool bForce, FString& OutError, FString& OutRestoredPackage)
{
	TSharedPtr<FJsonObject> Entry;
	if (!FindManifestEntry(BackupId, Entry))
	{
		OutError = FString::Printf(TEXT("No checkpoint with id '%s'. Use action='list' to browse."), *BackupId);
		return false;
	}

	const FString BackupFile = Entry->GetStringField(TEXT("backup_file"));
	const FString OriginalFile = Entry->GetStringField(TEXT("original_file"));
	const FString PackageName = Entry->GetStringField(TEXT("package"));
	if (!FPaths::FileExists(BackupFile))
	{
		OutError = FString::Printf(TEXT("Backup file is gone (pruned?): %s"), *BackupFile);
		return false;
	}

	UPackage* Loaded = FindPackage(nullptr, *PackageName);
	if (Loaded && Loaded->IsDirty() && !bForce)
	{
		OutError = FString::Printf(
			TEXT("Package %s has UNSAVED in-memory edits; restoring would discard them on reload. "
				"Save or revert first, or pass force=true to discard."), *PackageName);
		return false;
	}

	if (IFileManager::Get().Copy(*OriginalFile, *BackupFile) != COPY_OK)
	{
		OutError = FString::Printf(TEXT("Could not write %s (file locked?)."), *OriginalFile);
		return false;
	}

	if (Loaded)
	{
		FText ReloadError;
		if (!UPackageTools::ReloadPackages({ Loaded }, ReloadError,
			EReloadPackagesInteractionMode::AssumePositive))
		{
			OutError = FString::Printf(
				TEXT("File restored on disk, but the in-editor reload failed: %s. "
					"Restart the editor (or reload the asset manually) to see the restored version."),
				*ReloadError.ToString());
			OutRestoredPackage = PackageName;
			return false;
		}
	}

	OutRestoredPackage = PackageName;
	UE_LOG(LogRevvyCheckpoint, Log, TEXT("Restored %s from checkpoint %s"), *PackageName, *BackupId);
	return true;
}

int32 FRevvyCheckpointService::PruneCheckpoints(double InMaxAgeDays, double InMaxTotalMegabytes)
{
	int32 RemovedEntries = 0;
	const FString Root = CheckpointRootDir();
	const FDateTime Cutoff = FDateTime::Now() - FTimespan::FromDays(InMaxAgeDays);
	const FString CutoffDay = DayDirName(Cutoff);

	TArray<FString> DayDirs = SortedDayDirs(Root);

	// Age first: a day directory sorts lexically, so name < cutoff-name == older.
	for (int32 Index = DayDirs.Num() - 1; Index >= 0; --Index)
	{
		const FString DirName = FPaths::GetCleanFilename(DayDirs[Index]);
		if (DirName < CutoffDay)
		{
			RemovedEntries += ReadManifest(DayDirs[Index]).Num();
			IFileManager::Get().DeleteDirectory(*DayDirs[Index], /*RequireExists=*/false, /*Tree=*/true);
			DayDirs.RemoveAt(Index);
		}
	}

	// Then size: drop oldest whole days until under budget.
	const int64 Budget = (int64)(InMaxTotalMegabytes * 1024.0 * 1024.0);
	int64 Total = 0;
	for (const FString& Dir : DayDirs)
	{
		Total += DirectorySizeBytes(Dir);
	}
	int32 DirIndex = 0;
	while (Total > Budget && DirIndex < DayDirs.Num() - 1) // never delete today's dir for size alone
	{
		Total -= DirectorySizeBytes(DayDirs[DirIndex]);
		RemovedEntries += ReadManifest(DayDirs[DirIndex]).Num();
		IFileManager::Get().DeleteDirectory(*DayDirs[DirIndex], false, true);
		++DirIndex;
	}
	return RemovedEntries;
}

TSharedPtr<FJsonObject> FRevvyCheckpointService::GetStatusJson() const
{
	int32 Entries = 0;
	int64 TotalBytes = 0;
	for (const FString& DayDir : SortedDayDirs(CheckpointRootDir()))
	{
		Entries += ReadManifest(DayDir).Num();
		TotalBytes += DirectorySizeBytes(DayDir);
	}

	TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
	Status->SetBoolField(TEXT("enabled"), bEnabled);
	Status->SetStringField(TEXT("scope_mode"),
		bRevvyToolScopeOnly ? TEXT("revvy_tools_only") : TEXT("all_editor_saves"));
	Status->SetStringField(TEXT("root_dir"), FPaths::ConvertRelativePathToFull(CheckpointRootDir()));
	Status->SetNumberField(TEXT("entries"), Entries);
	Status->SetNumberField(TEXT("total_bytes"), (double)TotalBytes);
	Status->SetNumberField(TEXT("max_age_days"), MaxAgeDays);
	Status->SetNumberField(TEXT("max_total_mb"), MaxTotalMB);
	return Status;
}
