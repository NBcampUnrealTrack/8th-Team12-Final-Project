// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UDevEnvironmentService.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyDevEnv, Log, All);

namespace
{
	// ---- active detached task (game-thread only; Python calls run on the game thread) ----
	FProcHandle GActiveProc;
	FString     GActiveTask;
	FString     GActiveLog;

	FString DevEnvironmentSerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString OutJson;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		FJsonSerializer::Serialize(Obj, Writer);
		return OutJson;
	}

	FString DevEnvironmentMakeErrorJson(const FString& Error)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Error);
		return DevEnvironmentSerializeJson(Obj);
	}

	/** "14.38.33130" -> {14,38,33130,0}; tolerant of 2..4 fields. */
	void ParseVersion(const FString& In, int32 Out[4])
	{
		Out[0] = Out[1] = Out[2] = Out[3] = 0;
		TArray<FString> Parts;
		In.ParseIntoArray(Parts, TEXT("."));
		for (int32 i = 0; i < Parts.Num() && i < 4; ++i)
		{
			Out[i] = FCString::Atoi(*Parts[i]);
		}
	}

	/** -1 / 0 / 1 */
	int32 CompareVersions(const FString& A, const FString& B)
	{
		int32 Va[4], Vb[4];
		ParseVersion(A, Va);
		ParseVersion(B, Vb);
		for (int32 i = 0; i < 4; ++i)
		{
			if (Va[i] < Vb[i]) { return -1; }
			if (Va[i] > Vb[i]) { return 1; }
		}
		return 0;
	}

	/** Inclusive range "lo-hi". */
	bool VersionInRange(const FString& V, const FString& Range)
	{
		FString Lo, Hi;
		if (!Range.Split(TEXT("-"), &Lo, &Hi))
		{
			return false;
		}
		return CompareVersions(V, Lo) >= 0 && CompareVersions(V, Hi) <= 0;
	}

	/** Immediate subdirectory names of Dir. */
	TArray<FString> ListSubdirectories(const FString& Dir)
	{
		TArray<FString> Out;
		IFileManager::Get().FindFiles(Out, *(Dir / TEXT("*")), /*Files*/ false, /*Directories*/ true);
		return Out;
	}

	/** Return the last N lines of Text. */
	FString TailLinesOf(const FString& Text, int32 N)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
		const int32 Start = FMath::Max(0, Lines.Num() - N);
		TArray<FString> Tail;
		for (int32 i = Start; i < Lines.Num(); ++i)
		{
			Tail.Add(Lines[i]);
		}
		return FString::Join(Tail, TEXT("\n"));
	}
}

void UDevEnvironmentService::LoadEngineRequirements(FString& OutMinMsvc, TArray<FString>& OutBannedRanges, FString& OutMinWinSdk)
{
	// UE 5.8 fallbacks (verified against Engine/Config/Windows/Windows_SDK.json).
	OutMinMsvc = TEXT("14.38.33130");
	OutBannedRanges = { TEXT("14.50.0-14.50.35722"), TEXT("14.44.0-14.44.35210"),
	                    TEXT("14.40.0-14.43.99999"), TEXT("14.39.0-14.39.99999") };
	OutMinWinSdk = TEXT("10.0.19041.0");

	const FString JsonPath = FPaths::Combine(FPaths::EngineConfigDir(), TEXT("Windows"), TEXT("Windows_SDK.json"));
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *JsonPath))
	{
		return;
	}

	// The file carries // comment lines; UE's JSON parser rejects them — strip first.
	TArray<FString> Lines;
	Raw.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
	FString Clean;
	for (const FString& Line : Lines)
	{
		if (!Line.TrimStart().StartsWith(TEXT("//")))
		{
			Clean += Line + TEXT("\n");
		}
	}

	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Clean);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		UE_LOG(LogRevvyDevEnv, Warning, TEXT("Failed to parse %s — using built-in UE 5.8 requirements"), *JsonPath);
		return;
	}

	FString Tmp;
	if (Json->TryGetStringField(TEXT("MinimumVisualCppVersion"), Tmp)) { OutMinMsvc = Tmp; }
	if (Json->TryGetStringField(TEXT("MinVersion"), Tmp)) { OutMinWinSdk = Tmp; }

	const TArray<TSharedPtr<FJsonValue>>* Banned = nullptr;
	if (Json->TryGetArrayField(TEXT("BannedVisualCppVersions"), Banned) && Banned)
	{
		OutBannedRanges.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Banned)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S))
			{
				OutBannedRanges.Add(S);
			}
		}
	}
}

