// Copyright Buckley Builds LLC 2026 All Rights Reserved.

//
// run_automation_tests - the engine's Automation Test framework exposed as an MCP tool.
//
// WHY THIS IS ASYNC
// FToolRegistry::ExecuteTool runs on the game thread (MCPServer marshals every
// tools/call there and blocks the socket thread on the result). A test pass takes
// seconds to minutes and latent commands NEED engine frames to advance, so running
// inline would both freeze the editor and deadlock latent tests. Instead `run`
// snapshots the matching tests and returns immediately; a core-ticker state machine
// starts one test per frame-slot via FAutomationTestFramework::StartTestByName,
// pumps ExecuteLatentCommands() until the queue drains, then StopTest() collects
// the per-test FAutomationTestExecutionInfo. Callers poll `status`, then read
// `last_result`.
//
// KNOWN INTERACTION: while GIsAutomationTesting is true the editor's own
// AutomationWorker tick may also pump ExecuteLatentCommands(). Latent commands are
// written as re-entrant Update() polls so an extra pump per frame is harmless for
// unit/editor tests; frame-counting tests (screenshot waits) may complete early.
//

#include "Core/ToolRegistry.h"

#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/EnumClassFlags.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAutomationTool, Log, All);

namespace RevvyAutomationTool
{

// ---------------------------------------------------------------------------
// Parameter extraction (same shape as build_project / read_logs)
// ---------------------------------------------------------------------------

static FString ExtractParam(const TMap<FString, FString>& Params, const FString& FieldName)
{
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
// Flag decoding
// ---------------------------------------------------------------------------

static FString FilterBucketName(EAutomationTestFlags Flags)
{
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::SmokeFilter))    { return TEXT("smoke"); }
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::EngineFilter))   { return TEXT("engine"); }
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::ProductFilter))  { return TEXT("product"); }
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::PerfFilter))     { return TEXT("perf"); }
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::StressFilter))   { return TEXT("stress"); }
	if (EnumHasAnyFlags(Flags, EAutomationTestFlags::NegativeFilter)) { return TEXT("negative"); }
	return TEXT("none");
}

static bool RunsInEditorContext(EAutomationTestFlags Flags)
{
	return EnumHasAnyFlags(Flags, EAutomationTestFlags::EditorContext);
}

static bool NeedsUser(EAutomationTestFlags Flags)
{
	return EnumHasAnyFlags(Flags, EAutomationTestFlags::RequiresUser);
}

// ---------------------------------------------------------------------------
// The single in-flight run
// ---------------------------------------------------------------------------

struct FQueuedTest
{
	FString DisplayName;
	FString FullPath;
	FString Command;      // GetTestName() + optional " <parameter>" - what StartTestByName eats
	FString SkipReason;   // non-empty => never started
};

class FTestRun
{
public:
	static FTestRun& Get()
	{
		static FTestRun Instance;
		return Instance;
	}

	bool IsRunning() const { return bRunning; }

