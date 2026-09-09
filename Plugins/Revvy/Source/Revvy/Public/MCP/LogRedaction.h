// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

//
// Fail-closed redaction for argument logging (after ChiR24/Unreal_mcp).
//
// The MCP server logs every tools/call argument for diagnosability. Secrets
// travel through those arguments — API keys handed to settings tools, tokens
// in pass-through command lines — and a debug log is the one place they must
// never land. Redaction is decided by SHAPE (key name, or a secret-shaped
// assignment inside a serialized value), never by knowing which tools are
// sensitive: an allow-list of "safe tools" fails open the day a new tool ships.
//

namespace RevvyLogRedaction
{

inline const TCHAR* GRedactedPlaceholder = TEXT("[redacted]");

/** Longest argument value worth logging verbatim; longer values are elided. */
inline constexpr int32 MaxLoggedValueLength = 256;

namespace Private
{
	inline const TArray<FString>& SensitiveNeedles()
	{
		static const TArray<FString> Needles = {
			TEXT("token"), TEXT("secret"), TEXT("password"), TEXT("passphrase"),
			TEXT("apikey"), TEXT("api_key"), TEXT("api-key"),
			TEXT("authorization"), TEXT("bearer"), TEXT("credential")
		};
		return Needles;
	}
}

/** True when the argument NAME marks its value as secret-bearing. */
inline bool IsSensitiveArgumentKey(const FString& Key)
{
	const FString Normalized = Key.ToLower();
	for (const FString& Needle : Private::SensitiveNeedles())
	{
		if (Normalized.Contains(Needle))
		{
			return true;
		}
	}
	return false;
}

/**
 * True when a VALUE embeds a secret-shaped assignment — a sensitive word
 * followed by an assignment/JSON separator, as in `"api_key": "…"` inside a
 * ParamsJson blob or `--token=…` inside a pass-through command line. A value
 * merely MENTIONING the word "token" in prose stays loggable.
 */
inline bool ContainsSensitiveAssignment(const FString& Value)
{
	const FString Normalized = Value.ToLower();
	for (const FString& Needle : Private::SensitiveNeedles())
	{
		int32 SearchFrom = 0;
		for (;;)
		{
			const int32 Index = Normalized.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Index == INDEX_NONE)
			{
				break;
			}
			int32 Cursor = Index + Needle.Len();
			// Skip a closing quote and whitespace between the word and its separator.
			while (Cursor < Normalized.Len() &&
				(Normalized[Cursor] == TEXT('"') || Normalized[Cursor] == TEXT('\'') || FChar::IsWhitespace(Normalized[Cursor])))
			{
				++Cursor;
			}
			if (Cursor < Normalized.Len() &&
				(Normalized[Cursor] == TEXT(':') || Normalized[Cursor] == TEXT('=')))
			{
				return true;
			}
			SearchFrom = Index + 1;
		}
	}
	return false;
}

/** The exact string safe to write to a log for one argument. */
inline FString RedactArgumentForLog(const FString& Key, const FString& Value)
{
	if (IsSensitiveArgumentKey(Key) || ContainsSensitiveAssignment(Value))
	{
		return GRedactedPlaceholder;
	}
	if (Value.Len() > MaxLoggedValueLength)
	{
		return FString::Printf(TEXT("%s… (%d chars)"),
			*Value.Left(MaxLoggedValueLength), Value.Len());
	}
	return Value;
}

} // namespace RevvyLogRedaction
