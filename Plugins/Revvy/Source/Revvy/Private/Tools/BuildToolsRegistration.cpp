// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// build_project - UnrealBuildTool compile verification exposed as an MCP tool.
//
// WHY THIS IS ASYNC
// FToolRegistry::ExecuteTool runs on the game thread (MCPServer marshals every
// tools/call there via AsyncTask(ENamedThreads::GameThread) and blocks the socket
// thread on the result). A UBT run takes tens of seconds to minutes, so running it
// inline would hard-freeze the editor for the whole build. Instead `compile` spawns
// UBT with pipes and returns immediately; a core-ticker pump drains stdout/stderr
// one frame at a time. Callers poll `status` and then read `last_result`.
//

#include "Core/ToolRegistry.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/UniquePtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildTool, Log, All);

namespace RevvyBuildTool
{

// ---------------------------------------------------------------------------
// Parameter extraction (same shape as read_logs / manage_asset)
// ---------------------------------------------------------------------------

static FString ExtractParam(const TMap<FString, FString>& Params, const FString& FieldName)
{
	// Direct hit first.
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

	// Remaining arguments are folded into a ParamsJson blob by the same transform.
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

static FString ExtractParamWithDefault(const TMap<FString, FString>& Params, const FString& FieldName, const FString& DefaultValue)
{
	const FString Value = ExtractParam(Params, FieldName);
	return Value.IsEmpty() ? DefaultValue : Value;
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
// Structured diagnostics
// ---------------------------------------------------------------------------

/** One parsed compiler / linker / UBT / UHT diagnostic. */
struct FBuildDiagnostic
{
	/** "error", "warning" or "note". */
	FString Severity;

	/** Absolute or compiler-relative source path. Empty for toolchain-level messages. */
	FString File;

	/** 1-based line number, 0 when the diagnostic has no location. */
	int32 Line = 0;

	/** 1-based column, 0 when the compiler did not report one. */
	int32 Column = 0;

	/** Diagnostic code: C2065, LNK2019, UHT, UBT, ... Empty when the tool emitted none. */
	FString Code;

	/** Human-readable message with the location prefix stripped. */
	FString Message;

	/** The untouched output line, so the agent can see anything the parser dropped. */
	FString RawSnippet;

	/** Heuristic repair suggestion. Empty when nothing useful is known. */
	FString FixHint;
};

/**
 * Compiled once and reused for the whole build. UBT emits thousands of lines and
 * rebuilding ICU patterns per line is the single most expensive thing this pump
 * could do on the game thread.
 */
struct FDiagnosticPatterns
{
	/** MSVC / UHT:  Foo.cpp(12): error C2065: ...   |   Foo.cpp(12,7): warning C4996: ...   |   Foo.h(9): Error: ... */
	FRegexPattern Msvc;

	/** Clang (Mac/Linux, clang-cl):  /a/Foo.cpp:12:7: error: ... */
	FRegexPattern Clang;

	/** Linker:  Foo.cpp.obj : error LNK2019: ...   |   LINK : fatal error LNK1120: ... */
	FRegexPattern Linker;

	/** UBT / UAT:  ERROR: ...   |   WARNING: ... */
	FRegexPattern UbtPrefixed;

	/** MSBuild-style fallback:  EXEC : error : ... */
	FRegexPattern GenericSeparated;

	/** UBT action progress:  [12/57] Compile Foo.cpp */
	FRegexPattern ActionProgress;

	FDiagnosticPatterns()
		: Msvc(
			TEXT("^\\s*(.+?)\\((\\d+)(?:,\\s*(\\d+))?\\)\\s*:\\s*(?:fatal\\s+)?(error|warning|note)\\s*([A-Za-z]{1,6}[0-9]{4,5})?\\s*:\\s*(.+)$"),
			ERegexPatternFlags::CaseInsensitive)
		, Clang(
			TEXT("^\\s*(.+?):(\\d+):(\\d+):\\s*(?:fatal\\s+)?(error|warning|note)\\s*:\\s*(.+)$"),
			ERegexPatternFlags::CaseInsensitive)
		, Linker(
			TEXT("^\\s*(.*?)\\s*:\\s*(?:fatal\\s+)?(error|warning)\\s+(LNK[0-9]+|C[0-9]+)\\s*:\\s*(.+)$"),
			ERegexPatternFlags::CaseInsensitive)
		, UbtPrefixed(
			TEXT("^\\s*(ERROR|WARNING)\\s*:\\s*(.+)$"),
			ERegexPatternFlags::CaseInsensitive)
		, GenericSeparated(
			TEXT("^\\s*(\\S[^:]*?)\\s+:\\s*(?:fatal\\s+)?(error|warning)\\s*:\\s*(.+)$"),
			ERegexPatternFlags::CaseInsensitive)
		, ActionProgress(TEXT("^\\s*\\[([0-9]+)/([0-9]+)\\]\\s*(.+)$"))
	{
	}
};

static FDiagnosticPatterns& GetPatterns()
{
	static FDiagnosticPatterns Patterns;
	return Patterns;
}

/** Normalize "Error"/"FATAL ERROR"/"Warning" to the lower-case severity we report. */
static FString NormalizeSeverity(const FString& Raw)
{
	const FString Lower = Raw.TrimStartAndEnd().ToLower();
	if (Lower.StartsWith(TEXT("warn")))
	{
		return TEXT("warning");
	}
	if (Lower.StartsWith(TEXT("note")))
	{
		return TEXT("note");
	}
	return TEXT("error");
}

/**
 * Pull the replacement symbol out of a UE deprecation message.
 * MSVC C4996 renders as: 'UOld::Fn': Use UNew::Fn instead. Please update your code...
 */
static FString ExtractDeprecationReplacement(const FString& Message)
{
	const int32 UseIdx = Message.Find(TEXT("use "), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0);
	if (UseIdx == INDEX_NONE)
	{
		return FString();
	}

	FString Suggestion = Message.Mid(UseIdx + 4);
	const int32 InsteadIdx = Suggestion.Find(TEXT(" instead"), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0);
	if (InsteadIdx != INDEX_NONE)
	{
		Suggestion = Suggestion.Left(InsteadIdx);
	}
	else
	{
		// No " instead" anchor - take the rest of the sentence only.
		const int32 StopIdx = Suggestion.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
		if (StopIdx != INDEX_NONE)
		{
			Suggestion = Suggestion.Left(StopIdx);
		}
	}

	Suggestion.TrimStartAndEndInline();
	Suggestion.RemoveFromStart(TEXT("'"));
	Suggestion.RemoveFromEnd(TEXT("'"));
	Suggestion.TrimStartAndEndInline();

	// Anything long is prose, not a symbol - better to say nothing than to mislead.
	if (Suggestion.IsEmpty() || Suggestion.Len() > 90)
	{
		return FString();
	}
	return Suggestion;
}

/**
 * Attach a short repair hint for the UE C++ mistakes an agent actually makes.
 * Code-keyed first (precise), message-keyed second (covers clang/gcc/UHT wording).
 */
static FString MakeFixHint(const FBuildDiagnostic& Diagnostic)
{
	const FString Code = Diagnostic.Code.ToUpper();
	const FString& Message = Diagnostic.Message;

	// --- MSVC compiler codes -------------------------------------------------
	if (Code == TEXT("C2065") || Code == TEXT("C3861"))
	{
		return TEXT("Unknown identifier. Most often a missing #include for the header that declares it. ")
			TEXT("If the header exists but the symbol still will not resolve, the owning module is not in ")
			TEXT("Revvy.Build.cs (Public/PrivateDependencyModuleNames).");
	}
	if (Code == TEXT("C2027") || Code == TEXT("C2504") || Code == TEXT("C2079"))
	{
		return TEXT("Type is only forward-declared in this translation unit. Add the full #include for it ")
			TEXT("(a forward declaration is not enough to dereference, derive from, or size the type).");
	}
	if (Code == TEXT("C1083"))
	{
		return TEXT("Header not on the include path. Check the spelling/casing of the include, and add the ")
			TEXT("owning module to Revvy.Build.cs so UBT adds its Public include path.");
	}
	if (Code == TEXT("C2039"))
	{
		return TEXT("That member does not exist on the type in UE 5.8. It was most likely renamed or moved to ")
			TEXT("a subsystem/library class - check the engine header before guessing.");
	}
	if (Code == TEXT("C2143") || Code == TEXT("C2059") || Code == TEXT("C2061") || Code == TEXT("C4430"))
	{
		return TEXT("Syntax error. In UE code this is usually a missing semicolon after a UCLASS/USTRUCT body, ")
			TEXT("a missing GENERATED_BODY(), or a type that needs its header included above this line.");
	}
	if (Code == TEXT("C2440") || Code == TEXT("C2664") || Code == TEXT("C2666"))
	{
		return TEXT("Argument/type mismatch against the UE 5.8 signature. Watch for TObjectPtr<T> vs T* ")
			TEXT("(call .Get()), FString vs FName vs FText, and const-ness of the parameter.");
	}
	if (Code == TEXT("C2338"))
	{
		return TEXT("static_assert fired - read the assert text; UE uses it to reject unsupported template ")
			TEXT("arguments (e.g. a non-UObject in TObjectPtr / TSubclassOf).");
	}
	if (Code == TEXT("C4996"))
	{
		const FString Replacement = ExtractDeprecationReplacement(Message);
		if (!Replacement.IsEmpty())
		{
			return FString::Printf(
				TEXT("Deprecated API. The engine suggests: %s. Update the call site rather than suppressing the warning."),
				*Replacement);
		}
		return TEXT("Deprecated API. The engine's deprecation text names the replacement - follow it; ")
			TEXT("deprecated symbols are removed in a later release.");
	}

	// --- Linker --------------------------------------------------------------
	if (Code == TEXT("LNK2019") || Code == TEXT("LNK2001"))
	{
		return TEXT("Unresolved external symbol: the declaration was found but nothing links it. Usually the ")
			TEXT("module that defines the symbol is missing from Revvy.Build.cs ")
			TEXT("(Public/PrivateDependencyModuleNames), or the symbol is editor-only and this target is not ")
			TEXT("an editor target, or a declared method has no definition.");
	}
	if (Code == TEXT("LNK1120"))
	{
		return TEXT("Summary of the unresolved externals above - fix the LNK2019 entries, not this line.");
	}
	if (Code == TEXT("LNK1168") || Code == TEXT("LNK1104"))
	{
		return TEXT("Output binary is locked, almost always because the running editor has the DLL loaded. ")
			TEXT("Run with hot_reload=true (the default) so UBT emits a versioned hot-reload DLL, or close the editor.");
	}
	if (Code.StartsWith(TEXT("LNK")))
	{
		return TEXT("Link stage failure. Check module dependencies in Revvy.Build.cs and that every declared ")
			TEXT("function has a definition.");
	}

	// --- Message-keyed fallbacks (clang/gcc/UHT/UBT wording) -----------------
	if (Message.Contains(TEXT("undeclared identifier"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("was not declared in this scope"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("use of undeclared"), ESearchCase::IgnoreCase))
	{
		return TEXT("Unknown identifier - add the #include that declares it, or add its module to Revvy.Build.cs.");
	}
	if (Message.Contains(TEXT("unresolved external"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("undefined reference"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("undefined symbol"), ESearchCase::IgnoreCase))
	{
		return TEXT("Nothing links this symbol - add the defining module to Revvy.Build.cs, or supply the missing definition.");
	}
	if (Message.Contains(TEXT("no member named"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("is not a member of"), ESearchCase::IgnoreCase))
	{
		return TEXT("Member does not exist on that type in UE 5.8 - verify against the engine header; the API was likely renamed.");
	}
	if (Message.Contains(TEXT("incomplete type"), ESearchCase::IgnoreCase))
	{
		return TEXT("Incomplete type - the header is forward-declared only. Include the real header.");
	}
	if (Message.Contains(TEXT("deprecat"), ESearchCase::IgnoreCase))
	{
		const FString Replacement = ExtractDeprecationReplacement(Message);
		if (!Replacement.IsEmpty())
		{
			return FString::Printf(TEXT("Deprecated API. Suggested replacement: %s."), *Replacement);
		}
		return TEXT("Deprecated API - migrate to the replacement named in the message.");
	}
	if (Message.Contains(TEXT("Unrecognized type"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("must be a UCLASS"), ESearchCase::IgnoreCase))
	{
		return TEXT("UnrealHeaderTool cannot resolve this type. It must be a UCLASS/USTRUCT/UENUM, and its ")
			TEXT("header must be #included in THIS header (not only in the .cpp) so UHT can see it.");
	}
	if (Message.Contains(TEXT("GENERATED_BODY"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("GENERATED_UCLASS_BODY"), ESearchCase::IgnoreCase))
	{
		return TEXT("UHT requires GENERATED_BODY() as the first line inside the UCLASS/USTRUCT body, and the ")
			TEXT("matching \"<Name>.generated.h\" as the LAST include in the header.");
	}
	if (Message.Contains(TEXT("Could not find definition for module"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("Unable to find module"), ESearchCase::IgnoreCase))
	{
		return TEXT("A module named in Build.cs does not exist for this target - check the spelling, and enable ")
			TEXT("the owning plugin in the .uproject if it ships as a plugin module.");
	}
	if (Message.Contains(TEXT("Expecting to find a type to be declared in a module rules named"), ESearchCase::IgnoreCase))
	{
		return TEXT("Build.cs class name must match the file name: <Module>.Build.cs must declare `public class <Module> : ModuleRules`.");
	}
	if (Message.Contains(TEXT("being used by a running instance"), ESearchCase::IgnoreCase)
		|| Message.Contains(TEXT("Access is denied"), ESearchCase::IgnoreCase))
	{
		return TEXT("A file is locked by the running editor. Keep hot_reload=true so UBT patches into a versioned DLL.");
	}

	return FString();
}

/** Parse one raw output line. Returns false when the line is not a diagnostic. */
static bool ParseDiagnosticLine(const FString& Line, FBuildDiagnostic& Out)
{
	if (Line.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}

	FDiagnosticPatterns& Patterns = GetPatterns();

	// 1) MSVC / UHT - "File(Line[,Col]): severity [Code]: message"
	{
		FRegexMatcher Matcher(Patterns.Msvc, Line);
		if (Matcher.FindNext())
		{
			Out.File = Matcher.GetCaptureGroup(1).TrimStartAndEnd();
			Out.Line = FCString::Atoi(*Matcher.GetCaptureGroup(2));
			Out.Column = FCString::Atoi(*Matcher.GetCaptureGroup(3));
			Out.Severity = NormalizeSeverity(Matcher.GetCaptureGroup(4));
			Out.Code = Matcher.GetCaptureGroup(5).TrimStartAndEnd();
			Out.Message = Matcher.GetCaptureGroup(6).TrimStartAndEnd();
			Out.RawSnippet = Line;
			if (Out.Code.IsEmpty())
			{
				// UHT reports "File(12): Error: ..." with no code of its own.
				Out.Code = TEXT("UHT");
			}
			return true;
		}
	}

	// 2) Clang - "/path/File.cpp:12:7: error: message"
	{
		FRegexMatcher Matcher(Patterns.Clang, Line);
		if (Matcher.FindNext())
		{
			Out.File = Matcher.GetCaptureGroup(1).TrimStartAndEnd();
			Out.Line = FCString::Atoi(*Matcher.GetCaptureGroup(2));
			Out.Column = FCString::Atoi(*Matcher.GetCaptureGroup(3));
			Out.Severity = NormalizeSeverity(Matcher.GetCaptureGroup(4));
			Out.Message = Matcher.GetCaptureGroup(5).TrimStartAndEnd();
			Out.RawSnippet = Line;
			return true;
		}
	}

	// 3) Linker - "Foo.cpp.obj : error LNK2019: ..." (no line number available)
	{
		FRegexMatcher Matcher(Patterns.Linker, Line);
		if (Matcher.FindNext())
		{
			Out.File = Matcher.GetCaptureGroup(1).TrimStartAndEnd();
			Out.Line = 0;
			Out.Column = 0;
			Out.Severity = NormalizeSeverity(Matcher.GetCaptureGroup(2));
			Out.Code = Matcher.GetCaptureGroup(3).TrimStartAndEnd().ToUpper();
			Out.Message = Matcher.GetCaptureGroup(4).TrimStartAndEnd();
			Out.RawSnippet = Line;
			return true;
		}
	}

	// 4) UBT / UAT - "ERROR: ..." / "WARNING: ..."
	{
		FRegexMatcher Matcher(Patterns.UbtPrefixed, Line);
		if (Matcher.FindNext())
		{
			Out.File.Empty();
			Out.Line = 0;
			Out.Column = 0;
			Out.Severity = NormalizeSeverity(Matcher.GetCaptureGroup(1));
			Out.Code = TEXT("UBT");
			Out.Message = Matcher.GetCaptureGroup(2).TrimStartAndEnd();
			Out.RawSnippet = Line;
			return true;
		}
	}

	// 5) MSBuild-ish fallback - "EXEC : error : ..."
	{
		FRegexMatcher Matcher(Patterns.GenericSeparated, Line);
		if (Matcher.FindNext())
		{
			Out.File = Matcher.GetCaptureGroup(1).TrimStartAndEnd();
			Out.Line = 0;
			Out.Column = 0;
			Out.Severity = NormalizeSeverity(Matcher.GetCaptureGroup(2));
			Out.Code = TEXT("UBT");
			Out.Message = Matcher.GetCaptureGroup(3).TrimStartAndEnd();
			Out.RawSnippet = Line;
			return true;
		}
	}

	return false;
}

static TSharedPtr<FJsonObject> DiagnosticToJson(const FBuildDiagnostic& Diagnostic)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("severity"), Diagnostic.Severity);
	Obj->SetStringField(TEXT("file"), Diagnostic.File);
	Obj->SetNumberField(TEXT("line"), Diagnostic.Line);
	Obj->SetNumberField(TEXT("column"), Diagnostic.Column);
	Obj->SetStringField(TEXT("code"), Diagnostic.Code);
	Obj->SetStringField(TEXT("message"), Diagnostic.Message);
	Obj->SetStringField(TEXT("raw_snippet"), Diagnostic.RawSnippet);
	if (!Diagnostic.FixHint.IsEmpty())
	{
		Obj->SetStringField(TEXT("fix_hint"), Diagnostic.FixHint);
	}
	return Obj;
}

// ---------------------------------------------------------------------------
// Target / toolchain discovery
// ---------------------------------------------------------------------------

/** Absolute path to the loaded .uproject, or empty when the editor has none. */
static FString GetProjectFileFullPath()
{
	if (!FPaths::IsProjectFilePathSet())
	{
		return FString();
	}
	const FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(ProjectPath);
}

/**
 * Scan <Project>/Source for a *.Target.cs declaring TargetType.Editor - the same
 * detection BuildPlugin.bat performs, so both paths agree on the target name.
 * Prefers "<ProjectName>Editor" when several editor targets exist.
 */
static FString AutoDetectEditorTarget()
{
	const FString SourceDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source"));
	const FString Preferred = FString(FApp::GetProjectName()) + TEXT("Editor");

	TArray<FString> TargetFiles;
	IFileManager::Get().FindFilesRecursive(TargetFiles, *SourceDir, TEXT("*.Target.cs"), true, false);

	FString FirstMatch;
	for (const FString& TargetFile : TargetFiles)
	{
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *TargetFile))
		{
			continue;
		}
		if (!Contents.Contains(TEXT("TargetType.Editor")))
		{
			continue;
		}

		FString TargetName = FPaths::GetCleanFilename(TargetFile);
		TargetName.RemoveFromEnd(TEXT(".Target.cs"));
		if (TargetName.Equals(Preferred, ESearchCase::IgnoreCase))
		{
			return TargetName;
		}
		if (FirstMatch.IsEmpty())
		{
			FirstMatch = TargetName;
		}
	}

	if (!FirstMatch.IsEmpty())
	{
		return FirstMatch;
	}
	// No Source/ folder (Blueprint-only project with plugin sources) - fall back to convention.
	return Preferred;
}

static FString GetDefaultPlatformName()
{
#if PLATFORM_WINDOWS
	return TEXT("Win64");
#elif PLATFORM_MAC
	return TEXT("Mac");
#else
	return TEXT("Linux");
#endif
}

/** Absolute path to the engine's Build batch/shell script for this host. */
static FString GetEngineBuildScriptPath()
{
#if PLATFORM_WINDOWS
	const FString Relative = FPaths::EngineDir() / TEXT("Build/BatchFiles/Build.bat");
#elif PLATFORM_MAC
	const FString Relative = FPaths::EngineDir() / TEXT("Build/BatchFiles/Mac/Build.sh");
#else
	const FString Relative = FPaths::EngineDir() / TEXT("Build/BatchFiles/Linux/Build.sh");
#endif
	return FPaths::ConvertRelativePathToFull(Relative);
}

static FString GetBuildLogDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("Build"));
}

static FString GetLastResultPath()
{
	return GetBuildLogDir() / TEXT("last_result.json");
}

/**
 * Target and platform names end up both on a `cmd /c` command line and in a log
 * file name, so anything outside an identifier is rejected rather than escaped.
 * (extra_args is deliberately exempt - it exists to pass UBT switches verbatim.)
 */
static FString SanitizeToken(const FString& Value)
{
	FString Result;
	Result.Reserve(Value.Len());
	for (const TCHAR Ch : Value)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT('.') || Ch == TEXT('+'))
		{
			Result.AppendChar(Ch);
		}
	}
	return Result;
}

static bool IsKnownConfiguration(const FString& Configuration)
{
	static const TCHAR* Known[] = { TEXT("Debug"), TEXT("DebugGame"), TEXT("Development"), TEXT("Shipping"), TEXT("Test") };
	for (const TCHAR* Candidate : Known)
	{
		if (Configuration.Equals(Candidate, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// The build job
// ---------------------------------------------------------------------------

/** Everything the caller can steer about one UBT invocation. */
struct FBuildRequest
{
	FString Target;
	FString Platform;
	FString Configuration = TEXT("Development");
	FString ExtraArgs;
	bool bHotReload = true;
	double TimeoutSeconds = 900.0;
};

/**
 * One-at-a-time UBT runner. Lives for the process lifetime; owns the child
 * process, the pipes, the core-ticker pump and the last parsed result.
 * Game thread only - every entry point is reached from ExecuteTool or the ticker.
 */
class FBuildJob
{
public:
	static FBuildJob& Get()
	{
		static FBuildJob Instance;
		return Instance;
	}

	bool IsRunning() const { return bRunning; }

	/** Spawn UBT. Returns false (with OutError set) when the build could not start. */
	bool Start(const FBuildRequest& InRequest, FString& OutError);

	/** Terminate the running build and finalize its result as "cancelled". */
	bool Cancel(FString& OutError);

	/** Live progress for a running build; idle summary otherwise. */
	TSharedPtr<FJsonObject> BuildStatusJson() const;

	/** Last completed build, loaded from disk if this editor session has none in memory. */
	TSharedPtr<FJsonObject> GetLastResult();

	const FString& GetLogPath() const { return LogFilePath; }
	const FString& GetBuildId() const { return BuildId; }
	const FString& GetCommandLine() const { return CommandLine; }
	const FBuildRequest& GetRequest() const { return Request; }

private:
	FBuildJob() = default;
	~FBuildJob();

	FBuildJob(const FBuildJob&) = delete;
	FBuildJob& operator=(const FBuildJob&) = delete;

	bool Tick(float DeltaTime);
	void HandleEnginePreExit();
	void DrainPipes();
	void ProcessBufferedLines(bool bFlushIncomplete);
	void HandleLine(const FString& Line);
	void AppendToLogFile(const FString& Line);
	void Finish(const FString& InStatus);
	void CloseProcessAndPipes();
	void ResetRunState();
	TSharedPtr<FJsonObject> ComposeResultJson(const FString& InStatus) const;

	// --- process ---
	FProcHandle ProcessHandle;
	void* StdOutRead = nullptr;
	void* StdOutWrite = nullptr;
	void* StdErrRead = nullptr;
	void* StdErrWrite = nullptr;

	// --- pump ---
	FTSTicker::FDelegateHandle TickerHandle;
	TArray<uint8> OutputBuffer;
	bool bRunning = false;
	bool bInTick = false;
	bool bCancelRequested = false;
	bool bShutdownHookInstalled = false;
	double StartSeconds = 0.0;
	double DurationSeconds = 0.0;

	// --- request / identity ---
	FBuildRequest Request;
	FString BuildId;
	FString LogFilePath;
	FString CommandLine;
	TUniquePtr<FArchive> LogWriter;

	// --- parsed output ---
	TArray<FBuildDiagnostic> Errors;
	TArray<FBuildDiagnostic> Warnings;
	TArray<FBuildDiagnostic> Notes;
	TSet<FString> SeenDiagnosticKeys;
	TArray<FString> RecentLines;
	int32 TotalErrorCount = 0;
	int32 TotalWarningCount = 0;
	int32 TotalNoteCount = 0;
	int32 LinesRead = 0;
	int32 CompletedActions = 0;
	int32 TotalActions = 0;
	FString CurrentAction;
	int32 ExitCode = -1;

	TSharedPtr<FJsonObject> LastResult;

	/** Hard caps so a catastrophic build cannot blow out the agent's context. */
	static constexpr int32 MaxStoredErrors = 200;
	static constexpr int32 MaxStoredWarnings = 200;
	static constexpr int32 MaxStoredNotes = 50;
	static constexpr int32 MaxRecentLines = 40;
};

/** Keep this many build logs on disk; older ones are pruned when a build starts. */
static constexpr int32 MaxRetainedLogs = 20;

FBuildJob::~FBuildJob()
{
	// Static destruction: never leave a detached UBT behind.
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree=*/true);
	}
	CloseProcessAndPipes();
}

void FBuildJob::ResetRunState()
{
	OutputBuffer.Reset();
	Errors.Reset();
	Warnings.Reset();
	Notes.Reset();
	SeenDiagnosticKeys.Reset();
	RecentLines.Reset();
	TotalErrorCount = 0;
	TotalWarningCount = 0;
	TotalNoteCount = 0;
	LinesRead = 0;
	CompletedActions = 0;
	TotalActions = 0;
	CurrentAction.Empty();
	ExitCode = -1;
	DurationSeconds = 0.0;
	bCancelRequested = false;
}

/** Delete all but the newest MaxRetainedLogs Build_*.log files. */
static void PruneOldBuildLogs()
{
	const FString LogDir = GetBuildLogDir();
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *(LogDir / TEXT("Build_*.log")), true, false);
	if (LogFiles.Num() <= MaxRetainedLogs)
	{
		return;
	}

	// File names embed a sortable yyyyMMdd_HHmmss stamp, so lexical order is chronological.
	LogFiles.Sort();
	const int32 DeleteCount = LogFiles.Num() - MaxRetainedLogs;
	for (int32 Index = 0; Index < DeleteCount; ++Index)
	{
		IFileManager::Get().Delete(*(LogDir / LogFiles[Index]), false, true, true);
	}
}

bool FBuildJob::Start(const FBuildRequest& InRequest, FString& OutError)
{
	if (bRunning)
	{
		OutError = TEXT("A build is already running. Poll action='status', or stop it with action='cancel'.");
		return false;
	}

	const FString ProjectFile = GetProjectFileFullPath();
	if (ProjectFile.IsEmpty())
	{
		OutError = TEXT("No .uproject is loaded, so there is no C++ target to build.");
		return false;
	}

	const FString BuildScript = GetEngineBuildScriptPath();
	if (!FPaths::FileExists(BuildScript))
	{
		OutError = FString::Printf(
			TEXT("Engine build script not found at '%s'. The editor's engine directory looks incomplete (source/installed build mismatch)."),
			*BuildScript);
		return false;
	}

	Request = InRequest;
	ResetRunState();

	BuildId = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

	// --- log file ---
	const FString LogDir = GetBuildLogDir();
	IFileManager::Get().MakeDirectory(*LogDir, true);
	PruneOldBuildLogs();
	LogFilePath = LogDir / FString::Printf(TEXT("Build_%s_%s_%s.log"), *Request.Target, *Request.Configuration, *BuildId);
	LogWriter.Reset(IFileManager::Get().CreateFileWriter(*LogFilePath, FILEWRITE_AllowRead));
	if (!LogWriter.IsValid())
	{
		// Not fatal - the build can still run, we just lose the on-disk transcript.
		UE_LOG(LogBuildTool, Warning, TEXT("Could not open build log for writing: %s"), *LogFilePath);
	}

	// --- argument string ---
	// Mirrors BuildPlugin.bat: <Target> <Platform> <Configuration> "<uproject>" -waitmutex
	FString Arguments = FString::Printf(
		TEXT("%s %s %s \"%s\" -waitmutex"),
		*Request.Target, *Request.Platform, *Request.Configuration, *ProjectFile);

	if (!Request.bHotReload)
	{
		// Opt out of UBT's running-editor detection. The link step will then target the
		// primary DLL and can fail with LNK1168 while the editor holds it open.
		Arguments += TEXT(" -NoHotReloadFromIDE");
	}
	if (!Request.ExtraArgs.IsEmpty())
	{
		Arguments += TEXT(" ") + Request.ExtraArgs;
	}

	// --- launch ---
	FString Url;
	FString Params;
#if PLATFORM_WINDOWS
	// CreateProcess cannot launch a .bat directly; go through the command
	// interpreter (%ComSpec%), the same treatment any .cmd/.bat shim needs.
	Url = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
	if (Url.IsEmpty())
	{
		Url = TEXT("cmd.exe");
	}
	Params = FString::Printf(TEXT("/d /s /c \"\"%s\" %s\""), *BuildScript, *Arguments);
#else
	Url = BuildScript;
	Params = Arguments;
#endif

	CommandLine = FString::Printf(TEXT("%s %s"), *Url, *Params);

	if (!FPlatformProcess::CreatePipe(StdOutRead, StdOutWrite)
		|| !FPlatformProcess::CreatePipe(StdErrRead, StdErrWrite))
	{
		CloseProcessAndPipes();
		OutError = TEXT("Failed to create the stdout/stderr pipes for UnrealBuildTool.");
		return false;
	}

	const FString WorkingDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	UE_LOG(LogBuildTool, Log, TEXT("Starting build: %s"), *CommandLine);

	ProcessHandle = FPlatformProcess::CreateProc(
		*Url,
		*Params,
		/*bLaunchDetached=*/false,
		/*bLaunchHidden=*/true,
		/*bLaunchReallyHidden=*/true,
		/*OutProcessID=*/nullptr,
		/*PriorityModifier=*/0,
		*WorkingDir,
		/*PipeWriteChild=*/StdOutWrite,
		/*PipeReadChild=*/nullptr,
		/*PipeStdErrChild=*/StdErrWrite);

	if (!ProcessHandle.IsValid())
	{
		CloseProcessAndPipes();
		LogWriter.Reset();
		OutError = FString::Printf(TEXT("Failed to launch UnrealBuildTool via '%s'."), *Url);
		return false;
	}

	bRunning = true;
	StartSeconds = FPlatformTime::Seconds();

	// A UBT run must never outlive the editor that started it. `this` is a
	// function-local static, so binding raw is safe for the process lifetime.
	if (!bShutdownHookInstalled)
	{
		FCoreDelegates::OnEnginePreExit.AddRaw(this, &FBuildJob::HandleEnginePreExit);
		bShutdownHookInstalled = true;
	}

	AppendToLogFile(FString::Printf(TEXT("[Revvy] %s"), *CommandLine));
	AppendToLogFile(FString::Printf(TEXT("[Revvy] cwd: %s"), *WorkingDir));

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FBuildJob::Tick), 0.0f);

	return true;
}

bool FBuildJob::Cancel(FString& OutError)
{
	if (!bRunning)
	{
		OutError = TEXT("No build is running.");
		return false;
	}

	bCancelRequested = true;
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree=*/true);
	}

