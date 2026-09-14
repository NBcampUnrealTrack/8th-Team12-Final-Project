// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPerformanceService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPerf, Log, All);

// Core
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

// Editor / Engine
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

// RHI (GPU frame timing)
#include "RHI.h"
#include "DynamicRHI.h"

// Shader compile
#include "ShaderCompiler.h"

// Insights trace
#include "ProfilingDebugging/TraceAuxiliary.h"

// Stats subsystem
#include "Stats/Stats.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Editor subsystem enumeration (editor-only)
#if WITH_EDITOR
#include "EditorSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#endif

// =================================================================
// Engine / RenderCore module globals (declared once for the whole TU)
// =================================================================
// Smoothed averages (Engine module globals — always defined even if 0)
extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

// Thread cycle counters (RenderCore module globals)
extern RENDERCORE_API uint32 GGameThreadTime;
extern RENDERCORE_API uint32 GRenderThreadTime;
extern RENDERCORE_API uint32 GRHIThreadTime;

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString PerformanceJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}

	/** Serialize a JSON array to a compact FString. */
	FString PerformanceJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Convert an FString error message to a {"error":"..."} JSON payload. */
	FString PerformanceMakeErrorJson(const FString& Msg)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("error"), Msg);
		return PerformanceJsonObjectToString(Obj);
	}

	/** GPU frame time in milliseconds — safe wrapper around RHIGetGPUFrameCycles. */
	float SampleGPUFrameMs()
	{
		const uint32 GPUCycles = RHIGetGPUFrameCycles(0);
		return FPlatformTime::ToMilliseconds(GPUCycles);
	}

	/** Heuristic: treat as file path when it contains '/', '\', ':' or ends in .utrace. */
	bool LooksLikeFilePath(const FString& InStr)
	{
		if (InStr.IsEmpty()) { return false; }
		if (InStr.EndsWith(TEXT(".utrace"), ESearchCase::IgnoreCase)) { return true; }
		if (InStr.Contains(TEXT("/")) || InStr.Contains(TEXT("\\"))) { return true; }
		// Windows drive letter "D:" style
		if (InStr.Len() >= 2 && InStr[1] == TEXT(':')) { return true; }
		return false;
	}
}

// =================================================================
// Frame stats (live)
// =================================================================

FString UPerformanceService::GetCurrentFrameStats()
{
	if (!GEngine)
	{
		UE_LOG(LogRevvyPerf, Warning, TEXT("GetCurrentFrameStats: GEngine is null."));
		return FString();
	}

	const float FrameTimeMs    = GAverageMS;
	const float FPS            = GAverageFPS;
	const float GameThreadMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const float RenderThreadMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const float RHIThreadMs    = FPlatformTime::ToMilliseconds(GRHIThreadTime);
	const float GPUMs          = SampleGPUFrameMs();

	// Max tick rate — best-effort; requires a valid world for delta time.
	float MaxTickRate = 0.f;
	{
		float DeltaSec = 0.f;
#if WITH_EDITOR
		if (GEditor)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				DeltaSec = World->GetDeltaSeconds();
			}
		}
#endif
		MaxTickRate = GEngine->GetMaxTickRate(DeltaSec, /*bAllowSmoothing=*/true);
	}

	// Draw calls / primitives — STAT counters are only exposed via GET_DWORD_STAT
	// inside #if STATS blocks and are expensive to poll every frame. Report 0
	// with a flag so the Python side can decide to fall back to `stat scenerendering`.
	const int32 DrawCalls  = 0;
	const int32 Primitives = 0;

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("frame_time_ms"),    FrameTimeMs);
	Obj->SetNumberField(TEXT("game_thread_ms"),   GameThreadMs);
	Obj->SetNumberField(TEXT("render_thread_ms"), RenderThreadMs);
	Obj->SetNumberField(TEXT("gpu_ms"),           GPUMs);
	Obj->SetNumberField(TEXT("rhi_ms"),           RHIThreadMs);
	Obj->SetNumberField(TEXT("draw_calls"),       DrawCalls);
	Obj->SetNumberField(TEXT("primitives"),       Primitives);
	Obj->SetNumberField(TEXT("avg_fps"),          FPS);
	Obj->SetNumberField(TEXT("max_tick_rate"),    MaxTickRate);
	Obj->SetBoolField  (TEXT("stats_counters_available"), false);

	return PerformanceJsonObjectToString(Obj);
}