	FString Start(TArray<FQueuedTest>&& InQueue, const FString& InFilter, double InPerTestTimeout, double InTotalTimeout)
	{
		Queue = MoveTemp(InQueue);
		Filter = InFilter;
		PerTestTimeoutSeconds = InPerTestTimeout;
		TotalTimeoutSeconds = InTotalTimeout;
		Results.Reset();
		Index = 0;
		bTestInFlight = false;
		bCancelRequested = false;
		bRunning = true;
		RunStartSeconds = FPlatformTime::Seconds();

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FTestRun::Tick));

		int32 Runnable = 0;
		for (const FQueuedTest& Test : Queue)
		{
			if (Test.SkipReason.IsEmpty())
			{
				++Runnable;
			}
		}

		TSharedPtr<FJsonObject> Started = MakeShared<FJsonObject>();
		Started->SetBoolField(TEXT("success"), true);
		Started->SetStringField(TEXT("status"), TEXT("running"));
		Started->SetStringField(TEXT("filter"), Filter);
		Started->SetNumberField(TEXT("queued_tests"), Queue.Num());
		Started->SetNumberField(TEXT("runnable_tests"), Runnable);
		Started->SetNumberField(TEXT("skipped_tests"), Queue.Num() - Runnable);
		Started->SetStringField(TEXT("note"),
			TEXT("Tests run asynchronously so the editor stays responsive and latent commands get real frames. "
				"Poll action='status' until status='idle', then read action='last_result'."));
		return SerializeJson(Started);
	}

	void RequestCancel() { bCancelRequested = true; }

	TSharedPtr<FJsonObject> BuildStatusJson() const
	{
		TSharedPtr<FJsonObject> Status = MakeShared<FJsonObject>();
		Status->SetBoolField(TEXT("success"), true);
		Status->SetStringField(TEXT("status"), bRunning ? TEXT("running") : TEXT("idle"));

		if (!bRunning)
		{
			if (LastResult.IsValid())
			{
				Status->SetObjectField(TEXT("last_run"), LastResult);
			}
			Status->SetStringField(TEXT("note"), TEXT("No test run in flight. Start one with action='run'."));
			return Status;
		}

		Status->SetStringField(TEXT("filter"), Filter);
		Status->SetNumberField(TEXT("elapsed_seconds"),
			FMath::RoundToDouble((FPlatformTime::Seconds() - RunStartSeconds) * 10.0) / 10.0);

		TSharedPtr<FJsonObject> Progress = MakeShared<FJsonObject>();
		Progress->SetNumberField(TEXT("completed"), Results.Num());
		Progress->SetNumberField(TEXT("total"), Queue.Num());
		Progress->SetStringField(TEXT("current_test"),
			Queue.IsValidIndex(Index) ? Queue[Index].FullPath : FString());
		Status->SetObjectField(TEXT("progress"), Progress);

		if (bCancelRequested)
		{
			Status->SetBoolField(TEXT("cancel_requested"), true);
		}
		return Status;
	}

	TSharedPtr<FJsonObject> GetLastResult() const { return LastResult; }