FString UDevEnvironmentService::GetBuildBatPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::EngineDir(), TEXT("Build"), TEXT("BatchFiles"), TEXT("Build.bat")));
}

FString UDevEnvironmentService::GetBootstrapScriptPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Revvy"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	const FString Candidate = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(Plugin->GetBaseDir(), TEXT("SetupUnrealDev.bat")));
	return FPlatformFileManager::Get().GetPlatformFile().FileExists(*Candidate) ? Candidate : FString();
}

FString UDevEnvironmentService::CheckEnvironment()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	// ---- engine ----
	{
		TSharedRef<FJsonObject> Engine = MakeShared<FJsonObject>();
		Engine->SetStringField(TEXT("root"), FPaths::ConvertRelativePathToFull(FPaths::RootDir()));
		Engine->SetStringField(TEXT("version"), FEngineVersion::Current().ToString());
		Root->SetObjectField(TEXT("engine"), Engine);
	}

	// ---- project ----
	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const bool bHasSource = FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*(ProjectDir / TEXT("Source")));
	{
		TSharedRef<FJsonObject> Project = MakeShared<FJsonObject>();
		Project->SetStringField(TEXT("path"), ProjectFile);
		Project->SetStringField(TEXT("name"), FPaths::GetBaseFilename(ProjectFile));
		Project->SetBoolField(TEXT("has_source"), bHasSource);
		const FString Sln = ProjectDir / (FPaths::GetBaseFilename(ProjectFile) + TEXT(".sln"));
		Project->SetBoolField(TEXT("project_files_exist"),
			FPlatformFileManager::Get().GetPlatformFile().FileExists(*Sln));
		Root->SetObjectField(TEXT("project"), Project);
	}

	// ---- requirements (engine JSON = truth) ----
	FString MinMsvc, MinWinSdk;
	TArray<FString> BannedRanges;
	LoadEngineRequirements(MinMsvc, BannedRanges, MinWinSdk);
	{
		TSharedRef<FJsonObject> Req = MakeShared<FJsonObject>();
		Req->SetStringField(TEXT("min_msvc"), MinMsvc);
		TArray<TSharedPtr<FJsonValue>> BannedArr;
		for (const FString& B : BannedRanges) { BannedArr.Add(MakeShared<FJsonValueString>(B)); }
		Req->SetArrayField(TEXT("banned_msvc"), BannedArr);
		Req->SetStringField(TEXT("min_windows_sdk"), MinWinSdk);
		Root->SetObjectField(TEXT("requirements"), Req);
	}

	// ---- Visual Studio / MSVC toolchain (vswhere) ----
	{
		TSharedRef<FJsonObject> Toolchain = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Installations;
		TArray<TSharedPtr<FJsonValue>> ValidVersions;
		TArray<TSharedPtr<FJsonValue>> Rejected;

		const FString ProgramFilesX86 = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles(x86)"));
		const FString VsWhere = ProgramFilesX86 / TEXT("Microsoft Visual Studio/Installer/vswhere.exe");
		if (!ProgramFilesX86.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*VsWhere))
		{
			int32 ExitCode = -1;
			FString VsOut, VsErr;
			FPlatformProcess::ExecProcess(*VsWhere,
				TEXT("-products * -prerelease -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8"),
				&ExitCode, &VsOut, &VsErr);

			TArray<TSharedPtr<FJsonValue>> VsArray;
			const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(VsOut);
			if (ExitCode == 0 && FJsonSerializer::Deserialize(Reader, VsArray))
			{
				for (const TSharedPtr<FJsonValue>& VsVal : VsArray)
				{
					const TSharedPtr<FJsonObject>* VsObj = nullptr;
					if (!VsVal.IsValid() || !VsVal->TryGetObject(VsObj) || !VsObj)
					{
						continue;
					}
					FString InstallPath, DisplayName;
					(*VsObj)->TryGetStringField(TEXT("installationPath"), InstallPath);
					(*VsObj)->TryGetStringField(TEXT("displayName"), DisplayName);

					TSharedRef<FJsonObject> InstJson = MakeShared<FJsonObject>();
					InstJson->SetStringField(TEXT("product"), DisplayName);
					TArray<TSharedPtr<FJsonValue>> MsvcVersions;

					const FString MsvcRoot = InstallPath / TEXT("VC/Tools/MSVC");
					for (const FString& Ver : ListSubdirectories(MsvcRoot))
					{
						MsvcVersions.Add(MakeShared<FJsonValueString>(Ver));

						if (CompareVersions(Ver, MinMsvc) < 0)
						{
							TSharedRef<FJsonObject> Rej = MakeShared<FJsonObject>();
							Rej->SetStringField(TEXT("version"), Ver);
							Rej->SetStringField(TEXT("reason"), TEXT("below_minimum"));
							Rejected.Add(MakeShared<FJsonValueObject>(Rej));
							continue;
						}
						bool bBanned = false;
						for (const FString& Range : BannedRanges)
						{
							if (VersionInRange(Ver, Range)) { bBanned = true; break; }
						}
						if (bBanned)
						{
							TSharedRef<FJsonObject> Rej = MakeShared<FJsonObject>();
							Rej->SetStringField(TEXT("version"), Ver);
							Rej->SetStringField(TEXT("reason"), TEXT("banned_compiler_bug"));
							Rejected.Add(MakeShared<FJsonValueObject>(Rej));
							continue;
						}
						// cl.exe must actually exist.
						const FString Cl = MsvcRoot / Ver / TEXT("bin/Hostx64/x64/cl.exe");
						if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Cl))
						{
							ValidVersions.Add(MakeShared<FJsonValueString>(Ver));
						}
					}
					InstJson->SetArrayField(TEXT("msvc_versions"), MsvcVersions);
					Installations.Add(MakeShared<FJsonValueObject>(InstJson));
				}
			}
		}

		Toolchain->SetBoolField(TEXT("ok"), ValidVersions.Num() > 0);
		Toolchain->SetArrayField(TEXT("installations"), Installations);
		Toolchain->SetArrayField(TEXT("valid_versions"), ValidVersions);
		Toolchain->SetArrayField(TEXT("rejected"), Rejected);
		Root->SetObjectField(TEXT("toolchain"), Toolchain);
	}

	// ---- Windows SDK (default kit root scan; sufficient for diagnostics) ----
	{
		TSharedRef<FJsonObject> Sdk = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> SdkVersions;
		bool bSdkOk = false;

		const FString ProgramFilesX86 = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles(x86)"));
		const FString KitInclude = ProgramFilesX86 / TEXT("Windows Kits/10/Include");
		for (const FString& Ver : ListSubdirectories(KitInclude))
		{
			// A real SDK ships um/windows.h.
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*(KitInclude / Ver / TEXT("um/windows.h"))))
			{
				SdkVersions.Add(MakeShared<FJsonValueString>(Ver));
				if (CompareVersions(Ver, MinWinSdk) >= 0)
				{
					bSdkOk = true;
				}
			}
		}
		Sdk->SetBoolField(TEXT("ok"), bSdkOk);
		Sdk->SetArrayField(TEXT("versions"), SdkVersions);
		Root->SetObjectField(TEXT("windows_sdk"), Sdk);
	}

	Root->SetStringField(TEXT("bootstrap_script"), GetBootstrapScriptPath());
	return DevEnvironmentSerializeJson(Root);
}

