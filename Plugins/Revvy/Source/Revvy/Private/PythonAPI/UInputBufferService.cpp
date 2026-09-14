// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UInputBufferService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyInputBuffer, Log, All);

#include "HAL/PlatformTime.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Static state (anonymous namespace — hidden from other TUs)
// =================================================================

namespace
{
	/** One recorded input event. */
	struct FEntry
	{
		FString ActionTag;
		double  TimestampSeconds = 0.0;
	};

	/** Per-buffer bookkeeping. */
	struct FBuffer
	{
		int32          Capacity = 0;
		float          WindowSeconds = 0.f;
		TArray<FEntry> Entries;
	};

	/** Named-buffer registry, keyed by user-supplied buffer name. */
	static TMap<FString, FBuffer> Buffers;

	/** Emit a Warning if not on the GameThread — registry access is not thread-safe. */
	void InputBufferWarnIfNotGameThread(const TCHAR* Op)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogRevvyInputBuffer, Warning,
				TEXT("UInputBufferService::%s called off GameThread — registry access is not thread-safe."),
				Op);
		}
	}

	/** Drop entries older than (Now - WindowSeconds). Assumes Entries are chronological. */
	void PruneOldEntries(FBuffer& Buffer, double NowSeconds)
	{
		if (Buffer.WindowSeconds <= 0.f || Buffer.Entries.Num() == 0)
		{
			return;
		}

		const double CutoffSeconds = NowSeconds - static_cast<double>(Buffer.WindowSeconds);
		Buffer.Entries.RemoveAll([CutoffSeconds](const FEntry& E)
		{
			return E.TimestampSeconds < CutoffSeconds;
		});
	}

	/** Serialize a JSON object to a compact string. */
	FString InputBufferJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}
}

// =================================================================
// Lifecycle
// =================================================================

bool UInputBufferService::CreateBuffer(
	const FString& BufferName,
	int32 Capacity,
	float WindowMs)
{
	InputBufferWarnIfNotGameThread(TEXT("CreateBuffer"));

	if (BufferName.IsEmpty())
	{
		UE_LOG(LogRevvyInputBuffer, Warning, TEXT("CreateBuffer: BufferName must be non-empty."));
		return false;
	}

	if (Buffers.Contains(BufferName))
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("CreateBuffer: buffer '%s' is already registered."), *BufferName);
		return false;
	}

	if (Capacity < 1)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("CreateBuffer: Capacity must be >= 1 (got %d)."), Capacity);
		return false;
	}

	if (WindowMs <= 0.f)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("CreateBuffer: WindowMs must be > 0 (got %f)."), WindowMs);
		return false;
	}

	FBuffer NewBuffer;
	NewBuffer.Capacity      = Capacity;
	NewBuffer.WindowSeconds = WindowMs / 1000.f;

	Buffers.Add(BufferName, MoveTemp(NewBuffer));

	UE_LOG(LogRevvyInputBuffer, Log,
		TEXT("CreateBuffer: '%s' registered (capacity=%d, window_ms=%.2f)."),
		*BufferName, Capacity, WindowMs);
	return true;
}

// =================================================================
// Push
// =================================================================

bool UInputBufferService::PushInput(
	const FString& BufferName,
	const FString& ActionTag,
	double TimestampSeconds)
{
	InputBufferWarnIfNotGameThread(TEXT("PushInput"));

	FBuffer* Buffer = Buffers.Find(BufferName);
	if (!Buffer)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("PushInput: buffer '%s' is not registered."), *BufferName);
		return false;
	}

	if (TimestampSeconds <= 0.0)
	{
		TimestampSeconds = FPlatformTime::Seconds();
	}

	// Prune stale entries first so the incoming entry never displaces
	// something that was already expired.
	PruneOldEntries(*Buffer, TimestampSeconds);

	FEntry NewEntry;
	NewEntry.ActionTag        = ActionTag;
	NewEntry.TimestampSeconds = TimestampSeconds;
	Buffer->Entries.Add(MoveTemp(NewEntry));

	// Truncate from the front (oldest) if we blew past capacity.
	if (Buffer->Capacity > 0 && Buffer->Entries.Num() > Buffer->Capacity)
	{
		const int32 Excess = Buffer->Entries.Num() - Buffer->Capacity;
		Buffer->Entries.RemoveAt(0, Excess, EAllowShrinking::No);
	}

	UE_LOG(LogRevvyInputBuffer, Verbose,
		TEXT("PushInput: buffer '%s' <- '%s' @ %.4f (count=%d)."),
		*BufferName, *ActionTag, TimestampSeconds, Buffer->Entries.Num());
	return true;
}

