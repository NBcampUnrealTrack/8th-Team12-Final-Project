// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/ULoreService.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyLore, Log, All);

namespace
{
	const TCHAR* ErrorCodeNotInstalled = TEXT("LORE_NOT_INSTALLED");
	const TCHAR* ErrorCodeNoWorkspace = TEXT("LORE_NO_WORKSPACE");
	const TCHAR* ErrorCodeBadArgs = TEXT("LORE_BAD_ARGS");

	FString LoreSerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString OutJson;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		FJsonSerializer::Serialize(Obj, Writer);
		return OutJson;
	}

	/** Quote a single CLI argument if needed (Windows-style double quotes). */
	FString QuoteArg(const FString& Arg)
	{
		if (Arg.Contains(TEXT(" ")) || Arg.Contains(TEXT("\"")))
		{
			return FString::Printf(TEXT("\"%s\""), *Arg.Replace(TEXT("\""), TEXT("\\\"")));
		}
		return Arg;
	}
}

FString ULoreService::FindLoreBinary()
{
#if PLATFORM_WINDOWS
	const TCHAR* BinaryName = TEXT("lore.exe");
#else
	const TCHAR* BinaryName = TEXT("lore");
#endif

	// PATH lookup: run `lore --version` via the platform search. FPlatformProcess::ExecProcess
	// resolves through PATH, so try the bare name first with a cheap probe.
	{
		int32 ExitCode = -1;
		FString ProbeOut, ProbeErr;
		if (FPlatformProcess::ExecProcess(BinaryName, TEXT("--version"), &ExitCode, &ProbeOut, &ProbeErr) && ExitCode == 0)
		{
			return FString(BinaryName);
		}
	}

	// Official installer default: %USERPROFILE%/bin/lore.exe
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		const FString Candidate = FPaths::Combine(UserProfile, TEXT("bin"), BinaryName);
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Candidate))
		{
			return Candidate;
		}
	}

	return FString();
}

FString ULoreService::FindWorkspaceRoot()
{
	// Same algorithm as the lore CLI: walk up looking for .lore/ (or legacy .urc/).
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	while (!Dir.IsEmpty())
	{
		if (PlatformFile.DirectoryExists(*FPaths::Combine(Dir, TEXT(".lore"))) ||
			PlatformFile.DirectoryExists(*FPaths::Combine(Dir, TEXT(".urc"))))
		{
			return Dir;
		}

		const FString Parent = FPaths::GetPath(Dir.TrimChar(TEXT('/')));
		if (Parent.IsEmpty() || Parent == Dir)
		{
			break;
		}
		Dir = Parent;
	}

	return FString();
}

bool ULoreService::RunLore(const FString& Args, FString& OutStdout, FString& OutStderr, int32& OutExitCode)
{
	const FString Binary = FindLoreBinary();
	if (Binary.IsEmpty())
	{
		OutStderr = TEXT("lore CLI not found on PATH or in %USERPROFILE%\\bin");
		OutExitCode = -1;
		return false;
	}

	const FString Root = FindWorkspaceRoot();
	if (Root.IsEmpty())
	{
		OutStderr = TEXT("Project is not inside a Lore workspace (no .lore/ directory found walking up from the project dir)");
		OutExitCode = -1;
		return false;
	}

	// -P disables the pager; --non-interactive prevents any prompt from hanging the editor.
	const FString FullArgs = FString::Printf(TEXT("--repository %s --non-interactive -P %s"), *QuoteArg(Root), *Args);
	UE_LOG(LogRevvyLore, Log, TEXT("Running: lore %s"), *FullArgs);

	OutExitCode = -1;
	const bool bLaunched = FPlatformProcess::ExecProcess(*Binary, *FullArgs, &OutExitCode, &OutStdout, &OutStderr);
	if (!bLaunched)
	{
		OutStderr = FString::Printf(TEXT("Failed to launch lore process: %s"), *Binary);
		return false;
	}
	return true;
}

FString ULoreService::MakeResultJson(bool bSuccess, int32 ExitCode, const FString& StdOut, const FString& StdErr, const FString& ErrorCode)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), bSuccess);
	Obj->SetNumberField(TEXT("exit_code"), ExitCode);
	Obj->SetStringField(TEXT("stdout"), StdOut);
	Obj->SetStringField(TEXT("stderr"), StdErr);
	if (!ErrorCode.IsEmpty())
	{
		Obj->SetStringField(TEXT("error_code"), ErrorCode);
	}
	return LoreSerializeJson(Obj);
}

