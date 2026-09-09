// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UOnlineSessionService.generated.h"

/**
 * Online session service exposed directly to Python for OnlineSubsystem
 * session lifecycle basics — create, destroy, find, and join sessions.
 *
 * Mirrors the UPerformanceService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.OnlineSessionService.<snake_case_name>(...)`.
 *
 * This is a runtime-read service that dispatches asynchronous
 * OnlineSubsystem requests. Return values are true when the request is
 * accepted by the session interface; completion is reported through the
 * subsystem's delegates (not currently surfaced here).
 *
 * Internal state:
 *   - LastSearch (static TSharedPtr<FOnlineSessionSearch>) — retained
 *     between FindSessions and JoinSession so callers can reference a
 *     search result by index without re-issuing the search.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.OnlineSessionService
 *
 *   # Host
 *   svc.create_session("MyGameSession", 8, True)
 *   svc.destroy_session("MyGameSession")
 *
 *   # Client
 *   note = svc.find_sessions(20, True)   # async — results delivered later
 *   svc.join_session("MyGameSession", 0)
 *
 * @note All operations are asynchronous. A true return only indicates the
 *       request was accepted by the OnlineSubsystem session interface.
 */
UCLASS(BlueprintType)
class REVVY_API UOnlineSessionService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new online session with the given name / capacity / LAN flag.
	 *
	 * @param SessionName            Session name (converted to FName).
	 * @param NumPublicConnections   Public connection capacity.
	 * @param bLan                   True for LAN match, false for online.
	 * @return True if the create request was accepted (async completion).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|OnlineSession|Lifecycle")
	static bool CreateSession(const FString& SessionName, int32 NumPublicConnections, bool bLan);

	/**
	 * Destroy the named online session.
	 *
	 * @param SessionName  Session name to destroy.
	 * @return True if the destroy request was accepted (async completion).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|OnlineSession|Lifecycle")
	static bool DestroySession(const FString& SessionName);

	// ═══════════════════════════════════════════════════════════════════
	// Find
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Kick off an asynchronous session search. Results are retained
	 * internally as LastSearch for subsequent JoinSession by index.
	 *
	 * @param MaxResults  Maximum search results to request.
	 * @param bLan        True for LAN query, false for online.
	 * @return JSON note describing the request (async completion — results
	 *         are populated on LastSearch when the search completes).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|OnlineSession|Find")
	static FString FindSessions(int32 MaxResults, bool bLan);

	// ═══════════════════════════════════════════════════════════════════
	// Join
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Join a session by index into the most recent LastSearch results.
	 *
	 * @param SessionName         Local session name to bind on join.
	 * @param SearchResultIndex   Index into LastSearch->SearchResults.
	 * @return True if the join request was accepted (async completion).
	 *         False if there is no valid LastSearch or the index is out
	 *         of bounds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|OnlineSession|Join")
	static bool JoinSession(const FString& SessionName, int32 SearchResultIndex);
};
