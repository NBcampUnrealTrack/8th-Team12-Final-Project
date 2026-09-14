// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MCP/BridgeAuth.h"

namespace
{
    TArray<uint8> Utf8Bytes(const FString& Text)
    {
        const FTCHARToUTF8 Converter(*Text);
        TArray<uint8> Result;
        if (Converter.Length() > 0)
        {
            Result.Append(
                reinterpret_cast<const uint8*>(Converter.Get()),
                Converter.Length());
        }
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRevvyBridgeAuthGoldenVectorTest,
    "Revvy.Core.BridgeAuth.GoldenVectors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyBridgeAuthGoldenVectorTest::RunTest(const FString& Parameters)
{
    const FString Key = TEXT("protected-token-with-at-least-32-chars");
    const FString Instance = TEXT("0123456789abcdef0123456789abcdef");
    const FString Timestamp = TEXT("1786588800");
    const FString Nonce = TEXT("fedcba9876543210fedcba9876543210");
    const TArray<uint8> RequestBody = Utf8Bytes(
        TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\"}"));

    const FString RequestSignature = FRevvyBridgeAuth::SignRequest(
        Key,
        TEXT("full"),
        Instance,
        18088,
        Timestamp,
        Nonce,
        RequestBody);
    TestEqual(
        TEXT("request signature matches the cross-language golden vector"),
        RequestSignature,
        TEXT("v2=e5e3582ee7c15c4c2ab154523962645efa668759"));

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-revvy-bridge-instance"), Instance);
    Headers.Add(TEXT("x-revvy-access-scope"), TEXT("full"));
    Headers.Add(TEXT("x-revvy-request-timestamp"), Timestamp);
    Headers.Add(TEXT("x-revvy-request-nonce"), Nonce);
    Headers.Add(TEXT("x-revvy-request-signature"), RequestSignature);

    FRevvyBridgeAuthContext Context;
    FString Error;
    TestTrue(
        TEXT("golden request authenticates"),
        FRevvyBridgeAuth::TryAuthenticateRequest(
            Headers,
            Key,
            Instance,
            18088,
            RequestBody,
            1786588800,
            Context,
            Error));
    TestEqual(TEXT("authenticated scope"), Context.Scope, ERevvyBridgeAuthScope::Full);

    const TArray<uint8> ResponseBody = Utf8Bytes(
        TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"tools\":[]}}"));
    TestEqual(
        TEXT("response signature matches the cross-language golden vector"),
        FRevvyBridgeAuth::SignResponse(Key, Context, 200, ResponseBody),
        TEXT("v2=c5cceac625065b73fb1a3aaba749bb007c63790d"));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRevvyBridgeAuthPolicyTest,
    "Revvy.Core.BridgeAuth.Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyBridgeAuthPolicyTest::RunTest(const FString& Parameters)
{
    const FString Key = TEXT("protected-token-with-at-least-32-chars");
    const FString Instance = TEXT("0123456789abcdef0123456789abcdef");
    const FString Timestamp = TEXT("1786588800");
    const FString Nonce = TEXT("0123456789abcdef0123456789abcdef");
    const TArray<uint8> Body = Utf8Bytes(
        TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}"));

    TMap<FString, FString> Headers;
    Headers.Add(TEXT("x-revvy-bridge-instance"), Instance);
    Headers.Add(TEXT("x-revvy-access-scope"), TEXT("read_only"));
    Headers.Add(TEXT("x-revvy-request-timestamp"), Timestamp);
    Headers.Add(TEXT("x-revvy-request-nonce"), Nonce);
    Headers.Add(
        TEXT("x-revvy-request-signature"),
        FRevvyBridgeAuth::SignRequest(
            Key, TEXT("read_only"), Instance, 18088, Timestamp, Nonce, Body));

    FRevvyBridgeAuthContext Context;
    FString Error;
    TestTrue(
        TEXT("valid read-only request authenticates"),
        FRevvyBridgeAuth::TryAuthenticateRequest(
            Headers, Key, Instance, 18088, Body, 1786588800, Context, Error));

    TArray<uint8> ChangedBody = Body;
    ChangedBody.Add(static_cast<uint8>(' '));
    TestFalse(
        TEXT("signature is bound to exact request bytes"),
        FRevvyBridgeAuth::TryAuthenticateRequest(
            Headers, Key, Instance, 18088, ChangedBody, 1786588800, Context, Error));

    TestFalse(
        TEXT("stale timestamp is rejected"),
        FRevvyBridgeAuth::TryAuthenticateRequest(
            Headers, Key, Instance, 18088, Body, 1786588831, Context, Error));

    TMap<FString, FString> InvalidNonceHeaders = Headers;
    InvalidNonceHeaders[TEXT("x-revvy-request-nonce")] =
        TEXT("ABCDEF0123456789ABCDEF0123456789");
    TestFalse(
        TEXT("nonce must be 32 lowercase hexadecimal characters"),
        FRevvyBridgeAuth::TryAuthenticateRequest(
            InvalidNonceHeaders, Key, Instance, 18088, Body, 1786588800, Context, Error));

    FRevvyBridgeReplayCache ReplayCache;
    TestTrue(TEXT("first authenticated nonce is consumed"),
        ReplayCache.TryConsume(Instance, Nonce, 1786588800));
    TestFalse(TEXT("same nonce cannot be replayed"),
        ReplayCache.TryConsume(Instance, Nonce, 1786588801));
    ReplayCache.Reset();
    TestTrue(TEXT("listener restart resets the nonce namespace"),
        ReplayCache.TryConsume(TEXT("new-instance"), Nonce, 1786588801));

    TestTrue(TEXT("minimal JSON-RPC ping is public-safe"),
        FRevvyBridgeAuth::IsPublicPing(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}")));
    TestTrue(TEXT("ping permits empty params"),
        FRevvyBridgeAuth::IsPublicPing(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{}}")));
    TestFalse(TEXT("method name alone is not a public ping"),
        FRevvyBridgeAuth::IsPublicPing(TEXT("{\"method\":\"ping\"}")));
    TestFalse(TEXT("public ping rejects extra fields"),
        FRevvyBridgeAuth::IsPublicPing(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"admin\":true}")));
    TestFalse(TEXT("public ping rejects non-empty params"),
        FRevvyBridgeAuth::IsPublicPing(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{\"tool\":\"x\"}}")));

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