private:
	bool Tick(float)
	{
		if (!bRunning)
		{
			return false;
		}

		FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
		const double Now = FPlatformTime::Seconds();

		if (Now - RunStartSeconds > TotalTimeoutSeconds)
		{
			AbortInFlightTest(Framework, TEXT("timeout"));
			FinishRun(TEXT("timeout"));
			return false;
		}

		if (!bTestInFlight)
		{
			// Fold any pre-marked skips into results without starting them.
			while (Queue.IsValidIndex(Index) && !Queue[Index].SkipReason.IsEmpty())
			{
				RecordSkip(Queue[Index]);
				++Index;
			}

			if (bCancelRequested || Index >= Queue.Num())
			{
				FinishRun(bCancelRequested ? TEXT("cancelled") : TEXT("completed"));
				return false;
			}

			Framework.StartTestByName(Queue[Index].Command, /*RoleIndex=*/0, Queue[Index].FullPath);
			if (!GIsAutomationTesting)
			{
				// StartTestByName refuses silently when a slow task is in progress, a
				// play world is current, or the test vanished. Calling the framework's
				// pump in that state is a check() crash, so record and move on instead.
				RecordFailureWithoutInfo(Queue[Index],
					TEXT("did not start (slow task in progress, play world active, or test no longer registered)"));
				++Index;
				return true;
			}
			bTestInFlight = true;
			TestStartSeconds = Now;
			return true;
		}

		if (!GIsAutomationTesting)
		{
			// Someone else (the editor's automation worker) concluded the test between
			// our ticks. Its execution info is gone; record the gap and continue.
			RecordFailureWithoutInfo(Queue[Index], TEXT("concluded externally (automation worker race)"));
			++Index;
			bTestInFlight = false;
			return true;
		}

		// true == latent queue empty and the test is complete.
		if (!Framework.ExecuteLatentCommands())
		{
			if (Now - TestStartSeconds > PerTestTimeoutSeconds)
			{
				// A hung latent chain cannot be resumed safely - close this test and end
				// the whole run rather than start the next test over a dirty queue.
				AbortInFlightTest(Framework, TEXT("timeout"));
				FinishRun(TEXT("timeout"));
				return false;
			}
			return true;
		}

		FAutomationTestExecutionInfo Info;
		const bool bPassed = Framework.StopTest(Info);
		RecordResult(Queue[Index], Info, bPassed ? TEXT("passed") : TEXT("failed"));
		++Index;
		bTestInFlight = false;
		return true;
	}

	/**
	 * Close a test that is still executing. StopTest() check()s that the latent
	 * queue is empty and that GIsAutomationTesting is set, so both must be
	 * satisfied before calling it - the same drain StartTestByName performs when
	 * it preempts a previous test.
	 */
	void AbortInFlightTest(FAutomationTestFramework& Framework, const FString& State)
	{
		if (!bTestInFlight || !Queue.IsValidIndex(Index))
		{
			return;
		}
		if (!GIsAutomationTesting)
		{
			RecordFailureWithoutInfo(Queue[Index], TEXT("concluded externally (automation worker race)"));
			bTestInFlight = false;
			return;
		}
		Framework.DequeueAllCommands();
		FAutomationTestExecutionInfo Info;
		Framework.StopTest(Info);
		RecordResult(Queue[Index], Info, State);
		bTestInFlight = false;
	}

	void RecordFailureWithoutInfo(const FQueuedTest& Test, const FString& Reason)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Test.DisplayName);
		Entry->SetStringField(TEXT("full_path"), Test.FullPath);
		Entry->SetStringField(TEXT("state"), TEXT("failed"));
		TArray<TSharedPtr<FJsonValue>> Errors;
		Errors.Add(MakeShared<FJsonValueString>(Reason));
		Entry->SetArrayField(TEXT("errors"), Errors);
		Entry->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
		Results.Add(Entry);
	}

	void RecordSkip(const FQueuedTest& Test)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Test.DisplayName);
		Entry->SetStringField(TEXT("full_path"), Test.FullPath);
		Entry->SetStringField(TEXT("state"), TEXT("skipped"));
		Entry->SetStringField(TEXT("skip_reason"), Test.SkipReason);
		Results.Add(Entry);
	}

	void RecordResult(const FQueuedTest& Test, const FAutomationTestExecutionInfo& Info, const FString& State)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Test.DisplayName);
		Entry->SetStringField(TEXT("full_path"), Test.FullPath);
		Entry->SetStringField(TEXT("state"), State);
		Entry->SetNumberField(TEXT("duration_seconds"),
			FMath::RoundToDouble(Info.Duration * 1000.0) / 1000.0);

		constexpr int32 MaxErrors = 10;
		constexpr int32 MaxWarnings = 5;
		TArray<TSharedPtr<FJsonValue>> Errors;
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FAutomationExecutionEntry& ExecEntry : Info.GetEntries())
		{
			if (ExecEntry.Event.Type == EAutomationEventType::Error && Errors.Num() < MaxErrors)
			{
				Errors.Add(MakeShared<FJsonValueString>(ExecEntry.Event.Message));
			}
			else if (ExecEntry.Event.Type == EAutomationEventType::Warning && Warnings.Num() < MaxWarnings)
			{
				Warnings.Add(MakeShared<FJsonValueString>(ExecEntry.Event.Message));
			}
		}
		Entry->SetArrayField(TEXT("errors"), Errors);
		if (Info.GetErrorTotal() > MaxErrors)
		{
			Entry->SetNumberField(TEXT("errors_total"), Info.GetErrorTotal());
		}
		Entry->SetArrayField(TEXT("warnings"), Warnings);
		Results.Add(Entry);
	}

	void FinishRun(const FString& FinalStatus)
	{
		bRunning = false;
		bTestInFlight = false;
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		int32 Passed = 0, Failed = 0, Skipped = 0;
		for (const TSharedPtr<FJsonObject>& Entry : Results)
		{
			const FString State = Entry->GetStringField(TEXT("state"));
			if (State == TEXT("passed")) { ++Passed; }
			else if (State == TEXT("skipped")) { ++Skipped; }
			else { ++Failed; }  // failed + timeout both count as failures
		}
		const int32 NotRun = Queue.Num() - Results.Num();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("status"), FinalStatus);
		Result->SetStringField(TEXT("filter"), Filter);
		Result->SetBoolField(TEXT("all_passed"), FinalStatus == TEXT("completed") && Failed == 0);
		Result->SetNumberField(TEXT("duration_seconds"),
			FMath::RoundToDouble((FPlatformTime::Seconds() - RunStartSeconds) * 10.0) / 10.0);

		TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
		Counts->SetNumberField(TEXT("total"), Queue.Num());
		Counts->SetNumberField(TEXT("passed"), Passed);
		Counts->SetNumberField(TEXT("failed"), Failed);
		Counts->SetNumberField(TEXT("skipped"), Skipped);
		Counts->SetNumberField(TEXT("not_run"), NotRun);
		Result->SetObjectField(TEXT("counts"), Counts);

		TArray<TSharedPtr<FJsonValue>> Tests;
		for (const TSharedPtr<FJsonObject>& Entry : Results)
		{
			Tests.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("tests"), Tests);

		FString Summary;
		if (FinalStatus == TEXT("completed"))
		{
			Summary = FString::Printf(TEXT("%d/%d passed (%d failed, %d skipped)."),
				Passed, Queue.Num(), Failed, Skipped);
		}
		else
		{
			Summary = FString::Printf(TEXT("Run %s after %d of %d tests (%d passed, %d failed)."),
				*FinalStatus, Results.Num(), Queue.Num(), Passed, Failed);
		}
		Result->SetStringField(TEXT("summary"), Summary);

		LastResult = Result;
		UE_LOG(LogAutomationTool, Log, TEXT("Automation run %s: %s"), *FinalStatus, *Summary);
	}

	bool bRunning = false;
	bool bTestInFlight = false;
	bool bCancelRequested = false;
	int32 Index = 0;
	double RunStartSeconds = 0.0;
	double TestStartSeconds = 0.0;
	double PerTestTimeoutSeconds = 120.0;
	double TotalTimeoutSeconds = 900.0;
	FString Filter;
	TArray<FQueuedTest> Queue;
	TArray<TSharedPtr<FJsonObject>> Results;
	TSharedPtr<FJsonObject> LastResult;
	FTSTicker::FDelegateHandle TickerHandle;
};