bool UDevEnvironmentService::LaunchDetached(const FString& TaskName, const FString& Command, FString& OutLogFile, FString& OutError)
{
	if (GActiveProc.IsValid() && FPlatformProcess::IsProcRunning(GActiveProc))
	{
		OutError = FString::Printf(TEXT("Task '%s' is still running — poll get_setup_status() first"), *GActiveTask);
		return false;
	}
	if (GActiveProc.IsValid())
	{
		FPlatformProcess::CloseProc(GActiveProc);
		GActiveProc.Reset();
	}

	const FString LogDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Revvy/SetupLogs"));
	IFileManager::Get().MakeDirectory(*LogDir, /*Tree*/ true);
	if (!IFileManager::Get().DirectoryExists(*LogDir))
	{
		// Without this the child's `> log` redirect fails and it exits 1 with no evidence.
		OutError = FString::Printf(TEXT("Cannot create log directory '%s'"), *LogDir);
		return false;
	}

	// Prune logs older than 14 days — this dir sits outside UE's Saved/Logs rotation.
	{
		TArray<FString> OldLogs;
		IFileManager::Get().FindFiles(OldLogs, *(LogDir / TEXT("*.log")), /*Files*/ true, /*Dirs*/ false);
		const FDateTime Cutoff = FDateTime::UtcNow() - FTimespan::FromDays(14);
		for (const FString& Name : OldLogs)
		{
			const FString FullPath = LogDir / Name;
			if (IFileManager::Get().GetTimeStamp(*FullPath) < Cutoff)
			{
				IFileManager::Get().Delete(*FullPath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
			}
		}
	}

	// Session counter keeps same-second retries from truncating the previous log.
	static int32 LaunchCounter = 0;
	OutLogFile = LogDir / FString::Printf(TEXT("%s-%s-%d.log"), *TaskName,
		*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), ++LaunchCounter);

	// cmd /s /c "<command> > <log> 2>&1" — /s keeps the outer quote pair intact.
	const FString CmdParams = FString::Printf(TEXT("/s /c \"%s > \"%s\" 2>&1\""), *Command, *OutLogFile);
	UE_LOG(LogRevvyDevEnv, Log, TEXT("Launching %s: cmd %s"), *TaskName, *CmdParams);

	uint32 ProcId = 0;
	GActiveProc = FPlatformProcess::CreateProc(TEXT("cmd.exe"), *CmdParams,
		/*bLaunchDetached*/ true, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
		&ProcId, /*Priority*/ 0, /*WorkingDir*/ nullptr, /*PipeWrite*/ nullptr);
	if (!GActiveProc.IsValid())
	{
		OutError = TEXT("Failed to launch cmd.exe");
		return false;
	}
	GActiveTask = TaskName;
	GActiveLog = OutLogFile;
	return true;
}

