// Copyright RevStudio 2026. Based on VibeUE by Kevin Buckley (MIT).

#include "MCP/BridgeAuth.h"

#include "Dom/JsonObject.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    constexpr int64 AllowedClockSkewSeconds = 30;
    constexpr int64 ReplayRetentionSeconds = 60;

    const TCHAR* InstanceHeader = TEXT("x-revvy-bridge-instance");
    const TCHAR* ScopeHeader = TEXT("x-revvy-access-scope");
    const TCHAR* TimestampHeader = TEXT("x-revvy-request-timestamp");
    const TCHAR* NonceHeader = TEXT("x-revvy-request-nonce");
    const TCHAR* SignatureHeader = TEXT("x-revvy-request-signature");

    void AppendUtf8(TArray<uint8>& Destination, const FString& Text)
    {
        const FTCHARToUTF8 Converter(*Text);
        if (Converter.Length() > 0)
        {
            Destination.Append(
                reinterpret_cast<const uint8*>(Converter.Get()),
                Converter.Length());
        }
    }

    FString HmacSha1(const FString& Key, const TArray<uint8>& Canonical)
    {
        const FTCHARToUTF8 KeyUtf8(*Key);
        uint8 Hash[20] = {};
        FSHA1::HMACBuffer(
            KeyUtf8.Get(),
            static_cast<uint32>(KeyUtf8.Length()),
            Canonical.GetData(),
            static_cast<uint64>(Canonical.Num()),
            Hash);
        return TEXT("v2=") + BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
    }

    bool ConstantTimeEqualsAscii(const FString& Left, const FString& Right)
    {
        const int32 LeftLength = Left.Len();
        const int32 RightLength = Right.Len();
        uint32 Difference = static_cast<uint32>(LeftLength) ^ static_cast<uint32>(RightLength);
        const int32 MaximumLength = FMath::Max(LeftLength, RightLength);
        for (int32 Index = 0; Index < MaximumLength; ++Index)
        {
            const uint32 LeftCharacter = Index < LeftLength
                ? static_cast<uint32>(Left[Index])
                : 0u;
            const uint32 RightCharacter = Index < RightLength
                ? static_cast<uint32>(Right[Index])
                : 0u;
            Difference |= LeftCharacter ^ RightCharacter;
        }
        return Difference == 0;
    }

    bool IsLowerHex(const FString& Value, int32 ExpectedLength)
    {
        if (Value.Len() != ExpectedLength)
        {
            return false;
        }
        for (const TCHAR Character : Value)
        {
            if (!((Character >= TEXT('0') && Character <= TEXT('9'))
                || (Character >= TEXT('a') && Character <= TEXT('f'))))
            {
                return false;
            }
        }
        return true;
    }

    TArray<uint8> BuildRequestCanonical(
        const FString& Scope,
        const FString& BridgeInstance,
        int32 Port,
        const FString& Timestamp,
        const FString& Nonce,
        const TArray<uint8>& RequestBody)
    {
        FString Prefix = TEXT("revvy-proxy-v2\n");
        Prefix += Scope;
        Prefix += TEXT("\n");
        Prefix += BridgeInstance;
        Prefix += TEXT("\n");
        Prefix += LexToString(Port);
        Prefix += TEXT("\n");
        Prefix += Timestamp;
        Prefix += TEXT("\n");
        Prefix += Nonce;
        Prefix += TEXT("\n");
        Prefix += LexToString(RequestBody.Num());
        Prefix += TEXT("\n");

        TArray<uint8> Canonical;
        Canonical.Reserve(Prefix.Len() + RequestBody.Num());
        AppendUtf8(Canonical, Prefix);
        Canonical.Append(RequestBody);
        return Canonical;
    }
}

bool FRevvyBridgeAuth::HasAnySignedRequestHeader(const TMap<FString, FString>& Headers)
{
    return Headers.Contains(InstanceHeader)
        || Headers.Contains(ScopeHeader)
        || Headers.Contains(TimestampHeader)
        || Headers.Contains(NonceHeader)
        || Headers.Contains(SignatureHeader);
}

