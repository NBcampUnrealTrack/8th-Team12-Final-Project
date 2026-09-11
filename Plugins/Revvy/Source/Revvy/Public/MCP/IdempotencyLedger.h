// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Outcome of FRevvyIdempotencyLedger::Begin. */
enum class ERevvyIdempotencyBegin : uint8
{
	/** First sighting of this key - caller should execute and then Complete/Abandon. */
	Execute,
	/** Key already completed with the same payload - OutCompletedResult carries the stored result. */
	ReplayCompleted,
	/** Key is still executing (same payload) - caller should refuse rather than double-execute. */
	ConflictPending,
	/** Key was seen with a DIFFERENT payload - a client bug; caller should refuse loudly. */
	ConflictPayload,
};

/**
 * Idempotency ledger for MCP tools/call.
 *
 * A client that retries a mutating call after a network hiccup (proxy restart,
 * SSE drop, timeout) re-executes the mutation today - spawn twice, add the row
 * twice. When the client supplies an idempotency key, this ledger makes the
 * retry safe: the first call executes and its result is stored; an identical
 * retry replays the stored result; a same-key-different-payload call is
 * refused (the HTTP-409 analogue, after ChiR24/Unreal_mcp's principal-scoped
 * ledger). Entries evict FIFO beyond capacity, completed entries first.
 *
 * Thread-safe: Begin/Complete/Abandon may be called from socket threads and
 * the game thread concurrently.
 */
class REVVY_API FRevvyIdempotencyLedger
{
public:
	explicit FRevvyIdempotencyLedger(int32 InCapacity = 4096);

	/**
	 * Register intent to execute under Key with the given payload hash.
	 * Returns what the caller must do; on ReplayCompleted, OutCompletedResult
	 * holds the stored tool result verbatim.
	 */
	ERevvyIdempotencyBegin Begin(const FString& Key, const FString& PayloadHash, FString& OutCompletedResult);

	/** Store the result of a key previously returned as Execute. */
	void Complete(const FString& Key, const FString& Result);

	/** Forget a pending key whose execution never happened (so a retry can run). */
	void Abandon(const FString& Key);

	int32 Num() const;
	int32 GetCapacity() const { return Capacity; }

private:
	struct FEntry
	{
		FString PayloadHash;
		FString Result;
		bool bCompleted = false;
	};

	void EvictIfNeeded();

	mutable FCriticalSection Lock;
	TMap<FString, FEntry> Entries;
	TArray<FString> InsertionOrder;
	int32 Capacity;
};