FString UDevEnvironmentService::GenerateProjectFiles()
{
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*(ProjectDir / TEXT("Source"))))
	{
		return DevEnvironmentMakeErrorJson(TEXT("Content-only project (no Source folder) — project file generation does not apply"));
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString Command = FString::Printf(TEXT("\"%s\" -projectfiles -project=\"%s\" -game -waitmutex"),
		*GetBuildBatPath(), *ProjectFile);

	FString LogFile, Error;
	if (!LaunchDetached(TEXT("genproj"), Command, LogFile, Error))
	{
		return DevEnvironmentMakeErrorJson(Error);
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("started"), true);
	Obj->SetStringField(TEXT("task"), TEXT("genproj"));
	Obj->SetStringField(TEXT("log_file"), LogFile);
	return DevEnvironmentSerializeJson(Obj);
}

FString UDevEnvironmentService::BuildEditorTarget(const FString& Configuration)
{
	static const TArray<FString> AllowedConfigs = { TEXT("Development"), TEXT("DebugGame"), TEXT("Shipping") };
	if (!AllowedConfigs.Contains(Configuration))
	{
		return DevEnvironmentMakeErrorJson(FString::Printf(TEXT("Invalid configuration '%s' (allowed: Development, DebugGame, Shipping)"), *Configuration));
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString ProjectName = FPaths::GetBaseFilename(ProjectFile);

	// Target: Source\*Editor.Target.cs basename, else <Project>Editor, else UnrealEditor (content-only).
	FString TargetName;
	if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*(ProjectDir / TEXT("Source"))))
	{
		TArray<FString> TargetFiles;
		IFileManager::Get().FindFiles(TargetFiles, *(ProjectDir / TEXT("Source") / TEXT("*Editor.Target.cs")), /*Files*/ true, /*Dirs*/ false);
		if (TargetFiles.Num() > 0)
		{
			TargetName = TargetFiles[0].LeftChop(10); // strip ".Target.cs"
		}
		else
		{
			TargetName = ProjectName + TEXT("Editor");
		}
	}
	else
	{
		TargetName = TEXT("UnrealEditor");
	}

	// UBT target names are C# identifiers. Anything else means the glob matched a
	// stray/backup Target.cs — reject instead of splicing a broken cmd line.
	for (const TCHAR C : TargetName)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
		{
			return DevEnvironmentMakeErrorJson(FString::Printf(
				TEXT("Invalid target name '%s' — remove stray/backup *Editor.Target.cs files from Source"), *TargetName));
		}
	}

	const FString Command = FString::Printf(TEXT("\"%s\" %s Win64 %s -project=\"%s\" -waitmutex"),
		*GetBuildBatPath(), *TargetName, *Configuration, *ProjectFile);

	FString LogFile, Error;
	if (!LaunchDetached(TEXT("build"), Command, LogFile, Error))
	{
		return DevEnvironmentMakeErrorJson(Error);
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("started"), true);
	Obj->SetStringField(TEXT("task"), TEXT("build"));
	Obj->SetStringField(TEXT("target"), TargetName);
	Obj->SetStringField(TEXT("log_file"), LogFile);
	return DevEnvironmentSerializeJson(Obj);
}