	// Drain whatever the child already produced so the cancelled result still
	// carries the diagnostics found before the kill.
	DrainPipes();
	ProcessBufferedLines(/*bFlushIncomplete=*/true);
	Finish(TEXT("cancelled"));
	return true;
}

void FBuildJob::HandleEnginePreExit()
{
	if (!bRunning)
	{
		return;
	}

	UE_LOG(LogBuildTool, Log, TEXT("Editor is shutting down - terminating the in-flight build."));
	FString Unused;
	Cancel(Unused);
}

bool FBuildJob::Tick(float /*DeltaTime*/)
{
	bInTick = true;

	DrainPipes();
	ProcessBufferedLines(/*bFlushIncomplete=*/false);

	const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
	const bool bTimedOut = Request.TimeoutSeconds > 0.0 && Elapsed > Request.TimeoutSeconds;

	if (bTimedOut && ProcessHandle.IsValid())
	{
		UE_LOG(LogBuildTool, Warning, TEXT("Build timed out after %.0f s - terminating."), Elapsed);
		FPlatformProcess::TerminateProc(ProcessHandle, /*KillTree=*/true);
	}

	if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle) && !bTimedOut)
	{
		bInTick = false;
		return true;
	}

	// Final drain after exit - the child may have written since the last read.
	DrainPipes();
	ProcessBufferedLines(/*bFlushIncomplete=*/true);

	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::GetProcReturnCode(ProcessHandle, &ExitCode);
	}

	FString Status = TEXT("completed");
	if (bCancelRequested)
	{
		Status = TEXT("cancelled");
	}
	else if (bTimedOut)
	{
		Status = TEXT("timeout");
	}

	Finish(Status);

	bInTick = false;
	TickerHandle.Reset();
	return false;
}