bool FRevvyBridgeAuth::TryAuthenticateRequest(
    const TMap<FString, FString>& Headers,
    const FString& BridgeHmacKey,
    const FString& ExpectedBridgeInstance,
    int32 ExpectedPort,
    const TArray<uint8>& RequestBody,
    int64 NowUnixSeconds,
    FRevvyBridgeAuthContext& OutContext,
    FString& OutError)
{
    OutContext = FRevvyBridgeAuthContext();
    OutError.Empty();

    const FString* Scope = Headers.Find(ScopeHeader);
    const FString* Instance = Headers.Find(InstanceHeader);
    const FString* Timestamp = Headers.Find(TimestampHeader);
    const FString* Nonce = Headers.Find(NonceHeader);
    const FString* PresentedSignature = Headers.Find(SignatureHeader);
    if (Scope == nullptr || Instance == nullptr || Timestamp == nullptr
        || Nonce == nullptr || PresentedSignature == nullptr)
    {
        OutError = TEXT("missing signed proxy request headers");
        return false;
    }

    ERevvyBridgeAuthScope ParsedScope = ERevvyBridgeAuthScope::None;
    if (*Scope == TEXT("read_only"))
    {
        ParsedScope = ERevvyBridgeAuthScope::ReadOnly;
    }
    else if (*Scope == TEXT("full"))
    {
        ParsedScope = ERevvyBridgeAuthScope::Full;
    }
    else
    {
        OutError = TEXT("invalid proxy access scope");
        return false;
    }

    if (ExpectedPort < 1 || ExpectedPort > 65535
        || Instance->IsEmpty()
        || !ConstantTimeEqualsAscii(*Instance, ExpectedBridgeInstance))
    {
        OutError = TEXT("proxy bridge identity does not match this listener");
        return false;
    }

    int64 TimestampSeconds = 0;
    if (!LexTryParseString(TimestampSeconds, **Timestamp)
        || *Timestamp != LexToString(TimestampSeconds))
    {
        OutError = TEXT("invalid proxy request timestamp");
        return false;
    }
    if (TimestampSeconds < NowUnixSeconds - AllowedClockSkewSeconds
        || TimestampSeconds > NowUnixSeconds + AllowedClockSkewSeconds)
    {
        OutError = TEXT("expired proxy request timestamp");
        return false;
    }

    if (!IsLowerHex(*Nonce, 32))
    {
        OutError = TEXT("invalid proxy request nonce");
        return false;
    }
    if (PresentedSignature->Len() != 43
        || !PresentedSignature->StartsWith(TEXT("v2="), ESearchCase::CaseSensitive)
        || !IsLowerHex(PresentedSignature->Mid(3), 40))
    {
        OutError = TEXT("invalid or missing proxy request signature");
        return false;
    }
    if (BridgeHmacKey.Len() < 32)
    {
        OutError = TEXT("protected bridge authentication key is unavailable");
        return false;
    }

    const FString ExpectedSignature = SignRequest(
        BridgeHmacKey,
        *Scope,
        *Instance,
        ExpectedPort,
        *Timestamp,
        *Nonce,
        RequestBody);
    if (!ConstantTimeEqualsAscii(*PresentedSignature, ExpectedSignature))
    {
        OutError = TEXT("invalid proxy request signature");
        return false;
    }

    OutContext.Scope = ParsedScope;
    OutContext.ScopeText = *Scope;
    OutContext.BridgeInstance = *Instance;
    OutContext.Port = ExpectedPort;
    OutContext.Timestamp = TimestampSeconds;
    OutContext.TimestampText = *Timestamp;
    OutContext.Nonce = *Nonce;
    OutContext.RequestBody = RequestBody;
    return true;
}

FString FRevvyBridgeAuth::SignRequest(
    const FString& BridgeHmacKey,
    const FString& Scope,
    const FString& BridgeInstance,
    int32 Port,
    const FString& Timestamp,
    const FString& Nonce,
    const TArray<uint8>& RequestBody)
{
    return HmacSha1(
        BridgeHmacKey,
        BuildRequestCanonical(
            Scope,
            BridgeInstance,
            Port,
            Timestamp,
            Nonce,
            RequestBody));
}

