// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#pragma once

#include "CoreMinimal.h"

/** Access level asserted by the trusted local proxy for one exact request. */
enum class ERevvyBridgeAuthScope : uint8
{
    None,
    ReadOnly,
    Full
};

/**
 * Verified request material retained long enough to authenticate the response.
 * RequestBody is the exact byte sequence received from the socket, not a
 * re-serialized JSON representation.
 */
struct REVVY_API FRevvyBridgeAuthContext
{
    ERevvyBridgeAuthScope Scope = ERevvyBridgeAuthScope::None;
    FString ScopeText;
    FString BridgeInstance;
    int32 Port = 0;
    int64 Timestamp = 0;
    FString TimestampText;
    FString Nonce;
    TArray<uint8> RequestBody;

    bool IsAuthenticated() const
    {
        return Scope != ERevvyBridgeAuthScope::None;
    }
};

/** Exact-byte HMAC authentication shared by the proxy and editor bridges. */
class REVVY_API FRevvyBridgeAuth
{
public:
    /** True when any private-hop signature header is present. */
    static bool HasAnySignedRequestHeader(const TMap<FString, FString>& Headers);

    /**
     * Verify a signed proxy request. A successful result is safe to place into
     * the replay cache and later use to sign the HTTP response.
     */
    static bool TryAuthenticateRequest(
        const TMap<FString, FString>& Headers,
        const FString& BridgeHmacKey,
        const FString& ExpectedBridgeInstance,
        int32 ExpectedPort,
        const TArray<uint8>& RequestBody,
        int64 NowUnixSeconds,
        FRevvyBridgeAuthContext& OutContext,
        FString& OutError);

    /** Build the v2 request signature for an exact request byte sequence. */
    static FString SignRequest(
        const FString& BridgeHmacKey,
        const FString& Scope,
        const FString& BridgeInstance,
        int32 Port,
        const FString& Timestamp,
        const FString& Nonce,
        const TArray<uint8>& RequestBody);

    /** Build the v2 response signature bound to the verified request. */
    static FString SignResponse(
        const FString& BridgeHmacKey,
        const FRevvyBridgeAuthContext& Context,
        int32 HttpStatus,
        const TArray<uint8>& ResponseBody);

    /** Public Safe callers are restricted to a structurally exact JSON-RPC ping. */
    static bool IsPublicPing(const FString& JsonBody);
};

/** Process-local nonce cache. Nonces are consumed only after a valid HMAC. */
class REVVY_API FRevvyBridgeReplayCache
{
public:
    bool TryConsume(const FString& BridgeInstance, const FString& Nonce, int64 NowUnixSeconds);
    void Reset();

private:
    FCriticalSection Lock;
    TMap<FString, int64> SeenNonces;
};