void FBuildJob::DrainPipes()
{
	auto DrainOne = [this](void* Pipe)
	{
		if (!Pipe)
		{
			return;
		}
		TArray<uint8> Chunk;
		while (FPlatformProcess::ReadPipeToArray(Pipe, Chunk))
		{
			OutputBuffer.Append(Chunk);
			Chunk.Reset();
		}
	};

	DrainOne(StdOutRead);
	DrainOne(StdErrRead);
}

void FBuildJob::ProcessBufferedLines(bool bFlushIncomplete)
{
	auto DecodeRange = [this](int32 Start, int32 End) -> FString
	{
		if (End <= Start)
		{
			return FString();
		}
		TArray<ANSICHAR> Temp;
		Temp.Append(reinterpret_cast<const ANSICHAR*>(OutputBuffer.GetData() + Start), End - Start);
		Temp.Add('\0');
		return FString(UTF8_TO_TCHAR(Temp.GetData()));
	};

	int32 LineStart = 0;
	for (int32 Index = 0; Index < OutputBuffer.Num(); ++Index)
	{
		if (OutputBuffer[Index] != '\n')
		{
			continue;
		}

		int32 LineEnd = Index;
		if (LineEnd > LineStart && OutputBuffer[LineEnd - 1] == '\r')
		{
			--LineEnd;
		}
		HandleLine(DecodeRange(LineStart, LineEnd));
		LineStart = Index + 1;
	}

	if (bFlushIncomplete && LineStart < OutputBuffer.Num())
	{
		HandleLine(DecodeRange(LineStart, OutputBuffer.Num()));
		LineStart = OutputBuffer.Num();
	}

	if (LineStart > 0)
	{
		OutputBuffer.RemoveAt(0, LineStart);
	}
}