// ---------------------------------------------------------------------------
// Test enumeration
// ---------------------------------------------------------------------------

static bool MatchesFilter(const FAutomationTestInfo& Info, const FString& Filter)
{
	if (Filter.IsEmpty())
	{
		return true;
	}
	return Info.GetFullTestPath().Contains(Filter, ESearchCase::IgnoreCase)
		|| Info.GetDisplayName().Contains(Filter, ESearchCase::IgnoreCase);
}

static FString CommandFor(const FAutomationTestInfo& Info)
{
	const FString Parameter = Info.GetTestParameter();
	return Parameter.IsEmpty()
		? Info.GetTestName()
		: FString::Printf(TEXT("%s %s"), *Info.GetTestName(), *Parameter);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

static FString HandleList(const TMap<FString, FString>& Params)
{
	const FString Filter = ExtractParam(Params, TEXT("filter")).TrimStartAndEnd();
	const int32 Limit = FMath::Clamp(ExtractIntParam(Params, TEXT("limit"), 200), 1, 2000);

	TArray<FAutomationTestInfo> AllTests;
	FAutomationTestFramework::Get().GetValidTestNames(AllTests);

	int32 Matched = 0;
	TArray<TSharedPtr<FJsonValue>> Tests;
	for (const FAutomationTestInfo& Info : AllTests)
	{
		if (!MatchesFilter(Info, Filter))
		{
			continue;
		}
		++Matched;
		if (Tests.Num() >= Limit)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("full_path"), Info.GetFullTestPath());
		Entry->SetStringField(TEXT("name"), Info.GetDisplayName());
		Entry->SetStringField(TEXT("filter_bucket"), FilterBucketName(Info.GetTestFlags()));
		Entry->SetBoolField(TEXT("editor_context"), RunsInEditorContext(Info.GetTestFlags()));
		if (NeedsUser(Info.GetTestFlags()))
		{
			Entry->SetBoolField(TEXT("requires_user"), true);
		}
		Tests.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("total_registered"), AllTests.Num());
	Result->SetNumberField(TEXT("matched"), Matched);
	Result->SetNumberField(TEXT("returned"), Tests.Num());
	if (Matched > Tests.Num())
	{
		Result->SetStringField(TEXT("note"),
			TEXT("Match list truncated - narrow 'filter' or raise 'limit'."));
	}
	Result->SetArrayField(TEXT("tests"), Tests);
	return SerializeJson(Result);
}

