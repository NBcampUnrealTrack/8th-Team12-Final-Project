// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).
//
// Every MCP client caps a single tool result, and past that cap it rejects the
// whole reply rather than trimming it — the caller sees "result exceeds maximum
// allowed tokens" where the answer should be, after the work was already done.
// The server now bounds the reply and holds the rest behind a handle. These pin
// the parts that make that safe to rely on: a reply that fits is not touched, a
// bounded one says exactly how to read on, reading on returns the original
// characters and nothing else, and a handle never crosses an access scope.

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/OversizedResultStore.h"
#include "Misc/AutomationTest.h"

namespace RevvyOversizedResultTests
{
	/** Scope ids as EMCPAccessScope orders them; the store only compares them. */
	constexpr int32 ScopeReadOnly = 1;
	constexpr int32 ScopeFull = 2;

	FString MakeResult(int32 Chars)
	{
		FString Out;
		Out.Reserve(Chars);
		while (Out.Len() < Chars)
		{
			Out.Append(TEXT("0123456789"));
		}
		Out.LeftInline(Chars);
		return Out;
	}

	/** The handle printed in a bounded reply, so a test can read on the way a caller would. */
	FString ExtractHandle(const FString& Reply)
	{
		const FString Marker = TEXT("\"handle\": \"");
		const int32 Start = Reply.Find(Marker);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 ValueStart = Start + Marker.Len();
		const int32 End = Reply.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		return End == INDEX_NONE ? FString() : Reply.Mid(ValueStart, End - ValueStart);
	}