void FBuildJob::HandleLine(const FString& Line)
{
	++LinesRead;
	AppendToLogFile(Line);

	RecentLines.Add(Line);
	if (RecentLines.Num() > MaxRecentLines)
	{
		RecentLines.RemoveAt(0);
	}

	// UBT action progress: "[12/57] Compile Foo.cpp"
	{
		FRegexMatcher Matcher(GetPatterns().ActionProgress, Line);
		if (Matcher.FindNext())
		{
			CompletedActions = FCString::Atoi(*Matcher.GetCaptureGroup(1));
			TotalActions = FCString::Atoi(*Matcher.GetCaptureGroup(2));
			CurrentAction = Matcher.GetCaptureGroup(3).TrimStartAndEnd();
			return;
		}
	}

	FBuildDiagnostic Diagnostic;
	if (!ParseDiagnosticLine(Line, Diagnostic))
	{
		return;
	}

	// UBT echoes the same diagnostic from several actions; collapse duplicates.
	const FString Key = FString::Printf(TEXT("%s|%d|%d|%s|%s"),
		*Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Code, *Diagnostic.Message);
	if (SeenDiagnosticKeys.Contains(Key))
	{
		return;
	}
	SeenDiagnosticKeys.Add(Key);

	Diagnostic.FixHint = MakeFixHint(Diagnostic);

	if (Diagnostic.Severity == TEXT("error"))
	{
		++TotalErrorCount;
		if (Errors.Num() < MaxStoredErrors)
		{
			Errors.Add(MoveTemp(Diagnostic));
		}
	}
	else if (Diagnostic.Severity == TEXT("warning"))
	{
		++TotalWarningCount;
		if (Warnings.Num() < MaxStoredWarnings)
		{
			Warnings.Add(MoveTemp(Diagnostic));
		}
	}
	else
	{
		++TotalNoteCount;
		if (Notes.Num() < MaxStoredNotes)
		{
			Notes.Add(MoveTemp(Diagnostic));
		}
	}
}