FString UDevEnvironmentService::GetSetupStatus(int32 TailLines)
{
	TailLines = FMath::Clamp(TailLines, 0, 400);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("task"), GActiveTask);

	bool bRunning = false;
	int32 ExitCode = -1;
	if (GActiveProc.IsValid())
	{
		bRunning = FPlatformProcess::IsProcRunning(GActiveProc);
		if (!bRunning)
		{
			int32 Code = -1;
			if (FPlatformProcess::GetProcReturnCode(GActiveProc, &Code))
			{
				ExitCode = Code;
			}
		}
	}
	Obj->SetBoolField(TEXT("running"), bRunning);
	Obj->SetNumberField(TEXT("exit_code"), ExitCode);
	Obj->SetStringField(TEXT("log_file"), GActiveLog);

	FString LogTail;
	bool bLogReadable = false;
	if (!GActiveLog.IsEmpty() && TailLines > 0)
	{
		FString LogContent;
		// FILEREAD_AllowWrite adds FILE_SHARE_WRITE — without it the open fails with a
		// sharing violation while cmd/UBT still hold the redirected log for writing,
		// and log_tail would stay empty for the task's whole runtime.
		if (FFileHelper::LoadFileToString(LogContent, *GActiveLog, FFileHelper::EHashOptions::None,
				FILEREAD_AllowWrite | FILEREAD_Silent))
		{
			LogTail = TailLinesOf(LogContent, TailLines);
			bLogReadable = true;
		}
	}
	Obj->SetStringField(TEXT("log_tail"), LogTail);
	Obj->SetBoolField(TEXT("log_readable"), bLogReadable);
	return DevEnvironmentSerializeJson(Obj);
}

FString UDevEnvironmentService::RunSetupBootstrap(bool bCheckOnly)
{
	const FString Script = GetBootstrapScriptPath();
	if (Script.IsEmpty())
	{
		return DevEnvironmentMakeErrorJson(TEXT("SetupUnrealDev.bat not found in the Revvy plugin folder"));
	}

	// Consent gate: a full bootstrap can install VS Build Tools (UAC prompt,
	// 10-30 min). An AI/MCP call must not start that on its own — the artist
	// confirms in-editor first. Check-only runs are harmless and skip the gate.
	if (!bCheckOnly)
	{
		if (FApp::IsUnattended())
		{
			return DevEnvironmentMakeErrorJson(TEXT("user_confirmation_required — full bootstrap cannot run unattended (use bCheckOnly=true)"));
		}
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo,
			FText::FromString(TEXT("레비가 개발 환경 셋업(SetupUnrealDev.bat)을 실행하려고 해요.\n\n"
				"콘솔 창이 열리고, Visual Studio Build Tools가 없으면 관리자 승인(UAC) 1회와 함께 "
				"10~30분 정도 설치가 진행돼요.\n\n계속할까요?")));
		if (Answer != EAppReturnType::Yes)
		{
			return DevEnvironmentMakeErrorJson(TEXT("user_declined"));
		}
	}

	// Visible console via `cmd /c start` so the artist sees progress and the UAC
	// prompt has an obvious owner. The batch file pauses at the end.
	const FString Args = bCheckOnly ? TEXT(" -CheckOnly") : TEXT("");
	const FString CmdParams = FString::Printf(TEXT("/c start \"Revvy Setup\" \"%s\"%s"), *Script, *Args);

	uint32 ProcId = 0;
	FProcHandle Handle = FPlatformProcess::CreateProc(TEXT("cmd.exe"), *CmdParams,
		/*bLaunchDetached*/ true, /*bLaunchHidden*/ false, /*bLaunchReallyHidden*/ false,
		&ProcId, /*Priority*/ 0, /*WorkingDir*/ nullptr, /*PipeWrite*/ nullptr);
	if (!Handle.IsValid())
	{
		return DevEnvironmentMakeErrorJson(TEXT("Failed to launch the setup console"));
	}
	FPlatformProcess::CloseProc(Handle);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("started"), true);
	Obj->SetStringField(TEXT("script"), Script);
	return DevEnvironmentSerializeJson(Obj);
}
