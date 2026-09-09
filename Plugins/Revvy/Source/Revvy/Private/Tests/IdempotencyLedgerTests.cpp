// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

//
// Unit tests for FRevvyIdempotencyLedger - every Begin transition plus the
// FIFO/completed-first eviction policy. The MCPServer wiring (key extraction,
// scope prefixing, replay _meta flag) rides on these semantics.
//

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCP/IdempotencyLedger.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyIdempotencyTransitionsTest,
	"Revvy.Core.Idempotency.Transitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyIdempotencyTransitionsTest::RunTest(const FString& Parameters)
{
	FRevvyIdempotencyLedger Ledger(8);
	FString Stored;

	// First sighting executes.
	TestEqual(TEXT("fresh key executes"),
		Ledger.Begin(TEXT("k1"), TEXT("hashA"), Stored), ERevvyIdempotencyBegin::Execute);

	// Same key while pending: same payload waits, different payload is a client bug.
	TestEqual(TEXT("pending same payload conflicts as pending"),
		Ledger.Begin(TEXT("k1"), TEXT("hashA"), Stored), ERevvyIdempotencyBegin::ConflictPending);
	TestEqual(TEXT("pending different payload conflicts as payload"),
		Ledger.Begin(TEXT("k1"), TEXT("hashB"), Stored), ERevvyIdempotencyBegin::ConflictPayload);

	// Completion turns retries into replays carrying the stored result.
	Ledger.Complete(TEXT("k1"), TEXT("{\"success\":true,\"spawned\":\"Actor_7\"}"));
	TestEqual(TEXT("completed same payload replays"),
		Ledger.Begin(TEXT("k1"), TEXT("hashA"), Stored), ERevvyIdempotencyBegin::ReplayCompleted);
	TestEqual(TEXT("replay carries stored result"),
		Stored, TEXT("{\"success\":true,\"spawned\":\"Actor_7\"}"));
	TestEqual(TEXT("completed different payload still refuses"),
		Ledger.Begin(TEXT("k1"), TEXT("hashB"), Stored), ERevvyIdempotencyBegin::ConflictPayload);

	// Abandon frees a pending key for a real retry, but never a completed one.
	TestEqual(TEXT("second key executes"),
		Ledger.Begin(TEXT("k2"), TEXT("hashC"), Stored), ERevvyIdempotencyBegin::Execute);
	Ledger.Abandon(TEXT("k2"));
	TestEqual(TEXT("abandoned key executes again"),
		Ledger.Begin(TEXT("k2"), TEXT("hashC"), Stored), ERevvyIdempotencyBegin::Execute);
	Ledger.Abandon(TEXT("k1"));
	TestEqual(TEXT("abandon is a no-op on completed keys"),
		Ledger.Begin(TEXT("k1"), TEXT("hashA"), Stored), ERevvyIdempotencyBegin::ReplayCompleted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyIdempotencyEvictionTest,
	"Revvy.Core.Idempotency.Eviction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyIdempotencyEvictionTest::RunTest(const FString& Parameters)
{
	FRevvyIdempotencyLedger Ledger(3);
	FString Stored;

	// Fill: c1 completed, p1 pending, c2 completed.
	Ledger.Begin(TEXT("c1"), TEXT("h"), Stored);
	Ledger.Complete(TEXT("c1"), TEXT("r1"));
	Ledger.Begin(TEXT("p1"), TEXT("h"), Stored);
	Ledger.Begin(TEXT("c2"), TEXT("h"), Stored);
	Ledger.Complete(TEXT("c2"), TEXT("r2"));
	TestEqual(TEXT("at capacity"), Ledger.Num(), 3);

	// Overflow evicts the OLDEST COMPLETED entry (c1), never the pending one.
	Ledger.Begin(TEXT("k4"), TEXT("h"), Stored);
	TestEqual(TEXT("capacity held"), Ledger.Num(), 3);
	TestEqual(TEXT("evicted completed c1 re-executes"),
		Ledger.Begin(TEXT("c1"), TEXT("h"), Stored), ERevvyIdempotencyBegin::Execute);
	// (that Begin re-inserted c1, evicting c2 as the next completed victim)
	TestEqual(TEXT("pending p1 survived both evictions"),
		Ledger.Begin(TEXT("p1"), TEXT("h"), Stored), ERevvyIdempotencyBegin::ConflictPending);

	// All-pending ledger falls back to evicting the oldest pending entry.
	FRevvyIdempotencyLedger Pathological(2);
	Pathological.Begin(TEXT("a"), TEXT("h"), Stored);
	Pathological.Begin(TEXT("b"), TEXT("h"), Stored);
	Pathological.Begin(TEXT("c"), TEXT("h"), Stored);
	TestEqual(TEXT("pathological capacity held"), Pathological.Num(), 2);
	TestEqual(TEXT("oldest pending 'a' was evicted"),
		Pathological.Begin(TEXT("a"), TEXT("h"), Stored), ERevvyIdempotencyBegin::Execute);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
