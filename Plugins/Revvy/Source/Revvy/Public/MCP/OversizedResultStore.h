// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

/**
 * Holds a tool result that was too large to return in one reply, so the caller
 * can read the rest instead of losing it.
 *
 * Every MCP client caps how much a single tool result may carry. Past that cap
 * the client does not truncate - it rejects the whole reply, and the caller sees
 * "result exceeds maximum allowed tokens" where the answer should have been. The
 * work was done and paid for; only the delivery failed. Re-running the tool with
 * narrower arguments is the caller's usual recourse, but that is a guess, it
 * costs the run again, and for anything non-deterministic it does not even
 * return the same data.
 *
 * So the server keeps the full result here, hands back a bounded first slice,
 * and tells the caller the handle to read the remainder with. Nothing is
 * re-executed to finish reading an answer that already exists.
 *
 * Entries evict oldest-first past either capacity or the total character budget,
 * because this is a hand-off buffer for the next few calls, not a cache. A
 * handle that has aged out reads as unknown, which is honest: the caller can
 * re-run the tool.
 *
 * Handles are random (not sequential) and every read is checked against the
 * access scope that stored them, so a Safe/read-only session can neither guess
 * nor be handed the overflow of a Full-access session's call.
 *
 * Thread-safe: Store/Read may be called from socket threads and the game thread
 * concurrently.
 */
class REVVY_API FRevvyOversizedResultStore
{
public:
	explicit FRevvyOversizedResultStore(int32 InCapacity = 8, int64 InCharBudget = 4 * 1024 * 1024);

	/** Keep Result for later reads under AccessScope; returns the handle to read it with. */
	FString Store(int32 AccessScope, const FString& Result);

	/**
	 * Copy up to Count characters from Offset of a stored result.
	 *
	 * Returns false when the handle is unknown, has been evicted, was stored
	 * under a different access scope, or Offset is past the end. Offset at
	 * exactly the end succeeds with an empty chunk - that is how a caller
	 * paging forward learns it is finished.
	 */
	bool Read(
		int32 AccessScope,
		const FString& Handle,
		int32 Offset,
		int32 Count,
		FString& OutChunk,
		int32& OutTotalChars) const;

	int32 Num() const;
	int32 GetCapacity() const { return Capacity; }

private:
	struct FEntry
	{
		FString Handle;
		int32 AccessScope = 0;
		FString Payload;
	};

	/** Caller holds Lock. Drops oldest entries until both budgets are satisfied. */
	void EvictLocked();

	mutable FCriticalSection Lock;
	/** Oldest first, so eviction pops the front. */
	TArray<FEntry> Entries;
	int32 Capacity;
	int64 CharBudget;
	int64 StoredChars = 0;
};

/** Turning an over-budget tool reply into something the caller can actually read. */
namespace RevvyOversizedResult
{
	/**
	 * Largest reply worth sending whole.
	 *
	 * Matched to the same ceiling PythonTools budgets against: 24k leaves room
	 * for the JSON-RPC envelope inside a typical 25k-ish client tool budget.
	 */
	REVVY_API int32 GetToolReplyBudgetChars();

	/** The process-wide store used by the MCP server. */
	REVVY_API FRevvyOversizedResultStore& GetStore();

	/** Name of the server-served tool that reads on from a bounded reply. */
	REVVY_API const TCHAR* GetPageToolName();

	/** Description and input schema for that tool, as tools/list advertises it. */
	REVVY_API FString GetPageToolDescription();

	/**
	 * Return Result unchanged when it fits, or store it and return a bounded
	 * first slice prefixed with the handle to read the remainder with.
	 */
	REVVY_API FString BoundToolReply(
		const FString& Result,
		int32 AccessScope,
		FRevvyOversizedResultStore& Store);

	/** As above, against the process-wide store. */
	REVVY_API FString BoundToolReply(const FString& Result, int32 AccessScope);

	/**
	 * The reply for a result-page read: the requested slice with its range and
	 * the next offset, or a JSON error when the handle is unknown or expired.
	 */
	REVVY_API FString BuildPageReply(
		int32 AccessScope,
		const FString& Handle,
		int32 Offset,
		FRevvyOversizedResultStore& Store);

	/** As above, against the process-wide store. */
	REVVY_API FString BuildPageReply(int32 AccessScope, const FString& Handle, int32 Offset);
}