	/** Everything after the explanatory prefix — the tool's own characters. */
	FString ExtractBody(const FString& Reply)
	{
		const int32 Split = Reply.Find(TEXT("\n\n"));
		return Split == INDEX_NONE ? Reply : Reply.Mid(Split + 2);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyOversizedResultLeavesFittingRepliesAloneTest,
	"Revvy.MCP.OversizedResult.LeavesFittingRepliesAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyOversizedResultLeavesFittingRepliesAloneTest::RunTest(const FString&)
{
	// The overwhelming majority of calls are under the ceiling and must come back
	// byte-for-byte, with nothing stored: this is a backstop, not a pipeline stage.
	FRevvyOversizedResultStore Store;
	const FString Result = RevvyOversizedResultTests::MakeResult(
		RevvyOversizedResult::GetToolReplyBudgetChars());

	const FString Reply = RevvyOversizedResult::BoundToolReply(
		Result, RevvyOversizedResultTests::ScopeFull, Store);

	TestEqual(TEXT("returned verbatim"), Reply, Result);
	TestEqual(TEXT("nothing stored"), Store.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyOversizedResultBoundsAndExplainsTest,
	"Revvy.MCP.OversizedResult.BoundsAndExplains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyOversizedResultBoundsAndExplainsTest::RunTest(const FString&)
{
	// 61,030 characters is the real discover_python_class reply that started this.
	FRevvyOversizedResultStore Store;
	const FString Result = RevvyOversizedResultTests::MakeResult(61030);

	const FString Reply = RevvyOversizedResult::BoundToolReply(
		Result, RevvyOversizedResultTests::ScopeFull, Store);

	// Fitting under the ceiling is the whole point — a bounded reply the client
	// still rejects would have achieved nothing.
	TestTrue(TEXT("fits the budget"), Reply.Len() <= RevvyOversizedResult::GetToolReplyBudgetChars());
	TestTrue(TEXT("names the continuation tool"), Reply.Contains(RevvyOversizedResult::GetPageToolName()));
	TestTrue(TEXT("warns the fragment is partial"), Reply.Contains(TEXT("fragment")));
	TestEqual(TEXT("held for reading on"), Store.Num(), 1);

	const FString Handle = RevvyOversizedResultTests::ExtractHandle(Reply);
	TestFalse(TEXT("handle printed"), Handle.IsEmpty());
	// A guessable handle would be a way into another session's overflow.
	TestFalse(TEXT("handle is not a counter"), Handle == TEXT("res-1"));

	const FString Body = RevvyOversizedResultTests::ExtractBody(Reply);
	TestTrue(TEXT("body is the head of the result"), Result.StartsWith(Body));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyOversizedResultReadsBackWholeTest,
	"Revvy.MCP.OversizedResult.ReadsBackWhole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyOversizedResultReadsBackWholeTest::RunTest(const FString&)
{
	// Paging from the offsets the replies print must reassemble the original
	// exactly. Anything less and the caller is better off re-running the tool,
	// which is the behaviour this exists to replace.
	FRevvyOversizedResultStore Store;
	const FString Result = RevvyOversizedResultTests::MakeResult(61030);
	const FString First = RevvyOversizedResult::BoundToolReply(
		Result, RevvyOversizedResultTests::ScopeFull, Store);
	const FString Handle = RevvyOversizedResultTests::ExtractHandle(First);

	FString Assembled = RevvyOversizedResultTests::ExtractBody(First);
	int32 Guard = 0;
	while (Assembled.Len() < Result.Len() && Guard++ < 32)
	{
		const FString Page = RevvyOversizedResult::BuildPageReply(
			RevvyOversizedResultTests::ScopeFull, Handle, Assembled.Len(), Store);
		TestFalse(TEXT("page is not an error"), Page.StartsWith(TEXT("{\"success\":false")));
		TestTrue(TEXT("page fits the budget"),
			Page.Len() <= RevvyOversizedResult::GetToolReplyBudgetChars());
		Assembled.Append(RevvyOversizedResultTests::ExtractBody(Page));
	}

	TestEqual(TEXT("reassembles the original"), Assembled, Result);

	// The last page has to say so, or a caller cannot tell finished from stalled.
	const FString Final = RevvyOversizedResult::BuildPageReply(
		RevvyOversizedResultTests::ScopeFull, Handle, Result.Len() - 10, Store);
	TestTrue(TEXT("final page says it is the end"), Final.Contains(TEXT("end of the result")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyOversizedResultRefusesForeignScopeTest,
	"Revvy.MCP.OversizedResult.RefusesForeignScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyOversizedResultRefusesForeignScopeTest::RunTest(const FString&)
{
	// A Safe/read-only session holding a Full session's handle must get nothing —
	// and must not be told the handle exists, which would itself be a disclosure.
	FRevvyOversizedResultStore Store;
	const FString Handle = Store.Store(
		RevvyOversizedResultTests::ScopeFull, RevvyOversizedResultTests::MakeResult(61030));

	FString Chunk;
	int32 Total = -1;
	const bool bRead = Store.Read(RevvyOversizedResultTests::ScopeReadOnly, Handle, 0, 100, Chunk, Total);

	TestFalse(TEXT("refused"), bRead);
	TestTrue(TEXT("no characters leaked"), Chunk.IsEmpty());
	TestEqual(TEXT("size not disclosed"), Total, 0);

	const FString Reply = RevvyOversizedResult::BuildPageReply(
		RevvyOversizedResultTests::ScopeReadOnly, Handle, 0, Store);
	TestTrue(TEXT("reads as unknown"), Reply.Contains(TEXT("Unknown, expired, or out-of-range")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyOversizedResultEvictsOldestTest,
	"Revvy.MCP.OversizedResult.EvictsOldest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyOversizedResultEvictsOldestTest::RunTest(const FString&)
{
	// A hand-off buffer, not a cache: it must not grow without bound, and the
	// handle just issued must always still be readable.
	FRevvyOversizedResultStore Store(2);
	const FString Oldest = Store.Store(RevvyOversizedResultTests::ScopeFull, TEXT("first"));
	Store.Store(RevvyOversizedResultTests::ScopeFull, TEXT("second"));
	const FString Newest = Store.Store(RevvyOversizedResultTests::ScopeFull, TEXT("third"));

	TestEqual(TEXT("bounded to capacity"), Store.Num(), 2);

	FString Chunk;
	int32 Total = 0;
	TestFalse(TEXT("oldest evicted"),
		Store.Read(RevvyOversizedResultTests::ScopeFull, Oldest, 0, 100, Chunk, Total));
	TestTrue(TEXT("newest still readable"),
		Store.Read(RevvyOversizedResultTests::ScopeFull, Newest, 0, 100, Chunk, Total));
	TestEqual(TEXT("newest intact"), Chunk, FString(TEXT("third")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