FString UPerformanceService::GetCPUGPUVerdict()
{
	const float GameMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const float RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const float GPUMs    = SampleGPUFrameMs();

	FString Verdict;
	FString Dominant;

	const float CPUMax = FMath::Max(GameMs, RenderMs);
	if (GPUMs > CPUMax * 1.10f)
	{
		Verdict  = TEXT("GPU-Bound");
		Dominant = TEXT("gpu");
	}
	else if (GameMs >= RenderMs && GameMs >= GPUMs)
	{
		Verdict  = TEXT("CPU-Bound (Game)");
		Dominant = TEXT("game_thread");
	}
	else if (RenderMs >= GameMs && RenderMs >= GPUMs)
	{
		Verdict  = TEXT("CPU-Bound (Render)");
		Dominant = TEXT("render_thread");
	}
	else
	{
		Verdict  = TEXT("Balanced");
		Dominant = TEXT("balanced");
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("verdict"),        Verdict);
	Obj->SetNumberField(TEXT("game_ms"),        GameMs);
	Obj->SetNumberField(TEXT("render_ms"),      RenderMs);
	Obj->SetNumberField(TEXT("gpu_ms"),         GPUMs);
	Obj->SetStringField(TEXT("dominant_axis"),  Dominant);

	return PerformanceJsonObjectToString(Obj);
}

// =================================================================
// Unreal Insights trace capture
// =================================================================

bool UPerformanceService::StartInsightsTrace(const FString& OutputPathOrServer, const FString& ChannelListCommaSeparated)
{
#if UE_TRACE_ENABLED
	if (OutputPathOrServer.IsEmpty())
	{
		UE_LOG(LogRevvyPerf, Warning, TEXT("StartInsightsTrace: OutputPathOrServer is empty."));
		return false;
	}

	const FString Channels = ChannelListCommaSeparated.IsEmpty()
		? FString(TEXT("cpu,gpu,frame,bookmark,stats,counters"))
		: ChannelListCommaSeparated;

	const FTraceAuxiliary::EConnectionType ConnType = LooksLikeFilePath(OutputPathOrServer)
		? FTraceAuxiliary::EConnectionType::File
		: FTraceAuxiliary::EConnectionType::Network;

	FTraceAuxiliary::FOptions Opts;
	Opts.bTruncateFile = true;
	Opts.bExcludeTail  = false;

	const bool bStarted = FTraceAuxiliary::Start(
		ConnType,
		*OutputPathOrServer,
		*Channels,
		&Opts,
		LogRevvyPerf);

	if (!bStarted)
	{
		UE_LOG(LogRevvyPerf, Warning, TEXT("StartInsightsTrace: FTraceAuxiliary::Start returned false (target='%s', channels='%s')."),
			*OutputPathOrServer, *Channels);
	}
	return bStarted;
#else
	UE_LOG(LogRevvyPerf, Warning, TEXT("StartInsightsTrace: UE_TRACE_ENABLED is 0 in this build; trace capture unavailable."));
	return false;
#endif
}

bool UPerformanceService::StopInsightsTrace()
{
#if UE_TRACE_ENABLED
	return FTraceAuxiliary::Stop();
#else
	UE_LOG(LogRevvyPerf, Warning, TEXT("StopInsightsTrace: UE_TRACE_ENABLED is 0 in this build."));
	return false;
#endif
}

bool UPerformanceService::IsInsightsTraceActive()
{
#if UE_TRACE_ENABLED
	return FTraceAuxiliary::IsConnected();
#else
	return false;
#endif
}

// =================================================================
// Trace analysis (best-effort)
// =================================================================