// =================================================================
// Match
// =================================================================

bool UInputBufferService::TryMatchSequence(
	const FString& BufferName,
	const TArray<FString>& SequenceTags,
	int32& OutConsumedCount)
{
	InputBufferWarnIfNotGameThread(TEXT("TryMatchSequence"));

	OutConsumedCount = 0;

	FBuffer* Buffer = Buffers.Find(BufferName);
	if (!Buffer)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("TryMatchSequence: buffer '%s' is not registered."), *BufferName);
		return false;
	}

	if (SequenceTags.Num() == 0)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("TryMatchSequence: SequenceTags is empty for buffer '%s'."), *BufferName);
		return false;
	}

	// Prune expired entries relative to now so a stale entry doesn't count
	// toward the combo.
	const double NowSeconds = FPlatformTime::Seconds();
	PruneOldEntries(*Buffer, NowSeconds);

	if (Buffer->Entries.Num() < SequenceTags.Num())
	{
		return false;
	}

	// Walk oldest → newest, advance the sequence pointer on each match. Record
	// the buffer index of each consumed entry so we can remove them on success.
	TArray<int32> ConsumedIndices;
	ConsumedIndices.Reserve(SequenceTags.Num());

	int32 SeqIdx = 0;
	for (int32 i = 0; i < Buffer->Entries.Num() && SeqIdx < SequenceTags.Num(); ++i)
	{
		if (Buffer->Entries[i].ActionTag == SequenceTags[SeqIdx])
		{
			ConsumedIndices.Add(i);
			++SeqIdx;
		}
	}

	if (SeqIdx < SequenceTags.Num())
	{
		return false;
	}

	// Remove consumed entries. Walk high → low so indices stay valid.
	for (int32 j = ConsumedIndices.Num() - 1; j >= 0; --j)
	{
		Buffer->Entries.RemoveAt(ConsumedIndices[j], 1, EAllowShrinking::No);
	}

	OutConsumedCount = ConsumedIndices.Num();

	UE_LOG(LogRevvyInputBuffer, Log,
		TEXT("TryMatchSequence: buffer '%s' matched sequence of %d (remaining=%d)."),
		*BufferName, OutConsumedCount, Buffer->Entries.Num());
	return true;
}

// =================================================================
// Lifecycle
// =================================================================

bool UInputBufferService::ClearBuffer(const FString& BufferName)
{
	InputBufferWarnIfNotGameThread(TEXT("ClearBuffer"));

	FBuffer* Buffer = Buffers.Find(BufferName);
	if (!Buffer)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("ClearBuffer: buffer '%s' is not registered."), *BufferName);
		return false;
	}

	const int32 Cleared = Buffer->Entries.Num();
	Buffer->Entries.Reset();

	UE_LOG(LogRevvyInputBuffer, Log,
		TEXT("ClearBuffer: '%s' cleared (%d entries dropped)."),
		*BufferName, Cleared);
	return true;
}

// =================================================================
// Query
// =================================================================

FString UInputBufferService::GetBufferSnapshot(const FString& BufferName)
{
	InputBufferWarnIfNotGameThread(TEXT("GetBufferSnapshot"));

	FBuffer* Buffer = Buffers.Find(BufferName);
	if (!Buffer)
	{
		UE_LOG(LogRevvyInputBuffer, Warning,
			TEXT("GetBufferSnapshot: buffer '%s' is not registered."), *BufferName);
		return FString();
	}

	// Explicitly DO NOT prune here — Get is a read-only observer.
	const double NowSeconds = FPlatformTime::Seconds();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("capacity"), Buffer->Capacity);
	Root->SetNumberField(TEXT("window_seconds"), Buffer->WindowSeconds);
	Root->SetNumberField(TEXT("entry_count"), Buffer->Entries.Num());

	TArray<TSharedPtr<FJsonValue>> EntryValues;
	EntryValues.Reserve(Buffer->Entries.Num());
	for (const FEntry& Entry : Buffer->Entries)
	{
		TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetStringField(TEXT("tag"), Entry.ActionTag);
		EntryObj->SetNumberField(TEXT("timestamp"), Entry.TimestampSeconds);
		EntryObj->SetNumberField(TEXT("age_ms"),
			(NowSeconds - Entry.TimestampSeconds) * 1000.0);
		EntryValues.Add(MakeShared<FJsonValueObject>(EntryObj));
	}
	Root->SetArrayField(TEXT("entries"), EntryValues);

	return InputBufferJsonObjectToString(Root);
}
