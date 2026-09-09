// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * MCP progress notification context (spec: notifications/progress + progressToken).
 *
 * Tools that perform long-running operations call FRevvyProgress::Report() during
 * execution; the call is silently dropped when no context is active (e.g. tool
 * invoked from chat, not MCP). MCPServer wraps every tool/call with a scope:
 *
 *   {
 *       FRevvyProgressScope Scope(ProgressToken, ReporterLambda);
 *       ToolResult = Registry.ExecuteTool(...);
 *   }
 *
 * Initial cut: TLS-stored Reporter lambda. Hooking the SSE / streamable-HTTP
 * channel for outbound `notifications/progress` is in MCPServer.cpp (see
 * SendProgressNotification). Tools never see the JSON-RPC machinery.
 */
class REVVY_API FRevvyProgress
{
public:
	/**
	 * Report progress from inside a tool execution. Drops silently when no MCP
	 * progress context is active for the current thread.
	 *
	 * @param Progress  Current step (0..Total). Should monotonically increase.
	 * @param Total     Total steps. Pass 0.f if unknown — clients render as indeterminate.
	 * @param Message   Optional human-readable status string.
	 */
	static void Report(float Progress, float Total = 1.0f, const FString& Message = FString());

	/** True if a progress context is active for the current thread. */
	static bool HasActiveContext();

	/**
	 * Install a per-thread emit sink for progress events. Preferred routing path:
	 * when set, FRevvyProgress::Report delivers (Progress, Message) to the sink
	 * instead of the FRevvyProgressScope Reporter or the UE_LOG fallback. The
	 * MCPServer installs this on the thread that actually executes the tool
	 * (the game thread) so that tool code calling Report() reaches the JSON-RPC
	 * response builder.
	 *
	 * Prefer FRevvyProgressEmitSinkGuard for RAII-safe scoping; direct callers
	 * MUST call ClearActiveEmitSink() before the enclosing scope exits.
	 */
	static void SetActiveEmitSink(TFunction<void(float /*Progress*/, const FString& /*Message*/)> Sink);

	/** Clear the per-thread emit sink installed by SetActiveEmitSink. */
	static void ClearActiveEmitSink();

	/** True if a per-thread emit sink is currently installed. */
	static bool HasActiveEmitSink();
};

/**
 * RAII helper that installs a per-thread emit sink on construction and restores
 * the previous sink on destruction. Prevents leaking sinks between tool calls
 * even when an exception propagates or an early return skips ClearActiveEmitSink.
 * Nested guards restore the outer sink correctly.
 */
class REVVY_API FRevvyProgressEmitSinkGuard
{
public:
	explicit FRevvyProgressEmitSinkGuard(TFunction<void(float /*Progress*/, const FString& /*Message*/)> Sink);
	~FRevvyProgressEmitSinkGuard();

	FRevvyProgressEmitSinkGuard(const FRevvyProgressEmitSinkGuard&) = delete;
	FRevvyProgressEmitSinkGuard& operator=(const FRevvyProgressEmitSinkGuard&) = delete;

private:
	struct FSavedSink;
	TSharedPtr<FSavedSink> Saved;
};

/**
 * RAII helper used by MCPServer to scope an MCP tool/call's progress context.
 * Constructor pushes the (ProgressToken, Reporter) pair into TLS for the
 * current thread. Destructor pops it. Nested scopes restore the outer scope.
 */
class REVVY_API FRevvyProgressScope
{
public:
	FRevvyProgressScope(const FString& InProgressToken, TFunction<void(const FString& /*Token*/, float /*Progress*/, float /*Total*/, const FString& /*Msg*/)> InReporter);
	~FRevvyProgressScope();

	FRevvyProgressScope(const FRevvyProgressScope&) = delete;
	FRevvyProgressScope& operator=(const FRevvyProgressScope&) = delete;

private:
	// Opaque saved state — restored on destruct so nested scopes work.
	struct FSavedSlot;
	TSharedPtr<FSavedSlot> Saved;
};