FString ULoreService::GetWorkspaceInfo()
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();

	const FString Binary = FindLoreBinary();
	const bool bInstalled = !Binary.IsEmpty();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("installed"), bInstalled);

	FString Version;
	if (bInstalled)
	{
		int32 ExitCode = -1;
		FString VersionErr;
		FPlatformProcess::ExecProcess(*Binary, TEXT("--version"), &ExitCode, &Version, &VersionErr);
		Version.TrimStartAndEndInline();
	}
	Obj->SetStringField(TEXT("version"), Version);

	const FString Root = FindWorkspaceRoot();
	Obj->SetBoolField(TEXT("is_workspace"), !Root.IsEmpty());
	Obj->SetStringField(TEXT("workspace_root"), Root);

	return LoreSerializeJson(Obj);
}

FString ULoreService::Status()
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(TEXT("status --scan"), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::Diff(const FString& Paths)
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	const FString Args = Paths.IsEmpty() ? TEXT("diff") : FString::Printf(TEXT("diff %s"), *Paths);
	if (!RunLore(Args, StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::History(int32 Length)
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	const int32 Clamped = FMath::Clamp(Length, 1, 200);
	if (!RunLore(FString::Printf(TEXT("history --oneline %d"), Clamped), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::StageAll()
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(TEXT("stage --scan ."), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::Stage(const FString& Paths)
{
	if (Paths.TrimStartAndEnd().IsEmpty())
	{
		return MakeResultJson(false, -1, FString(), TEXT("Stage requires at least one path; use StageAll for everything"), ErrorCodeBadArgs);
	}
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(FString::Printf(TEXT("stage %s"), *Paths), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::Commit(const FString& Message)
{
	const FString Trimmed = Message.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return MakeResultJson(false, -1, FString(), TEXT("Commit requires a non-empty message"), ErrorCodeBadArgs);
	}
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(FString::Printf(TEXT("commit %s"), *QuoteArg(Trimmed)), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::Checkpoint(const FString& Message)
{
	const FString CheckpointMessage = Message.TrimStartAndEnd().IsEmpty()
		? TEXT("Revvy checkpoint")
		: Message.TrimStartAndEnd();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();

	// Stage everything found on disk
	const FString StageJson = StageAll();
	TSharedPtr<FJsonObject> StageObj;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StageJson);
		FJsonSerializer::Deserialize(Reader, StageObj);
	}
	if (StageObj.IsValid())
	{
		Obj->SetObjectField(TEXT("stage"), StageObj);
		if (!StageObj->GetBoolField(TEXT("success")))
		{
			Obj->SetBoolField(TEXT("success"), false);
			return LoreSerializeJson(Obj);
		}
	}

	// Commit; "nothing to commit" style failures become a skipped no-op, not an error
	const FString CommitJson = Commit(CheckpointMessage);
	TSharedPtr<FJsonObject> CommitObj;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CommitJson);
		FJsonSerializer::Deserialize(Reader, CommitObj);
	}
	if (CommitObj.IsValid())
	{
		Obj->SetObjectField(TEXT("commit"), CommitObj);
		const bool bCommitOk = CommitObj->GetBoolField(TEXT("success"));
		FString CombinedOutput = CommitObj->GetStringField(TEXT("stdout")) + CommitObj->GetStringField(TEXT("stderr"));
		const bool bNothingToCommit = !bCommitOk && CombinedOutput.Contains(TEXT("othing to commit"));
		Obj->SetBoolField(TEXT("success"), bCommitOk || bNothingToCommit);
		if (bNothingToCommit)
		{
			Obj->SetBoolField(TEXT("skipped"), true);
		}
	}
	else
	{
		Obj->SetBoolField(TEXT("success"), false);
	}

	return LoreSerializeJson(Obj);
}

FString ULoreService::Push()
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(TEXT("push"), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::Sync()
{
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(TEXT("sync"), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}

FString ULoreService::MarkDirty(const FString& Paths)
{
	if (Paths.TrimStartAndEnd().IsEmpty())
	{
		return MakeResultJson(false, -1, FString(), TEXT("MarkDirty requires at least one path"), ErrorCodeBadArgs);
	}
	FString StdOut, StdErr;
	int32 ExitCode = -1;
	if (!RunLore(FString::Printf(TEXT("dirty %s"), *Paths), StdOut, StdErr, ExitCode))
	{
		const bool bNoBinary = StdErr.Contains(TEXT("not found on PATH"));
		return MakeResultJson(false, ExitCode, StdOut, StdErr, bNoBinary ? ErrorCodeNotInstalled : ErrorCodeNoWorkspace);
	}
	return MakeResultJson(ExitCode == 0, ExitCode, StdOut, StdErr);
}