FString FRevvyBridgeAuth::SignResponse(
    const FString& BridgeHmacKey,
    const FRevvyBridgeAuthContext& Context,
    int32 HttpStatus,
    const TArray<uint8>& ResponseBody)
{
    FString Prefix = TEXT("revvy-bridge-response-v2\n");
    Prefix += Context.ScopeText;
    Prefix += TEXT("\n");
    Prefix += Context.BridgeInstance;
    Prefix += TEXT("\n");
    Prefix += LexToString(Context.Port);
    Prefix += TEXT("\n");
    Prefix += Context.TimestampText;
    Prefix += TEXT("\n");
    Prefix += Context.Nonce;
    Prefix += TEXT("\n");
    Prefix += LexToString(HttpStatus);
    Prefix += TEXT("\n");
    Prefix += LexToString(Context.RequestBody.Num());
    Prefix += TEXT("\n");

    FString Separator = TEXT("\n");
    Separator += LexToString(ResponseBody.Num());
    Separator += TEXT("\n");

    TArray<uint8> Canonical;
    Canonical.Reserve(
        Prefix.Len() + Context.RequestBody.Num() + Separator.Len() + ResponseBody.Num());
    AppendUtf8(Canonical, Prefix);
    Canonical.Append(Context.RequestBody);
    AppendUtf8(Canonical, Separator);
    Canonical.Append(ResponseBody);
    return HmacSha1(BridgeHmacKey, Canonical);
}

bool FRevvyBridgeAuth::IsPublicPing(const FString& JsonBody)
{
    TSharedPtr<FJsonObject> Request;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonBody);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        return false;
    }

    static const TSet<FString> AllowedFields = {
        TEXT("jsonrpc"), TEXT("id"), TEXT("method"), TEXT("params")
    };
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Request->Values)
    {
        if (!AllowedFields.Contains(Field.Key))
        {
            return false;
        }
    }

    FString JsonRpc;
    FString Method;
    if (!Request->TryGetStringField(TEXT("jsonrpc"), JsonRpc)
        || JsonRpc != TEXT("2.0")
        || !Request->TryGetStringField(TEXT("method"), Method)
        || Method != TEXT("ping"))
    {
        return false;
    }

    if (const TSharedPtr<FJsonValue>* RequestId = Request->Values.Find(TEXT("id")))
    {
        if (!RequestId->IsValid()
            || ((*RequestId)->Type != EJson::String
                && (*RequestId)->Type != EJson::Number
                && (*RequestId)->Type != EJson::Null))
        {
            return false;
        }
    }

    if (const TSharedPtr<FJsonValue>* Params = Request->Values.Find(TEXT("params")))
    {
        const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
        if (!Params->IsValid()
            || !(*Params)->TryGetObject(ParamsObject)
            || ParamsObject == nullptr
            || !ParamsObject->IsValid()
            || (*ParamsObject)->Values.Num() != 0)
        {
            return false;
        }
    }
    return true;
}

bool FRevvyBridgeReplayCache::TryConsume(
    const FString& BridgeInstance,
    const FString& Nonce,
    int64 NowUnixSeconds)
{
    const FString ReplayKey = BridgeInstance + TEXT("\n") + Nonce;
    FScopeLock ScopeLock(&Lock);
    for (auto Iterator = SeenNonces.CreateIterator(); Iterator; ++Iterator)
    {
        if (Iterator->Value < NowUnixSeconds - ReplayRetentionSeconds)
        {
            Iterator.RemoveCurrent();
        }
    }
    if (SeenNonces.Contains(ReplayKey))
    {
        return false;
    }
    SeenNonces.Add(ReplayKey, NowUnixSeconds);
    return true;
}

void FRevvyBridgeReplayCache::Reset()
{
    FScopeLock ScopeLock(&Lock);
    SeenNonces.Reset();
}
