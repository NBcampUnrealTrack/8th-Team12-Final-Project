// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UInputBufferService.generated.h"

/**
 * InputBuffer service exposed directly to Python for combat combo detection.
 *
 * Mirrors the UActorPoolService pattern: all UFUNCTIONs are static and
 * BlueprintCallable so they are reachable from Python as
 * `unreal.InputBufferService.<snake_case_name>(...)`.
 *
 * State model:
 *   A named-buffer registry (TMap<FString, FBuffer>) is held in the .cpp anon
 *   namespace. Each buffer tracks a Capacity cap, a WindowSeconds age cap,
 *   and an ordered list of FEntry { ActionTag, TimestampSeconds } records.
 *
 *   No FScopedTransaction is used — buffer state is runtime input history and
 *   undo/redo would corrupt the ordering invariants.
 *
 * Timestamp semantics:
 *   Timestamps are FPlatformTime::Seconds() values — monotonic seconds since
 *   process start (NOT wall-clock time, NOT world time). Callers passing their
 *   own timestamps must use the same clock. Pass TimestampSeconds <= 0 to
 *   have PushInput sample FPlatformTime::Seconds() automatically.
 *
 * Prune-on-access strategy:
 *   Entries older than (now - WindowSeconds) are only pruned during PushInput
 *   and TryMatchSequence. GetBufferSnapshot returns ALL entries unpruned so
 *   callers can inspect the raw contents for debugging.
 *
 * Threading:
 *   GameThread only. Registry access is not guarded.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.InputBufferService
 *
 *   svc.create_buffer("Player1", 8, 500.0)  # 8 entries, 500ms window
 *   svc.push_input("Player1", "Attack", 0.0)  # 0 → auto-timestamp
 *   svc.push_input("Player1", "Attack", 0.0)
 *   svc.push_input("Player1", "Special", 0.0)
 *
 *   matched, consumed = svc.try_match_sequence(
 *       "Player1", ["Attack", "Attack", "Special"])
 *   if matched:
 *       print(f"Combo triggered, consumed {consumed} inputs")
 *
 *   print(svc.get_buffer_snapshot("Player1"))
 *   svc.clear_buffer("Player1")
 */
UCLASS(BlueprintType)
class REVVY_API UInputBufferService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Register a new named buffer with the given capacity and window.
	 *
	 * @param BufferName - Unique registry key. Empty / duplicate → rejected.
	 * @param Capacity   - Max entries retained; older entries are dropped when full.
	 * @param WindowMs   - Age window in milliseconds; entries older than this are
	 *                     pruned during PushInput / TryMatchSequence.
	 * @return True on successful registration.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputBuffer|Lifecycle")
	static bool CreateBuffer(
		const FString& BufferName,
		int32 Capacity,
		float WindowMs);

	// ═══════════════════════════════════════════════════════════════════
	// Push
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append an input entry to the buffer. Prunes entries older than the window
	 * before appending, then truncates from the front if Capacity is exceeded.
	 *
	 * @param BufferName       - Registered buffer name.
	 * @param ActionTag        - Input identifier (e.g. "Attack", "Dodge", "Special").
	 * @param TimestampSeconds - FPlatformTime::Seconds() value. If <= 0, the current
	 *                           FPlatformTime::Seconds() is sampled.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputBuffer|Push")
	static bool PushInput(
		const FString& BufferName,
		const FString& ActionTag,
		double TimestampSeconds);

	// ═══════════════════════════════════════════════════════════════════
	// Match
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Attempt to match SequenceTags against the buffer in order. Prunes stale
	 * entries first. Walks the buffer from oldest to newest, advancing the
	 * sequence pointer on each matching entry. If the full sequence is matched,
	 * the matched entries are REMOVED from the buffer (consumed) and
	 * OutConsumedCount is set to the number of entries removed.
	 *
	 * On failure, OutConsumedCount is 0 and the buffer is left untouched.
	 *
	 * @param BufferName        - Registered buffer name.
	 * @param SequenceTags      - Ordered list of action tags to match.
	 * @param OutConsumedCount  - Number of buffer entries consumed on match.
	 * @return True if the full sequence matched (and was consumed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputBuffer|Match")
	static bool TryMatchSequence(
		const FString& BufferName,
		const TArray<FString>& SequenceTags,
		int32& OutConsumedCount);

	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Discard all entries in the buffer while keeping the registration alive.
	 *
	 * @param BufferName - Registered buffer name.
	 * @return True if the buffer existed and was cleared.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputBuffer|Lifecycle")
	static bool ClearBuffer(const FString& BufferName);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a JSON snapshot of the buffer:
	 *   { capacity, window_seconds, entry_count,
	 *     entries: [ { tag, timestamp, age_ms }, ... ] }
	 *
	 * Includes ALL entries regardless of age (no pruning side effect).
	 *
	 * @param BufferName - Registered buffer name.
	 * @return JSON string, or empty string if the buffer is missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|InputBuffer|Query")
	static FString GetBufferSnapshot(const FString& BufferName);
};