void FBuildJob::AppendToLogFile(const FString& Line)
{
	if (!LogWriter.IsValid())
	{
		return;
	}
	// FTCHARToUTF8::Get() is const UTF8CHAR* on UE5; go through void* so the
	// element type cannot drift out from under us.
	FTCHARToUTF8 Utf8Line(*Line);
	if (Utf8Line.Length() > 0)
	{
		LogWriter->Serialize(const_cast<void*>(static_cast<const void*>(Utf8Line.Get())), Utf8Line.Length());
	}
	static const ANSICHAR Newline[] = "\r\n";
	LogWriter->Serialize(const_cast<void*>(static_cast<const void*>(Newline)), 2);
}

void FBuildJob::Finish(const FString& InStatus)
{
	DurationSeconds = FPlatformTime::Seconds() - StartSeconds;

	CloseProcessAndPipes();

	if (LogWriter.IsValid())
	{
		LogWriter->Flush();
		LogWriter->Close();
		LogWriter.Reset();
	}

	bRunning = false;

	LastResult = ComposeResultJson(InStatus);

	// Persist so `last_result` still answers after an editor restart.
	const FString ResultPath = GetLastResultPath();
	IFileManager::Get().MakeDirectory(*GetBuildLogDir(), true);
	FFileHelper::SaveStringToFile(SerializeJson(LastResult), *ResultPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogBuildTool, Log,
		TEXT("Build %s in %.1fs - exit=%d errors=%d warnings=%d (log: %s)"),
		*InStatus, DurationSeconds, ExitCode, TotalErrorCount, TotalWarningCount, *LogFilePath);

	if (!bInTick && TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void FBuildJob::CloseProcessAndPipes()
{
	if (StdOutRead || StdOutWrite)
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
		StdOutRead = nullptr;
		StdOutWrite = nullptr;
	}
	if (StdErrRead || StdErrWrite)
	{
		FPlatformProcess::ClosePipe(StdErrRead, StdErrWrite);
		StdErrRead = nullptr;
		StdErrWrite = nullptr;
	}
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::CloseProc(ProcessHandle);
		ProcessHandle.Reset();
	}
}