static FString HandleRun(const TMap<FString, FString>& Params)
{
	// Input validity before machine state: a call with no filter is malformed
	// whatever the editor happens to be doing, and "supply a filter" is the
	// actionable answer. Reporting FRAMEWORK_BUSY for it would send the caller
	// to wait out a condition that was never the problem.
	const FString Filter = ExtractParam(Params, TEXT("filter")).TrimStartAndEnd();
	if (Filter.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_FILTER"),
			TEXT("The 'filter' parameter is required for action='run' - running every registered test at once "
				"is never intended. Use action='list' to find test paths (e.g. filter='Revvy.')."));
	}

	if (FTestRun::Get().IsRunning())
	{
		return BuildErrorResponse(TEXT("TESTS_ALREADY_RUNNING"),
			TEXT("A test run is already in flight. Poll action='status', or stop it with action='cancel'."));
	}
	if (GIsAutomationTesting)
	{
		return BuildErrorResponse(TEXT("FRAMEWORK_BUSY"),
			TEXT("The automation framework is already executing a test (Session Frontend or another runner). "
				"Wait for it to finish."));
	}
	if (GEditor && (GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor))
	{
		return BuildErrorResponse(TEXT("PIE_ACTIVE"),
			TEXT("Stop the Play In Editor session first (pie_control action='stop'). Tests that need PIE "
				"start and stop their own session."));
	}

	const int32 MaxTests = FMath::Clamp(ExtractIntParam(Params, TEXT("max_tests"), 50), 1, 500);
	const bool bIncludeNonEditor = ExtractBoolParam(Params, TEXT("include_non_editor_context"), false);
	const double PerTestTimeout = FMath::Clamp(
		(double)ExtractIntParam(Params, TEXT("timeout_seconds_per_test"), 120), 5.0, 1800.0);
	const double TotalTimeout = FMath::Clamp(
		(double)ExtractIntParam(Params, TEXT("timeout_seconds"), 900), 30.0, 7200.0);

	TArray<FAutomationTestInfo> AllTests;
	FAutomationTestFramework::Get().GetValidTestNames(AllTests);

	TArray<FQueuedTest> Queue;
	for (const FAutomationTestInfo& Info : AllTests)
	{
		if (!MatchesFilter(Info, Filter))
		{
			continue;
		}
		FQueuedTest Test;
		Test.DisplayName = Info.GetDisplayName();
		Test.FullPath = Info.GetFullTestPath();
		Test.Command = CommandFor(Info);
		if (NeedsUser(Info.GetTestFlags()))
		{
			Test.SkipReason = TEXT("requires a human user (RequiresUser flag)");
		}
		else if (!RunsInEditorContext(Info.GetTestFlags()) && !bIncludeNonEditor)
		{
			Test.SkipReason = TEXT("not flagged for editor context (pass include_non_editor_context=true to force)");
		}
		Queue.Add(MoveTemp(Test));
	}

	if (Queue.Num() == 0)
	{
		return BuildErrorResponse(TEXT("NO_TESTS_MATCHED"),
			FString::Printf(TEXT("No registered test matches filter '%s'. Use action='list' to browse."), *Filter));
	}
	if (Queue.Num() > MaxTests)
	{
		return BuildErrorResponse(TEXT("TOO_MANY_TESTS"),
			FString::Printf(TEXT("Filter '%s' matches %d tests, above the max_tests cap of %d. "
				"Narrow the filter or raise max_tests (hard cap 500)."), *Filter, Queue.Num(), MaxTests));
	}

	return FTestRun::Get().Start(MoveTemp(Queue), Filter, PerTestTimeout, TotalTimeout);
}

static FString HandleStatus()
{
	return SerializeJson(FTestRun::Get().BuildStatusJson());
}

static FString HandleLastResult()
{
	TSharedPtr<FJsonObject> Last = FTestRun::Get().GetLastResult();
	if (FTestRun::Get().IsRunning())
	{
		TSharedPtr<FJsonObject> Busy = MakeShared<FJsonObject>();
		Busy->SetBoolField(TEXT("success"), true);
		Busy->SetStringField(TEXT("status"), TEXT("running"));
		Busy->SetStringField(TEXT("note"),
			TEXT("A run is still in flight - no final result yet. Keep polling action='status'."));
		return SerializeJson(Busy);
	}
	if (!Last.IsValid())
	{
		return BuildErrorResponse(TEXT("NO_RESULT"),
			TEXT("No test run has finished yet. Start one with action='run'."));
	}
	return SerializeJson(Last);
}

