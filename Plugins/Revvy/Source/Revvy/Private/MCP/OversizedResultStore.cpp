// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "MCP/OversizedResultStore.h"

#include "Misc/Guid.h"

FRevvyOversizedResultStore::FRevvyOversizedResultStore(int32 InCapacity, int64 InCharBudget)
	: Capacity(FMath::Max(1, InCapacity))
	, CharBudget(FMath::Max<int64>(1, InCharBudget))
{
}

FString FRevvyOversizedResultStore::Store(int32 AccessScope, const FString& Result)
{
	// Random rather than sequential: a handle is a capability, and a caller must
	// not be able to reach another session's overflow by counting.
	const FString Handle = FString::Printf(
		TEXT("res-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));

	FScopeLock ScopeLock(&Lock);

	FEntry Entry;
	Entry.Handle = Handle;
	Entry.AccessScope = AccessScope;
	Entry.Payload = Result;
	StoredChars += Result.Len();
	Entries.Add(MoveTemp(Entry));
	EvictLocked();

	return Handle;
}

bool FRevvyOversizedResultStore::Read(
	int32 AccessScope,
	const FString& Handle,
	int32 Offset,
	int32 Count,
	FString& OutChunk,
	int32& OutTotalChars) const
{
	OutChunk.Reset();
	OutTotalChars = 0;

	if (Handle.IsEmpty() || Offset < 0 || Count < 0)
	{
		return false;
	}

	FScopeLock ScopeLock(&Lock);

	const FEntry* Found = Entries.FindByPredicate(
		[&Handle](const FEntry& Entry) { return Entry.Handle == Handle; });
	// A scope mismatch reads exactly like an unknown handle on purpose: the
	// answer must not tell a Safe session that a Full session's result exists.
	if (!Found || Found->AccessScope != AccessScope)
	{
		return false;
	}

	const int32 Total = Found->Payload.Len();
	OutTotalChars = Total;
	if (Offset > Total)
	{
		return false;
	}

	OutChunk = Found->Payload.Mid(Offset, FMath::Min(Count, Total - Offset));
	return true;
}

int32 FRevvyOversizedResultStore::Num() const
{
	FScopeLock ScopeLock(&Lock);
	return Entries.Num();
}

void FRevvyOversizedResultStore::EvictLocked()
{
	// The newest entry always survives, even alone over budget: the caller is
	// about to be handed its handle, and a store that evicts what it just
	// promised is worse than one that briefly exceeds its own ceiling.
	while (Entries.Num() > 1 && (Entries.Num() > Capacity || StoredChars > CharBudget))
	{
		StoredChars -= Entries[0].Payload.Len();
		Entries.RemoveAt(0);
	}
}

namespace RevvyOversizedResult
{
	namespace
	{
		/** Room set aside for the explanatory prefix, so prefix + slice still fits. */
		constexpr int32 PrefixReserveChars = 512;

		int32 SliceLength()
		{
			return FMath::Max(1, GetToolReplyBudgetChars() - PrefixReserveChars);
		}

		FString JsonError(const FString& Message)
		{
			// Escaping is unnecessary here - every message below is literal ASCII
			// with no quotes or backslashes - but the shape must stay the one the
			// server's error classifier reads.
			return FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"), *Message);
		}
	}

	int32 GetToolReplyBudgetChars()
	{
		return 24000;
	}

	const TCHAR* GetPageToolName()
	{
		return TEXT("revvy_result_page");
	}

	FString GetPageToolDescription()
	{
		return TEXT(
			"Read the rest of a tool result that was too large to return in one reply. "
			"When a result is bounded you get a handle and the offset to continue from; "
			"pass them back here to read on. The result is already computed and held for "
			"the next few calls, so this re-runs nothing. A handle that has expired reads "
			"as unknown - run the original tool again to get a fresh one.");
	}

	FRevvyOversizedResultStore& GetStore()
	{
		static FRevvyOversizedResultStore Store;
		return Store;
	}

	FString BoundToolReply(const FString& Result, int32 AccessScope, FRevvyOversizedResultStore& Store)
	{
		if (Result.Len() <= GetToolReplyBudgetChars())
		{
			return Result;
		}

		const int32 ChunkLen = FMath::Min(SliceLength(), Result.Len());
		const FString Handle = Store.Store(AccessScope, Result);

		// Addressed to a machine that must decide what to do next, so it states
		// the fragment is not self-contained JSON before the fragment arrives,
		// and gives the exact call rather than describing one.
		return FString::Printf(
			TEXT("[revvy] This result is %d characters, more than can be returned in one reply.\n")
			TEXT("[revvy] Below is characters 0-%d of it, verbatim - a fragment, so it will not parse as JSON on its own.\n")
			TEXT("[revvy] Read the next part with: %s {\"handle\": \"%s\", \"offset\": %d}\n")
			TEXT("[revvy] The whole result is already computed and held; reading on re-runs nothing.\n\n%s"),
			Result.Len(),
			ChunkLen - 1,
			GetPageToolName(),
			*Handle,
			ChunkLen,
			*Result.Mid(0, ChunkLen));
	}

	FString BoundToolReply(const FString& Result, int32 AccessScope)
	{
		return BoundToolReply(Result, AccessScope, GetStore());
	}

	FString BuildPageReply(
		int32 AccessScope,
		const FString& Handle,
		int32 Offset,
		FRevvyOversizedResultStore& Store)
	{
		FString Chunk;
		int32 Total = 0;
		if (!Store.Read(AccessScope, Handle, Offset, SliceLength(), Chunk, Total))
		{
			// One message for unknown, expired, wrong-scope and past-the-end, and
			// it names the recourse: a handle is a short-lived hand-off, and the
			// caller's move in every one of those cases is to run the tool again.
			return JsonError(
				TEXT("Unknown, expired, or out-of-range result handle. Run the original tool again to get a fresh one."));
		}

		const int32 NextOffset = Offset + Chunk.Len();
		if (NextOffset >= Total)
		{
			return FString::Printf(
				TEXT("[revvy] Characters %d-%d of %d for handle %s. This is the end of the result.\n\n%s"),
				Offset,
				FMath::Max(Offset, NextOffset - 1),
				Total,
				*Handle,
				*Chunk);
		}

		return FString::Printf(
			TEXT("[revvy] Characters %d-%d of %d for handle %s.\n")
			TEXT("[revvy] Read the next part with: %s {\"handle\": \"%s\", \"offset\": %d}\n\n%s"),
			Offset,
			NextOffset - 1,
			Total,
			*Handle,
			GetPageToolName(),
			*Handle,
			NextOffset,
			*Chunk);
	}

	FString BuildPageReply(int32 AccessScope, const FString& Handle, int32 Offset)
	{
		return BuildPageReply(AccessScope, Handle, Offset, GetStore());
	}
}