TSharedPtr<FJsonObject> FBuildJob::ComposeResultJson(const FString& InStatus) const
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const bool bBuildSucceeded = (InStatus == TEXT("completed")) && ExitCode == 0 && TotalErrorCount == 0;

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("status"), InStatus);
	Result->SetBoolField(TEXT("build_succeeded"), bBuildSucceeded);
	Result->SetStringField(TEXT("build_id"), BuildId);
	Result->SetStringField(TEXT("target"), Request.Target);
	Result->SetStringField(TEXT("platform"), Request.Platform);
	Result->SetStringField(TEXT("configuration"), Request.Configuration);
	Result->SetBoolField(TEXT("hot_reload"), Request.bHotReload);
	Result->SetNumberField(TEXT("exit_code"), ExitCode);
	Result->SetNumberField(TEXT("duration_seconds"), FMath::RoundToDouble(DurationSeconds * 10.0) / 10.0);
	Result->SetStringField(TEXT("log_path"), LogFilePath);
	Result->SetStringField(TEXT("command"), CommandLine);
	Result->SetNumberField(TEXT("lines_read"), LinesRead);

	TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("errors"), TotalErrorCount);
	Counts->SetNumberField(TEXT("warnings"), TotalWarningCount);
	Counts->SetNumberField(TEXT("notes"), TotalNoteCount);
	Counts->SetBoolField(TEXT("truncated"),
		TotalErrorCount > Errors.Num() || TotalWarningCount > Warnings.Num() || TotalNoteCount > Notes.Num());
	Result->SetObjectField(TEXT("counts"), Counts);

	auto ToArray = [](const TArray<FBuildDiagnostic>& Source)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		for (const FBuildDiagnostic& Diagnostic : Source)
		{
			Array.Add(MakeShared<FJsonValueObject>(DiagnosticToJson(Diagnostic)));
		}
		return Array;
	};

	Result->SetArrayField(TEXT("errors"), ToArray(Errors));
	Result->SetArrayField(TEXT("warnings"), ToArray(Warnings));
	Result->SetArrayField(TEXT("notes"), ToArray(Notes));

	// When a build fails without a single parsed diagnostic the failure is in the
	// toolchain itself; the raw tail is the only thing that can explain it.
	if (!bBuildSucceeded && Errors.Num() == 0 && RecentLines.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Tail;
		for (const FString& Line : RecentLines)
		{
			Tail.Add(MakeShared<FJsonValueString>(Line));
		}
		Result->SetArrayField(TEXT("log_tail"), Tail);
	}

	FString Summary;
	if (bBuildSucceeded)
	{
		Summary = FString::Printf(TEXT("Build succeeded in %.1fs (%d warnings)."), DurationSeconds, TotalWarningCount);
	}
	else if (InStatus == TEXT("cancelled"))
	{
		Summary = TEXT("Build cancelled before completion; diagnostics below are partial.");
	}
	else if (InStatus == TEXT("timeout"))
	{
		Summary = FString::Printf(TEXT("Build timed out after %.0fs and was terminated."), DurationSeconds);
	}
	else
	{
		Summary = FString::Printf(
			TEXT("Build FAILED (exit %d): %d errors, %d warnings. Fix the errors below and re-run action='compile'."),
			ExitCode, TotalErrorCount, TotalWarningCount);
	}
	Result->SetStringField(TEXT("summary"), Summary);

	return Result;
}

TSharedPtr<FJsonObject> FBuildJob::BuildStatusJson() const
{
	TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
	Status->SetBoolField(TEXT("success"), true);
	Status->SetStringField(TEXT("status"), bRunning ? TEXT("running") : TEXT("idle"));

	if (!bRunning)
	{
		if (LastResult.IsValid())
		{
			Status->SetObjectField(TEXT("last_build"), LastResult);
		}
		Status->SetStringField(TEXT("note"), TEXT("No build in flight. Start one with action='compile'."));
		return Status;
	}

	Status->SetStringField(TEXT("build_id"), BuildId);
	Status->SetStringField(TEXT("target"), Request.Target);
	Status->SetStringField(TEXT("configuration"), Request.Configuration);
	Status->SetStringField(TEXT("log_path"), LogFilePath);
	Status->SetNumberField(TEXT("elapsed_seconds"),
		FMath::RoundToDouble((FPlatformTime::Seconds() - StartSeconds) * 10.0) / 10.0);
	Status->SetNumberField(TEXT("timeout_seconds"), Request.TimeoutSeconds);
	Status->SetNumberField(TEXT("lines_read"), LinesRead);
	Status->SetNumberField(TEXT("errors_so_far"), TotalErrorCount);
	Status->SetNumberField(TEXT("warnings_so_far"), TotalWarningCount);

	TSharedPtr<FJsonObject> Progress = MakeShared<FJsonObject>();
	Progress->SetNumberField(TEXT("completed_actions"), CompletedActions);
	Progress->SetNumberField(TEXT("total_actions"), TotalActions);
	Progress->SetNumberField(TEXT("percent"),
		TotalActions > 0 ? FMath::Clamp((CompletedActions * 100) / TotalActions, 0, 100) : 0);
	Progress->SetStringField(TEXT("current_action"), CurrentAction);
	Status->SetObjectField(TEXT("progress"), Progress);

	return Status;
}

TSharedPtr<FJsonObject> FBuildJob::GetLastResult()
{
	if (LastResult.IsValid())
	{
		return LastResult;
	}

	// Nothing in memory - a previous editor session may have left one on disk.
	FString FileContents;
	if (!FFileHelper::LoadFileToString(FileContents, *GetLastResultPath()))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> Parsed;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContents);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		return nullptr;
	}

	Parsed->SetBoolField(TEXT("from_disk"), true);
	LastResult = Parsed;
	return LastResult;
}

// ---------------------------------------------------------------------------
// Result shaping for the caller (caps, filters)
// ---------------------------------------------------------------------------

/** Copy a stored result, trimming diagnostic arrays to what the caller asked for. */
static TSharedPtr<FJsonObject> ShapeResult(
	const TSharedPtr<FJsonObject>& Source,
	int32 MaxErrors,
	int32 MaxWarnings,
	bool bIncludeWarnings,
	bool bIncludeNotes)
{
	if (!Source.IsValid())
	{
		return nullptr;
	}

	// Shallow copy of the field map so the cached result keeps its full contents.
	TSharedPtr<FJsonObject> Shaped = MakeShared<FJsonObject>();
	Shaped->Values = Source->Values;

	auto Trim = [&Shaped](const FString& Field, int32 MaxCount)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Shaped->TryGetArrayField(Field, Array) || !Array)
		{
			return;
		}
		if (MaxCount < 0 || Array->Num() <= MaxCount)
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Trimmed;
		for (int32 Index = 0; Index < MaxCount; ++Index)
		{
			Trimmed.Add((*Array)[Index]);
		}
		Shaped->SetArrayField(Field, Trimmed);
		Shaped->SetBoolField(Field + TEXT("_trimmed"), true);
	};

	Trim(TEXT("errors"), MaxErrors);

	if (bIncludeWarnings)
	{
		Trim(TEXT("warnings"), MaxWarnings);
	}
	else
	{
		Shaped->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
	}

	if (!bIncludeNotes)
	{
		Shaped->SetArrayField(TEXT("notes"), TArray<TSharedPtr<FJsonValue>>());
	}

	return Shaped;
}

// ---------------------------------------------------------------------------
// help
// ---------------------------------------------------------------------------

