// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "MCP/IdempotencyLedger.h"

FRevvyIdempotencyLedger::FRevvyIdempotencyLedger(int32 InCapacity)
	: Capacity(FMath::Max(InCapacity, 1))
{
}

ERevvyIdempotencyBegin FRevvyIdempotencyLedger::Begin(
	const FString& Key, const FString& PayloadHash, FString& OutCompletedResult)
{
	FScopeLock ScopeLock(&Lock);

	if (FEntry* Existing = Entries.Find(Key))
	{
		if (Existing->PayloadHash != PayloadHash)
		{
			return ERevvyIdempotencyBegin::ConflictPayload;
		}
		if (!Existing->bCompleted)
		{
			return ERevvyIdempotencyBegin::ConflictPending;
		}
		OutCompletedResult = Existing->Result;
		return ERevvyIdempotencyBegin::ReplayCompleted;
	}

	FEntry Entry;
	Entry.PayloadHash = PayloadHash;
	Entries.Add(Key, MoveTemp(Entry));
	InsertionOrder.Add(Key);
	EvictIfNeeded();
	return ERevvyIdempotencyBegin::Execute;
}

void FRevvyIdempotencyLedger::Complete(const FString& Key, const FString& Result)
{
	FScopeLock ScopeLock(&Lock);
	if (FEntry* Entry = Entries.Find(Key))
	{
		Entry->Result = Result;
		Entry->bCompleted = true;
	}
}

void FRevvyIdempotencyLedger::Abandon(const FString& Key)
{
	FScopeLock ScopeLock(&Lock);
	const FEntry* Entry = Entries.Find(Key);
	if (Entry && !Entry->bCompleted)
	{
		Entries.Remove(Key);
		InsertionOrder.Remove(Key);
	}
}

int32 FRevvyIdempotencyLedger::Num() const
{
	FScopeLock ScopeLock(&Lock);
	return Entries.Num();
}

void FRevvyIdempotencyLedger::EvictIfNeeded()
{
	// Caller holds Lock. Evict oldest completed entries first; touch pending
	// entries only when every entry is pending (a pathological client).
	while (Entries.Num() > Capacity)
	{
		int32 VictimIndex = INDEX_NONE;
		for (int32 Index = 0; Index < InsertionOrder.Num(); ++Index)
		{
			const FEntry* Entry = Entries.Find(InsertionOrder[Index]);
			if (Entry && Entry->bCompleted)
			{
				VictimIndex = Index;
				break;
			}
		}
		if (VictimIndex == INDEX_NONE)
		{
			VictimIndex = 0;
		}
		Entries.Remove(InsertionOrder[VictimIndex]);
		InsertionOrder.RemoveAt(VictimIndex);
	}
}