FString UPerformanceService::AnalyzeTraceSummary(const FString& UTracePath)
{
#if WITH_EDITOR
	if (UTracePath.IsEmpty())
	{
		UE_LOG(LogRevvyPerf, Warning, TEXT("AnalyzeTraceSummary: UTracePath is empty."));
		return PerformanceMakeErrorJson(TEXT("UTracePath is empty"));
	}

	// TraceServices in-process analysis is not surfaced through a stable
	// public API in UE 5.8. Provide an actionable fallback so tooling can
	// display a helpful hint rather than failing silently.
	const FString AbsPath = FPaths::ConvertRelativePathToFull(UTracePath);
	UE_LOG(LogRevvyPerf, Warning,
		TEXT("AnalyzeTraceSummary: in-process analysis unavailable in UE 5.8 build; open '%s' in UnrealInsights.exe."),
		*AbsPath);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("error"),
		FString::Printf(TEXT("trace analysis requires editor Insights UI; use UnrealInsights.exe to open %s"), *AbsPath));
	Obj->SetStringField(TEXT("utrace_path"), AbsPath);
	Obj->SetBoolField  (TEXT("analysis_available"), false);
	return PerformanceJsonObjectToString(Obj);
#else
	return PerformanceMakeErrorJson(TEXT("trace analysis unavailable in non-editor builds"));
#endif
}

FString UPerformanceService::GetTopBottlenecks(const FString& ScopeStr)
{
	const FString Scope = ScopeStr.IsEmpty() ? FString(TEXT("live")) : ScopeStr.ToLower();

	if (Scope == TEXT("trace"))
	{
#if WITH_EDITOR
		// Delegate to the (currently stubbed) analysis path so callers get a
		// consistent JSON error payload rather than an empty array.
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("error"),
			TEXT("trace-scope bottleneck extraction unavailable in-process; open .utrace in UnrealInsights.exe"));
		Obj->SetArrayField(TEXT("scopes"), TArray<TSharedPtr<FJsonValue>>());
		return PerformanceJsonObjectToString(Obj);
#else
		return PerformanceMakeErrorJson(TEXT("trace scope unavailable in non-editor builds"));
#endif
	}

	// Live scope — the FThreadStats API is not stable / cheap to poll from a
	// UI service in UE 5.8. Return a best-effort snapshot built from the
	// same thread-cycle globals used by GetCurrentFrameStats so the shape is
	// consistent and non-empty.
	const float GameMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const float RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const float RHIMs    = FPlatformTime::ToMilliseconds(GRHIThreadTime);
	const float GPUMs    = SampleGPUFrameMs();

	auto MakeEntry = [](const TCHAR* Name, const TCHAR* Thread, float Ms)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("scope_name"), Name);
		Entry->SetStringField(TEXT("thread"),     Thread);
		Entry->SetNumberField(TEXT("avg_ms"),     Ms);
		Entry->SetNumberField(TEXT("calls"),      1);
		return Entry;
	};

	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Add(MakeShared<FJsonValueObject>(MakeEntry(TEXT("GameThread"),   TEXT("GameThread"),   GameMs)));
	Entries.Add(MakeShared<FJsonValueObject>(MakeEntry(TEXT("RenderThread"), TEXT("RenderThread"), RenderMs)));
	Entries.Add(MakeShared<FJsonValueObject>(MakeEntry(TEXT("RHIThread"),    TEXT("RHIThread"),    RHIMs)));
	Entries.Add(MakeShared<FJsonValueObject>(MakeEntry(TEXT("GPU"),          TEXT("GPU"),          GPUMs)));

	// Sort by avg_ms descending — hottest first.
	Entries.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		const double AMs = A->AsObject()->GetNumberField(TEXT("avg_ms"));
		const double BMs = B->AsObject()->GetNumberField(TEXT("avg_ms"));
		return AMs > BMs;
	});

	if (Entries.Num() > 10)
	{
		Entries.SetNum(10);
	}

	return PerformanceJsonArrayToString(Entries);
}

