// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UOnlineSessionService.h"

// OnlineSubsystem
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"

// JSON
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyOnlineSession, Log, All);

// =================================================================
// Internal state — retained across UFUNCTION calls so callers can
// reference a search result by index without re-issuing the search.
// =================================================================
static TSharedPtr<FOnlineSessionSearch> LastSearch;

// =================================================================
// Local helpers
// =================================================================
namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString OnlineSessionJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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

	/** Resolve the current OnlineSubsystem session interface (may be null). */
	IOnlineSessionPtr GetSessionInterfaceChecked()
	{
		IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
		if (!OSS)
		{
			UE_LOG(LogRevvyOnlineSession, Warning, TEXT("OnlineSubsystem unavailable"));
			return nullptr;
		}
		IOnlineSessionPtr SI = OSS->GetSessionInterface();
		if (!SI.IsValid())
		{
			UE_LOG(LogRevvyOnlineSession, Warning, TEXT("OnlineSubsystem session interface unavailable"));
			return nullptr;
		}
		return SI;
	}
}

// =================================================================
// Lifecycle
// =================================================================

bool UOnlineSessionService::CreateSession(const FString& SessionName, int32 NumPublicConnections, bool bLan)
{
	IOnlineSessionPtr SI = GetSessionInterfaceChecked();
	if (!SI.IsValid())
	{
		return false;
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = NumPublicConnections;
	Settings.bIsLANMatch = bLan;
	Settings.bShouldAdvertise = true;

	const bool bAccepted = SI->CreateSession(0, FName(*SessionName), Settings);
	UE_LOG(LogRevvyOnlineSession, Log,
		TEXT("CreateSession '%s' (Slots=%d, LAN=%d) → accepted=%d"),
		*SessionName, NumPublicConnections, bLan ? 1 : 0, bAccepted ? 1 : 0);
	return bAccepted;
}

bool UOnlineSessionService::DestroySession(const FString& SessionName)
{
	IOnlineSessionPtr SI = GetSessionInterfaceChecked();
	if (!SI.IsValid())
	{
		return false;
	}

	const bool bAccepted = SI->DestroySession(FName(*SessionName));
	UE_LOG(LogRevvyOnlineSession, Log,
		TEXT("DestroySession '%s' → accepted=%d"),
		*SessionName, bAccepted ? 1 : 0);
	return bAccepted;
}

// =================================================================
// Find
// =================================================================

FString UOnlineSessionService::FindSessions(int32 MaxResults, bool bLan)
{
	TSharedPtr<FJsonObject> Note = MakeShared<FJsonObject>();

	IOnlineSessionPtr SI = GetSessionInterfaceChecked();
	if (!SI.IsValid())
	{
		Note->SetStringField(TEXT("error"), TEXT("OnlineSubsystem or session interface unavailable"));
		return OnlineSessionJsonObjectToString(Note);
	}

	LastSearch = MakeShared<FOnlineSessionSearch>();
	LastSearch->MaxSearchResults = MaxResults;
	LastSearch->bIsLanQuery = bLan;

	const bool bAccepted = SI->FindSessions(0, LastSearch.ToSharedRef());
	UE_LOG(LogRevvyOnlineSession, Log,
		TEXT("FindSessions (MaxResults=%d, LAN=%d) → accepted=%d"),
		MaxResults, bLan ? 1 : 0, bAccepted ? 1 : 0);

	Note->SetBoolField(TEXT("accepted"), bAccepted);
	Note->SetNumberField(TEXT("max_results"), MaxResults);
	Note->SetBoolField(TEXT("lan"), bLan);
	Note->SetStringField(TEXT("note"),
		TEXT("Async request dispatched. Results delivered via OnFindSessionsComplete; "
		     "inspect LastSearch->SearchResults or call JoinSession by index once complete."));
	return OnlineSessionJsonObjectToString(Note);
}

// =================================================================
// Join
// =================================================================

bool UOnlineSessionService::JoinSession(const FString& SessionName, int32 SearchResultIndex)
{
	if (!LastSearch.IsValid() || !LastSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		UE_LOG(LogRevvyOnlineSession, Warning,
			TEXT("JoinSession '%s' — LastSearch invalid or index %d out of bounds"),
			*SessionName, SearchResultIndex);
		return false;
	}

	IOnlineSessionPtr SI = GetSessionInterfaceChecked();
	if (!SI.IsValid())
	{
		return false;
	}

	const bool bAccepted = SI->JoinSession(0, FName(*SessionName), LastSearch->SearchResults[SearchResultIndex]);
	UE_LOG(LogRevvyOnlineSession, Log,
		TEXT("JoinSession '%s' (Index=%d) → accepted=%d"),
		*SessionName, SearchResultIndex, bAccepted ? 1 : 0);
	return bAccepted;
}
