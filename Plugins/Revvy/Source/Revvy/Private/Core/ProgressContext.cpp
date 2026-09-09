// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Core/ProgressContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyProgress, Log, All);

namespace
{
	// Per-thread current context. Read by FRevvyProgress::Report, written by FRevvyProgressScope.
	// thread_local for safe nested-scope behavior; tools never see this directly.
	struct FProgressSlot
	{
		FString ProgressToken;
		TFunction<void(const FString&, float, float, const FString&)> Reporter;
		bool bActive = false;
	};

	static thread_local FProgressSlot GCurrent;

	// Per-thread emit sink installed via FRevvyProgress::SetActiveEmitSink /
	// FRevvyProgressEmitSinkGuard. Preferred over the FRevvyProgressScope Reporter
	// when both are set — this is the newer API that MCPServer uses to funnel
	// progress into the JSON-RPC response builder.
	static thread_local TFunction<void(float, const FString&)> GEmitSink;
}

struct FRevvyProgressScope::FSavedSlot
{
	FProgressSlot Previous;
};

FRevvyProgressScope::FRevvyProgressScope(
	const FString& InProgressToken,
	TFunction<void(const FString&, float, float, const FString&)> InReporter)
{
	Saved = MakeShared<FSavedSlot>();
	Saved->Previous = GCurrent;
	GCurrent.ProgressToken = InProgressToken;
	GCurrent.Reporter = MoveTemp(InReporter);
	GCurrent.bActive = !InProgressToken.IsEmpty();
}

FRevvyProgressScope::~FRevvyProgressScope()
{
	if (Saved.IsValid())
	{
		GCurrent = Saved->Previous;
	}
}

void FRevvyProgress::Report(float Progress, float Total, const FString& Message)
{
	// Preferred routing path: newer per-thread emit sink API.
	if (GEmitSink)
	{
		GEmitSink(Progress, Message);
		return;
	}
	// Legacy path: token+total via FRevvyProgressScope Reporter.
	if (GCurrent.bActive && GCurrent.Reporter)
	{
		GCurrent.Reporter(GCurrent.ProgressToken, Progress, Total, Message);
		return;
	}
	// No MCP progress context — log at Verbose so debug builds can still see it.
	// This is the intentional local-debug stub and MUST be preserved.
	UE_LOG(LogRevvyProgress, Verbose, TEXT("[no-context] %.0f/%.0f %s"), Progress, Total, *Message);
}

bool FRevvyProgress::HasActiveContext()
{
	return GCurrent.bActive || static_cast<bool>(GEmitSink);
}

void FRevvyProgress::SetActiveEmitSink(TFunction<void(float, const FString&)> Sink)
{
	GEmitSink = MoveTemp(Sink);
	UE_LOG(LogRevvyProgress, Verbose, TEXT("[sink] set active=%s"),
		static_cast<bool>(GEmitSink) ? TEXT("yes") : TEXT("no"));
}

void FRevvyProgress::ClearActiveEmitSink()
{
	GEmitSink = nullptr;
	UE_LOG(LogRevvyProgress, Verbose, TEXT("[sink] cleared"));
}

bool FRevvyProgress::HasActiveEmitSink()
{
	return static_cast<bool>(GEmitSink);
}

struct FRevvyProgressEmitSinkGuard::FSavedSink
{
	TFunction<void(float, const FString&)> Previous;
};

FRevvyProgressEmitSinkGuard::FRevvyProgressEmitSinkGuard(TFunction<void(float, const FString&)> Sink)
{
	Saved = MakeShared<FSavedSink>();
	Saved->Previous = MoveTemp(GEmitSink);
	GEmitSink = MoveTemp(Sink);
	UE_LOG(LogRevvyProgress, Verbose, TEXT("[sink-guard] installed active=%s"),
		static_cast<bool>(GEmitSink) ? TEXT("yes") : TEXT("no"));
}

FRevvyProgressEmitSinkGuard::~FRevvyProgressEmitSinkGuard()
{
	if (Saved.IsValid())
	{
		GEmitSink = MoveTemp(Saved->Previous);
	}
	else
	{
		GEmitSink = nullptr;
	}
	UE_LOG(LogRevvyProgress, Verbose, TEXT("[sink-guard] restored"));
}
