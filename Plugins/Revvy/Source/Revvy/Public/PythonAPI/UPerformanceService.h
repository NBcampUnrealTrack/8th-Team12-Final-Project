// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPerformanceService.generated.h"

/**
 * Performance service exposed directly to Python for live editor perf
 * telemetry, Unreal Insights trace capture, shader-compile / lighting
 * build progress, and editor subsystem introspection.
 *
 * Mirrors the ULightingService / USequencerService pattern: all
 * UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.PerformanceService.<snake_case_name>(...)`.
 *
 * This is a read-only measurement service — no FScopedTransaction is
 * required because no assets or actors are mutated.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.PerformanceService
 *
 *   # Live frame stats
 *   stats = json.loads(svc.get_current_frame_stats())
 *   verdict = json.loads(svc.get_cpu_gpu_verdict())
 *
 *   # Unreal Insights trace capture
 *   svc.start_insights_trace("D:/Traces/PerfCapture.utrace",
 *                            "cpu,gpu,frame,bookmark,stats,counters")
 *   assert svc.is_insights_trace_active()
 *   svc.stop_insights_trace()
 *
 *   # Trace analysis (editor-only, best-effort)
 *   summary = json.loads(svc.analyze_trace_summary("D:/Traces/PerfCapture.utrace"))
 *   top    = json.loads(svc.get_top_bottlenecks("live"))
 *
 *   # Build / compile status
 *   shader   = json.loads(svc.get_shader_compilation_status())
 *   lighting = json.loads(svc.get_lighting_build_progress())
 *
 *   # Subsystem snapshot
 *   subsystems = json.loads(svc.list_editor_subsystems_snapshot())
 *
 * @note All 10 performance operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UPerformanceService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Frame Stats (Live)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Sample the current live frame stats from the running editor world.
	 *
	 * Returned JSON fields:
	 *   frame_time_ms, game_thread_ms, render_thread_ms, gpu_ms, rhi_ms,
	 *   draw_calls, primitives, avg_fps, max_tick_rate.
	 *
	 * @return JSON string. Empty string on early error (a warning is logged).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Live")
	static FString GetCurrentFrameStats();

	/**
	 * Classify the current frame as CPU-bound (game), CPU-bound (render),
	 * GPU-bound, or Balanced by comparing the smoothed game / render / GPU
	 * thread times.
	 *
	 * @return JSON {verdict, game_ms, render_ms, gpu_ms, dominant_axis}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Live")
	static FString GetCPUGPUVerdict();

	// ═══════════════════════════════════════════════════════════════════
	// Unreal Insights Trace
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Start an Unreal Insights trace capture (file or network).
	 *
	 * @param OutputPathOrServer          File path (contains '/' or '\\' or
	 *                                    ends in ".utrace") or hostname / IP
	 *                                    to stream to Session Browser.
	 * @param ChannelListCommaSeparated   Insights channels, e.g.
	 *                                    "cpu,gpu,frame,bookmark,stats,counters".
	 * @return True if the trace subsystem accepted the start request.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Trace")
	static bool StartInsightsTrace(const FString& OutputPathOrServer, const FString& ChannelListCommaSeparated);

	/**
	 * Stop the currently active Unreal Insights trace capture (if any).
	 *
	 * @return True if a trace was active and the stop request was accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Trace")
	static bool StopInsightsTrace();

	/**
	 * Query whether an Unreal Insights trace is currently connected.
	 *
	 * @return True if a trace connection is live.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Trace")
	static bool IsInsightsTraceActive();

	// ═══════════════════════════════════════════════════════════════════
	// Trace Analysis (best-effort, editor-only)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Analyze a .utrace file and return a JSON summary. In UE 5.8 the
	 * TraceServices in-process analysis surface is limited — if analysis
	 * is unavailable the return value is a JSON object of the form
	 * {"error": "..."} with a hint to open the trace in UnrealInsights.exe.
	 *
	 * @param UTracePath  Absolute path to a .utrace file.
	 * @return JSON string (summary or {error}).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Trace")
	static FString AnalyzeTraceSummary(const FString& UTracePath);

	/**
	 * Return the top-N (up to 10) hottest scopes.
	 *
	 * @param ScopeStr  "live" — iterate active in-process stat groups.
	 *                  "trace" — delegate to AnalyzeTraceSummary (may stub).
	 * @return JSON array of {scope_name, thread, avg_ms, calls}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Trace")
	static FString GetTopBottlenecks(const FString& ScopeStr);

	// ═══════════════════════════════════════════════════════════════════
	// Build / Compile Status
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Snapshot the global shader-compile manager.
	 *
	 * @return JSON {is_compiling, pending_jobs, outstanding_jobs,
	 *               remaining_jobs, num_workers}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Build")
	static FString GetShaderCompilationStatus();

	/**
	 * Snapshot the editor lighting build.
	 *
	 * @return JSON {is_building, progress_percent, elapsed_seconds}.
	 *         If unavailable, {is_building:false, note:"unavailable"}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Build")
	static FString GetLightingBuildProgress();

	// ═══════════════════════════════════════════════════════════════════
	// Subsystem Snapshot
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all currently loaded UEditorSubsystem instances.
	 *
	 * @return JSON array of {name, class}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Performance|Live")
	static FString ListEditorSubsystemsSnapshot();
};