static FString HandleCancel()
{
	if (!FTestRun::Get().IsRunning())
	{
		return BuildErrorResponse(TEXT("NOT_RUNNING"), TEXT("No test run is in flight."));
	}
	FTestRun::Get().RequestCancel();

	TSharedPtr<FJsonObject> Cancelled = MakeShared<FJsonObject>();
	Cancelled->SetBoolField(TEXT("success"), true);
	Cancelled->SetStringField(TEXT("status"), TEXT("cancelling"));
	Cancelled->SetStringField(TEXT("note"),
		TEXT("The run stops after the current test completes (a hung test falls to its per-test timeout). "
			"Partial results stay available via action='last_result'."));
	return SerializeJson(Cancelled);
}

static FString GetAutomationToolHelp()
{
	TSharedPtr<FJsonObject> Help = MakeShared<FJsonObject>();
	Help->SetBoolField(TEXT("success"), true);
	Help->SetStringField(TEXT("tool"), TEXT("run_automation_tests"));
	Help->SetStringField(TEXT("description"),
		TEXT("Enumerate and execute the engine's Automation Tests (the same registry Session Frontend shows) "
			"and read per-test pass/fail back as structured JSON. Runs are asynchronous: 'run' returns "
			"immediately, then poll 'status' and read 'last_result'."));

	TArray<TSharedPtr<FJsonValue>> Actions;
	auto AddAction = [&Actions](const FString& Name, const FString& Desc, const FString& ParamsDesc)
	{
		TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
		ActionObj->SetStringField(TEXT("action"), Name);
		ActionObj->SetStringField(TEXT("description"), Desc);
		ActionObj->SetStringField(TEXT("parameters"), ParamsDesc);
		Actions.Add(MakeShared<FJsonValueObject>(ActionObj));
	};
	AddAction(TEXT("list"),
		TEXT("Enumerate registered tests. Each entry: full_path, name, filter_bucket (smoke/engine/product/perf/stress/negative), editor_context."),
		TEXT("filter (substring, optional), limit (default 200)"));
	AddAction(TEXT("run"),
		TEXT("Start the tests matching 'filter' and return immediately with status='running'."),
		TEXT("filter (REQUIRED substring), max_tests (default 50), include_non_editor_context (default false), "
			"timeout_seconds_per_test (default 120), timeout_seconds (default 900)"));
	AddAction(TEXT("status"), TEXT("Progress of the in-flight run ([completed/total], current test, elapsed)."), TEXT("None"));
	AddAction(TEXT("last_result"),
		TEXT("Per-test results of the last finished run: {name, full_path, state: passed|failed|skipped|timeout, duration_seconds, errors[], warnings[]}."),
		TEXT("None"));
	AddAction(TEXT("cancel"), TEXT("Stop after the current test; partial results are kept."), TEXT("None"));
	AddAction(TEXT("help"), TEXT("Show this message."), TEXT("None"));
	Help->SetArrayField(TEXT("actions"), Actions);

	TArray<TSharedPtr<FJsonValue>> Examples;
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=list, filter=Revvy.")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=run, filter=Revvy.")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=run, filter=Project.Functional Tests., timeout_seconds=1800")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=status")));
	Examples.Add(MakeShared<FJsonValueString>(TEXT("action=last_result")));
	Help->SetArrayField(TEXT("examples"), Examples);

	TArray<TSharedPtr<FJsonValue>> Notes;
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("The Revvy plugin ships its own tests under 'Revvy.' - run them after editing plugin code.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("Tests execute inside THIS editor process and may create transient objects or open PIE; "
			"save your work before running broad filters.")));
	Notes.Add(MakeShared<FJsonValueString>(
		TEXT("One run at a time; 'run' during a run returns TESTS_ALREADY_RUNNING.")));
	Help->SetArrayField(TEXT("notes"), Notes);

	return SerializeJson(Help);
}