// =================================================================
// Build / compile status
// =================================================================

FString UPerformanceService::GetShaderCompilationStatus()
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	if (GShaderCompilingManager)
	{
		const bool  bCompiling  = GShaderCompilingManager->IsCompiling();
		const int32 Remaining   = GShaderCompilingManager->GetNumRemainingJobs();
		const int32 Outstanding = GShaderCompilingManager->GetNumOutstandingJobs();
		const int32 Pending     = GShaderCompilingManager->GetNumPendingJobs();
		const int32 Workers     = GShaderCompilingManager->GetNumLocalWorkers();

		Obj->SetBoolField  (TEXT("is_compiling"),     bCompiling);
		Obj->SetNumberField(TEXT("pending_jobs"),     Pending);
		Obj->SetNumberField(TEXT("outstanding_jobs"), Outstanding);
		Obj->SetNumberField(TEXT("remaining_jobs"),   Remaining);
		Obj->SetNumberField(TEXT("num_workers"),      Workers);
	}
	else
	{
		Obj->SetBoolField  (TEXT("is_compiling"),     false);
		Obj->SetNumberField(TEXT("pending_jobs"),     0);
		Obj->SetNumberField(TEXT("outstanding_jobs"), 0);
		Obj->SetNumberField(TEXT("remaining_jobs"),   0);
		Obj->SetNumberField(TEXT("num_workers"),      0);
		Obj->SetStringField(TEXT("note"),             TEXT("GShaderCompilingManager is null"));
	}

	return PerformanceJsonObjectToString(Obj);
}

FString UPerformanceService::GetLightingBuildProgress()
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

#if WITH_EDITOR
	bool bBuilding = false;
	if (GEditor)
	{
		// UEditorEngine::IsLightingBuildCurrentlyRunning is available since
		// UE 5.x and is the canonical live-build signal.
		bBuilding = GEditor->IsLightingBuildCurrentlyRunning();
	}

	Obj->SetBoolField  (TEXT("is_building"),      bBuilding);
	// Progress percent / elapsed seconds are not surfaced through a stable
	// public API in 5.8 — expose the boolean and leave the numeric fields
	// as 0 with a note so Python callers can gracefully fall back.
	Obj->SetNumberField(TEXT("progress_percent"), 0.0);
	Obj->SetNumberField(TEXT("elapsed_seconds"),  0.0);
	Obj->SetStringField(TEXT("note"),
		TEXT("progress_percent/elapsed_seconds not exposed by UEditorEngine in UE 5.8; is_building is authoritative"));
#else
	Obj->SetBoolField  (TEXT("is_building"),      false);
	Obj->SetNumberField(TEXT("progress_percent"), 0.0);
	Obj->SetNumberField(TEXT("elapsed_seconds"),  0.0);
	Obj->SetStringField(TEXT("note"),             TEXT("unavailable"));
#endif

	return PerformanceJsonObjectToString(Obj);
}

// =================================================================
// Editor subsystem snapshot
// =================================================================

FString UPerformanceService::ListEditorSubsystemsSnapshot()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

#if WITH_EDITOR
	if (!GEditor)
	{
		UE_LOG(LogRevvyPerf, Warning, TEXT("ListEditorSubsystemsSnapshot: GEditor is null."));
		return PerformanceJsonArrayToString(Entries);
	}

	const TArray<UEditorSubsystem*> Subsystems = GEditor->GetEditorSubsystemArrayCopy<UEditorSubsystem>();
	Entries.Reserve(Subsystems.Num());

	for (UEditorSubsystem* Sub : Subsystems)
	{
		if (!Sub) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"),  Sub->GetName());
		Entry->SetStringField(TEXT("class"), Sub->GetClass() ? Sub->GetClass()->GetPathName() : FString());
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
#else
	UE_LOG(LogRevvyPerf, Warning, TEXT("ListEditorSubsystemsSnapshot: not available in non-editor builds."));
#endif

	return PerformanceJsonArrayToString(Entries);
}