static FString GetBuildToolHelp()
{
	TSharedPtr<FJsonObject> Help = MakeShared<FJsonObject>();
	Help->SetBoolField(TEXT("success"), true);
	Help->SetStringField(TEXT("tool"), TEXT("build_project"));
	Help->SetStringField(TEXT("description"),
		TEXT("Compile the loaded project's C++ with UnrealBuildTool and return structured compiler errors. "
			"The build runs asynchronously so the editor never freezes: 'compile' returns immediately, "
			"then poll 'status' and read 'last_result'."));

	TArray<TSharedPtr<FJsonValue>> Actions;
	auto AddAction = [&Actions](const FString& Name, const FString& Desc, const FString& ParamsDesc)
	{
		TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
		ActionObj->SetStringField(TEXT("action"), Name);
		ActionObj->SetStringField(TEXT("description"), Desc);
		ActionObj->SetStringField(TEXT("parameters"), ParamsDesc);
		Actions.Add(MakeShared<FJsonValueObject>(ActionObj));
	};

	AddAction(TEXT("compile"),
		TEXT("Start a UBT build of the editor target and return immediately with status='running'."),
		TEXT("target, platform, configuration, hot_reload, timeout_seconds, extra_args (all optional)"));
	AddAction(TEXT("status"),
		TEXT("Progress of the in-flight build (elapsed, [n/total] actions, errors so far). Returns the last build when idle."),
		TEXT("None"));
	AddAction(TEXT("last_result"),
		TEXT("Structured result of the last finished build. Does NOT rebuild."),
		TEXT("max_errors (default 50), max_warnings (default 25), include_warnings (default true), include_notes (default false)"));
	AddAction(TEXT("cancel"),
		TEXT("Terminate the running build; its partial diagnostics are kept as the last result."),
		TEXT("None"));
	AddAction(TEXT("help"),
		TEXT("Show this message."),
		TEXT("None"));

	Help->SetArrayField(TEXT("actions"), Actions);

	TSharedPtr<FJsonObject> Defaults = MakeShared<FJsonObject>();
	Defaults->SetStringField(TEXT("target"), TEXT("auto-detected from <Project>/Source/*.Target.cs (TargetType.Editor)"));
	Defaults->SetStringField(TEXT("platform"), GetDefaultPlatformName());
	Defaults->SetStringField(TEXT("configuration"), TEXT("Development"));
	Defaults->SetStringField(TEXT("hot_reload"),
		TEXT("true - UBT detects the running editor and links a versioned hot-reload DLL. "
			"Set false for a plain build; that can fail with LNK1168 while the editor holds the DLL open."));
	Defaults->SetNumberField(TEXT("timeout_seconds"), 900);
	Defaults->SetStringField(TEXT("log_dir"), GetBuildLogDir());
	Help->SetObjectField(TEXT("defaults"), Defaults);

	TSharedPtr<FJsonObject> Shape = MakeShared<FJsonObject>();
	Shape->SetStringField(TEXT("errors[]"),
		TEXT("{severity, file, line, column, code, message, raw_snippet, fix_hint}"));
	Shape->SetStringField(TEXT("counts"), TEXT("{errors, warnings, notes, truncated}"));
	Shape->SetStringField(TEXT("build_succeeded"), TEXT("true only when exit_code==0 AND zero parsed errors"));
	Shape->SetStringField(TEXT("log_tail"),
		TEXT("last 40 raw lines, present only when the build failed with no parsed diagnostic"));
	Help->SetObjectField(TEXT("result_shape"), Shape);

	TArray<TSharedPtr<FJsonValue>> Examples;
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=compile")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=compile, configuration=DebugGame, timeout_seconds=1800")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=status")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=last_result, max_errors=10, include_warnings=false")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=cancel")));
	Help->SetArrayField(TEXT("examples"), Examples);

	TArray<TSharedPtr<FJsonValue>> Notes;
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("A compile writes Binaries/ and Intermediate/ only - it never touches Content/ or any asset.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Recompiled C++ does not take effect in the running editor until Live Coding / hot reload loads it.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Only one build runs at a time; 'compile' while one is in flight returns BUILD_ALREADY_RUNNING.")));
	Help->SetArrayField(TEXT("notes"), Notes);

	return SerializeJson(Help);
}

// ---------------------------------------------------------------------------
// Action dispatch
// ---------------------------------------------------------------------------

static FString HandleCompile(const TMap<FString, FString>& Params)
{
	FBuildJob& Job = FBuildJob::Get();

	if (Job.IsRunning())
	{
		TSharedPtr<FJsonObject> Busy = Job.BuildStatusJson();
		Busy->SetBoolField(TEXT("success"), false);
		Busy->SetStringField(TEXT("error_code"), TEXT("BUILD_ALREADY_RUNNING"));
		Busy->SetStringField(TEXT("error"),
			TEXT("A build is already running. Poll action='status', or stop it with action='cancel'."));
		return SerializeJson(Busy);
	}

	FBuildRequest Request;

	const FString RequestedTarget = ExtractParam(Params, TEXT("target")).TrimStartAndEnd();
	if (RequestedTarget.IsEmpty())
	{
		Request.Target = SanitizeToken(AutoDetectEditorTarget());
	}
	else
	{
		Request.Target = SanitizeToken(RequestedTarget);
		if (Request.Target != RequestedTarget)
		{
			return BuildErrorResponse(TEXT("INVALID_TARGET"),
				FString::Printf(TEXT("Target '%s' is not a plain target name. Use e.g. 'LyraEditor'."), *RequestedTarget));
		}
	}
	if (Request.Target.IsEmpty())
	{
		return BuildErrorResponse(TEXT("TARGET_NOT_FOUND"),
			TEXT("Could not determine an editor target. Pass target='<Project>Editor' explicitly."));
	}

	const FString RequestedPlatform =
		ExtractParamWithDefault(Params, TEXT("platform"), GetDefaultPlatformName()).TrimStartAndEnd();
	Request.Platform = SanitizeToken(RequestedPlatform);
	if (Request.Platform.IsEmpty() || Request.Platform != RequestedPlatform)
	{
		return BuildErrorResponse(TEXT("INVALID_PLATFORM"),
			FString::Printf(TEXT("Platform '%s' is not a plain platform name. Use Win64, Mac or Linux."), *RequestedPlatform));
	}

	Request.Configuration = ExtractParamWithDefault(Params, TEXT("configuration"), TEXT("Development")).TrimStartAndEnd();
	if (!IsKnownConfiguration(Request.Configuration))
	{
		return BuildErrorResponse(TEXT("INVALID_CONFIGURATION"),
			FString::Printf(TEXT("Unknown configuration '%s'. Use Debug, DebugGame, Development, Shipping or Test."),
				*Request.Configuration));
	}

	Request.ExtraArgs = ExtractParam(Params, TEXT("extra_args")).TrimStartAndEnd();
	Request.bHotReload = ExtractBoolParam(Params, TEXT("hot_reload"), true);
	Request.TimeoutSeconds = static_cast<double>(
		FMath::Clamp(ExtractIntParam(Params, TEXT("timeout_seconds"), 900), 30, 7200));

	FString Error;
	if (!Job.Start(Request, Error))
	{
		return BuildErrorResponse(TEXT("BUILD_START_FAILED"), Error);
	}

	TSharedPtr<FJsonObject> Started = MakeShared<FJsonObject>();
	Started->SetBoolField(TEXT("success"), true);
	Started->SetStringField(TEXT("status"), TEXT("running"));
	Started->SetStringField(TEXT("build_id"), Job.GetBuildId());
	Started->SetStringField(TEXT("target"), Request.Target);
	Started->SetStringField(TEXT("platform"), Request.Platform);
	Started->SetStringField(TEXT("configuration"), Request.Configuration);
	Started->SetStringField(TEXT("project"), GetProjectFileFullPath());
	Started->SetBoolField(TEXT("hot_reload"), Request.bHotReload);
	Started->SetNumberField(TEXT("timeout_seconds"), Request.TimeoutSeconds);
	Started->SetStringField(TEXT("log_path"), Job.GetLogPath());
	Started->SetStringField(TEXT("command"), Job.GetCommandLine());
	Started->SetStringField(TEXT("note"),
		TEXT("Build started asynchronously so the editor stays responsive. Poll action='status' (a cold build "
			"takes minutes), then read the structured diagnostics with action='last_result'."));
	return SerializeJson(Started);
}