/** Single entry point - keeps the REGISTER macro body free of top-level commas. */
static FString ExecuteAutomationTool(const TMap<FString, FString>& Params)
{
	const FString Action = ExtractParam(Params, TEXT("action")).TrimStartAndEnd().ToLower();

	if (Action.IsEmpty())
	{
		return BuildErrorResponse(TEXT("MISSING_ACTION"),
			TEXT("The 'action' parameter is required. Use action=help for documentation."));
	}
	if (Action == TEXT("help"))
	{
		return GetAutomationToolHelp();
	}
	if (Action == TEXT("list"))
	{
		return HandleList(Params);
	}
	if (Action == TEXT("run"))
	{
		return HandleRun(Params);
	}
	if (Action == TEXT("status"))
	{
		return HandleStatus();
	}
	if (Action == TEXT("last_result") || Action == TEXT("result"))
	{
		return HandleLastResult();
	}
	if (Action == TEXT("cancel"))
	{
		return HandleCancel();
	}

	return BuildErrorResponse(TEXT("UNKNOWN_ACTION"),
		FString::Printf(TEXT("Unknown action: %s. Valid: list, run, status, last_result, cancel, help."), *Action));
}

/**
 * MCP annotations.
 *
 * Running tests executes arbitrary registered test code, which may spawn transient
 * objects or PIE sessions - a write, but one designed to clean up after itself, so
 * destructiveHint stays false. Results differ run to run, so not idempotent.
 * ReadOnlyActions keeps the query verbs reachable from a read-only MCP session.
 */
static FToolAnnotations MakeAutomationToolAnnotations()
{
	FToolAnnotations Annotations;
	Annotations.Title = TEXT("Run Automation Tests");
	Annotations.bReadOnlyHint = false;
	Annotations.bDestructiveHint = false;
	Annotations.bIdempotentHint = false;
	Annotations.bOpenWorldHint = false;
	Annotations.bAnnotated = true;
	Annotations.ReadOnlyActions = { TEXT("list"), TEXT("status"), TEXT("last_result"), TEXT("result"), TEXT("help") };
	return Annotations;
}

} // namespace RevvyAutomationTool

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

REGISTER_REVVY_TOOL_ANNOTATED(run_automation_tests,
	"Run the engine's Automation Tests (unit, functional, Revvy's own plugin tests) inside this editor and "
	"read per-test pass/fail back as structured JSON. THE way to verify editor-side changes actually work.\n"
	"\nThe run is ASYNCHRONOUS so the editor never freezes and latent tests get real frames: action='run' "
	"returns immediately with status='running'. Poll action='status' until status='idle', then read "
	"action='last_result'.\n"
	"\nACTIONS:\n"
	"  list         : enumerate registered tests (filter substring, e.g. filter='Revvy.').\n"
	"  run          : start tests matching the REQUIRED 'filter'. Returns queued/skipped counts.\n"
	"  status       : [completed/total], current test, elapsed seconds. 'idle' means finished.\n"
	"  last_result  : per-test {state: passed|failed|skipped|timeout, duration, errors[], warnings[]}.\n"
	"  cancel       : stop after the current test; partial results are kept.\n"
	"  help         : full parameter and result-shape documentation.\n"
	"\nTYPICAL LOOP:\n"
	"  1. edit code / assets\n"
	"  2. action='list', filter='Revvy.'   (discover)\n"
	"  3. action='run', filter='Revvy.'\n"
	"  4. action='status'  (repeat until idle)\n"
	"  5. action='last_result'  -> fix failures -> back to 3\n"
	"\nTests run IN-PROCESS: they may create transient objects or open their own PIE session and are "
	"expected to clean up. Non-editor-context and RequiresUser tests are skipped by default.",
	"Testing",
	RevvyAutomationTool::MakeAutomationToolAnnotations(),
	TOOL_PARAMS(
		TOOL_PARAM("action", "Operation: list, run, status, last_result, cancel, help", "string", true),
		TOOL_PARAM("filter", "[list/run] Case-insensitive substring matched against the full test path and display name. REQUIRED for run", "string", false),
		TOOL_PARAM("limit", "[list] Maximum entries to return (default 200, max 2000)", "number", false),
		TOOL_PARAM("max_tests", "[run] Refuse to start when the filter matches more than this many tests (default 50, max 500)", "number", false),
		TOOL_PARAM("include_non_editor_context", "[run] Also run tests not flagged for editor context (default false - they are recorded as skipped)", "boolean", false),
		TOOL_PARAM("timeout_seconds_per_test", "[run] Abort the run when a single test exceeds this (default 120, range 5-1800)", "number", false),
		TOOL_PARAM("timeout_seconds", "[run] Abort the whole run after this many seconds (default 900, range 30-7200)", "number", false)
	),
	{
		return RevvyAutomationTool::ExecuteAutomationTool(Params);
	}
);