static FString HandleStatus()
{
	return SerializeJson(FBuildJob::Get().BuildStatusJson());
}

static FString HandleLastResult(const TMap<FString, FString>& Params)
{
	FBuildJob& Job = FBuildJob::Get();

	if (Job.IsRunning())
	{
		TSharedPtr<FJsonObject> Busy = Job.BuildStatusJson();
		Busy->SetStringField(TEXT("note"),
			TEXT("A build is still running - no final result yet. Keep polling action='status'."));
		return SerializeJson(Busy);
	}

	TSharedPtr<FJsonObject> Result = Job.GetLastResult();
	if (!Result.IsValid())
	{
		return BuildErrorResponse(TEXT("NO_BUILD_RESULT"),
			TEXT("No build has been run yet in this project. Start one with action='compile'."));
	}

	const int32 MaxErrors = FMath::Max(0, ExtractIntParam(Params, TEXT("max_errors"), 50));
	const int32 MaxWarnings = FMath::Max(0, ExtractIntParam(Params, TEXT("max_warnings"), 25));
	const bool bIncludeWarnings = ExtractBoolParam(Params, TEXT("include_warnings"), true);
	const bool bIncludeNotes = ExtractBoolParam(Params, TEXT("include_notes"), false);

	return SerializeJson(ShapeResult(Result, MaxErrors, MaxWarnings, bIncludeWarnings, bIncludeNotes));
}

static FString HandleCancel()
{
	FBuildJob& Job = FBuildJob::Get();

	FString Error;
	if (!Job.Cancel(Error))
	{
		return BuildErrorResponse(TEXT("NO_BUILD_RUNNING"), Error);
	}

	TSharedPtr<FJsonObject> Cancelled = MakeShared<FJsonObject>();
	Cancelled->SetBoolField(TEXT("success"), true);
	Cancelled->SetStringField(TEXT("status"), TEXT("cancelled"));
	Cancelled->SetStringField(TEXT("note"),
		TEXT("UnrealBuildTool was terminated. Partial diagnostics are available via action='last_result'."));
	if (TSharedPtr<FJsonObject> Result = Job.GetLastResult())
	{
		Cancelled->SetObjectField(TEXT("last_build"), Result);
	}
	return SerializeJson(Cancelled);
}

/** Single entry point - keeps the REGISTER macro body free of top-level commas. */
static FString ExecuteBuildTool(const TMap<FString, FString>& Params)
{
	const FString Action = ExtractParam(Params, TEXT("action")).TrimStartAndEnd().ToLower();

	if (Action.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_ACTION"),
			TEXT("The 'action' parameter is required. Use action=help for documentation."));
	}
	if (Action == TEXT("help"))
	{
		return GetBuildToolHelp();
	}
	if (Action == TEXT("compile") || Action == TEXT("build"))
	{
		return HandleCompile(Params);
	}
	if (Action == TEXT("status"))
	{
		return HandleStatus();
	}
	if (Action == TEXT("last_result") || Action == TEXT("result"))
	{
		return HandleLastResult(Params);
	}
	if (Action == TEXT("cancel"))
	{
		return HandleCancel();
	}

	return BuildErrorResponse(TEXT("UNKNOWN_ACTION"),
		FString::Printf(TEXT("Unknown action: %s. Valid: compile, status, last_result, cancel, help."), *Action));
}

/**
 * MCP annotations.
 *
 * A compile is a write (Binaries/, Intermediate/, Saved/Revvy/Build/) so readOnlyHint
 * is false, but it destroys nothing: no asset, package or source file is touched, and
 * re-running it converges on the same output - hence destructiveHint=false and
 * idempotentHint=true. Everything stays on this machine, so openWorldHint=false.
 *
 * ReadOnlyActions keeps the query verbs reachable from a read-only MCP session
 * instead of hiding the whole tool from it.
 */
static FToolAnnotations MakeBuildToolAnnotations()
{
	FToolAnnotations Annotations;
	Annotations.Title = TEXT("Build Project (C++ compile verification)");
	Annotations.bReadOnlyHint = false;
	Annotations.bDestructiveHint = false;
	Annotations.bIdempotentHint = true;
	Annotations.bOpenWorldHint = false;
	Annotations.bAnnotated = true;
	Annotations.ReadOnlyActions = { TEXT("status"), TEXT("last_result"), TEXT("result"), TEXT("help") };
	return Annotations;
}

} // namespace RevvyBuildTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(build_project,
	"Compile this project's C++ with UnrealBuildTool and read the errors back as structured JSON. "
	"Use this to verify your own C++ edits before claiming they work.\n"
	"\nThe build is ASYNCHRONOUS so the editor never freezes: action='compile' returns immediately with "
	"status='running'. Poll action='status' until status='idle', then read action='last_result'.\n"
	"\nACTIONS:\n"
	"  compile      : start a build of the editor target (auto-detected). Returns build_id + log_path.\n"
	"  status       : elapsed time, [completed/total] UBT actions, errors so far. 'idle' means it finished.\n"
	"  last_result  : structured diagnostics of the last finished build. Never rebuilds.\n"
	"  cancel       : terminate the running build; partial diagnostics are kept.\n"
	"  help         : full parameter and result-shape documentation.\n"
	"\nEach diagnostic is {severity, file, line, column, code, message, raw_snippet, fix_hint}, where "
	"fix_hint is a heuristic for common UE mistakes (C2065 -> missing #include, LNK2019 -> module missing "
	"from Build.cs, C4996 -> the suggested replacement API). Counts and an overall build_succeeded flag "
	"come back alongside.\n"
	"\nTYPICAL LOOP:\n"
	"  1. edit C++\n"
	"  2. action='compile'\n"
	"  3. action='status'  (repeat; a cold build takes minutes)\n"
	"  4. action='last_result'  -> fix the reported errors -> back to 2\n"
	"\nWrites Binaries/ and Intermediate/ only; no asset or content is modified. Full output is written to "
	"Saved/Revvy/Build/.",
	"Build",
	RevvyBuildTool::MakeBuildToolAnnotations(),
	TOOL_PARAMS(
		TOOL_PARAM("action", "Operation: compile, status, last_result, cancel, help", "string", true),
		TOOL_PARAM("target", "[compile] Build target name. Default: editor target auto-detected from <Project>/Source/*.Target.cs (e.g. 'LyraEditor')", "string", false),
		TOOL_PARAM("platform", "[compile] Target platform. Default: the host platform (Win64/Mac/Linux)", "string", false),
		TOOL_PARAM("configuration", "[compile] Debug, DebugGame, Development, Shipping or Test. Default Development", "string", false),
		TOOL_PARAM("hot_reload", "[compile] Default true: let UBT detect the running editor and link a versioned hot-reload DLL. false adds -NoHotReloadFromIDE and may fail with LNK1168 while the editor is open", "boolean", false),
		TOOL_PARAM("timeout_seconds", "[compile] Kill the build after this many seconds (default 900, range 30-7200)", "number", false),
		TOOL_PARAM("extra_args", "[compile] Extra arguments appended verbatim to the UnrealBuildTool command line", "string", false),
		TOOL_PARAM("max_errors", "[last_result] Maximum error entries to return (default 50)", "number", false),
		TOOL_PARAM("max_warnings", "[last_result] Maximum warning entries to return (default 25)", "number", false),
		TOOL_PARAM("include_warnings", "[last_result] Include warnings (default true)", "boolean", false),
		TOOL_PARAM("include_notes", "[last_result] Include compiler 'note:' lines (default false)", "boolean", false)
	),
	{
		return RevvyBuildTool::ExecuteBuildTool(Params);
	}
);
