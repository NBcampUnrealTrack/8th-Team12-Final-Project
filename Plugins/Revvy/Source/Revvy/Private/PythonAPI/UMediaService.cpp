// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UMediaService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyMedia, Log, All);

#include "Editor.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "FileMediaSource.h"

#include "UObject/StrongObjectPtr.h"
#include "Misc/Timespan.h"

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
	/**
	 * Named-player registry, keyed by user-supplied PlayerName.
	 * TStrongObjectPtr keeps each UMediaPlayer rooted against GC while the
	 * registry retains it. Entries are removed only when a caller explicitly
	 * lets the registry drop them (currently: never — StopMediaPlayer closes
	 * the source but keeps the entry for reuse).
	 */
	static TMap<FString, TStrongObjectPtr<UMediaPlayer>>& GetPlayerRegistry()
	{
		static TMap<FString, TStrongObjectPtr<UMediaPlayer>> Players;
		return Players;
	}

	/** Emit a Warning if not on the GameThread — registry access is not thread-safe. */
	void MediaWarnIfNotGameThread(const TCHAR* Op)
	{
		if (!IsInGameThread())
		{
			UE_LOG(LogRevvyMedia, Warning,
				TEXT("UMediaService::%s called off GameThread — registry access is not thread-safe."),
				Op);
		}
	}

	/** Look up an existing player by name. Returns nullptr if not registered. */
	UMediaPlayer* FindPlayer(const FString& PlayerName)
	{
		if (PlayerName.IsEmpty())
		{
			return nullptr;
		}
		TStrongObjectPtr<UMediaPlayer>* Entry = GetPlayerRegistry().Find(PlayerName);
		if (!Entry)
		{
			return nullptr;
		}
		return Entry->Get();
	}

	/**
	 * Look up or create a UMediaPlayer under PlayerName. Newly-created players
	 * are transient (no package) — playback state lives only in memory.
	 */
	UMediaPlayer* FindOrCreatePlayer(const FString& PlayerName)
	{
		if (PlayerName.IsEmpty())
		{
			UE_LOG(LogRevvyMedia, Warning, TEXT("FindOrCreatePlayer: PlayerName must be non-empty."));
			return nullptr;
		}

		if (UMediaPlayer* Existing = FindPlayer(PlayerName))
		{
			return Existing;
		}

		UMediaPlayer* NewPlayer = NewObject<UMediaPlayer>(
			GetTransientPackage(),
			UMediaPlayer::StaticClass(),
			NAME_None,
			RF_Transient);

		if (!NewPlayer)
		{
			UE_LOG(LogRevvyMedia, Warning,
				TEXT("FindOrCreatePlayer: NewObject<UMediaPlayer> failed for '%s'."),
				*PlayerName);
			return nullptr;
		}

		GetPlayerRegistry().Add(PlayerName, TStrongObjectPtr<UMediaPlayer>(NewPlayer));
		UE_LOG(LogRevvyMedia, Log,
			TEXT("FindOrCreatePlayer: registered new UMediaPlayer under '%s'."),
			*PlayerName);
		return NewPlayer;
	}

	/** Serialize a JSON object to a compact string. */
	FString MediaJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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
// Playback
// =================================================================

FString UMediaService::PlayMediaSource(const FString& MediaSourcePath, const FString& PlayerName)
{
	MediaWarnIfNotGameThread(TEXT("PlayMediaSource"));

	if (PlayerName.IsEmpty())
	{
		UE_LOG(LogRevvyMedia, Warning, TEXT("PlayMediaSource: PlayerName must be non-empty."));
		return FString();
	}

	if (MediaSourcePath.IsEmpty())
	{
		UE_LOG(LogRevvyMedia, Warning, TEXT("PlayMediaSource: MediaSourcePath must be non-empty."));
		return FString();
	}

	UMediaSource* Source = LoadObject<UMediaSource>(nullptr, *MediaSourcePath);
	if (!Source)
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("PlayMediaSource: could not load UMediaSource at '%s'."),
			*MediaSourcePath);
		return FString();
	}

	UMediaPlayer* Player = FindOrCreatePlayer(PlayerName);
	if (!Player)
	{
		return FString();
	}

	// If the player already had a source open, close it before switching.
	Player->Close();

	if (!Player->OpenSource(Source))
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("PlayMediaSource: OpenSource failed for '%s' on player '%s'."),
			*MediaSourcePath, *PlayerName);
		return FString();
	}

	if (!Player->Play())
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("PlayMediaSource: Play() failed on player '%s' (source '%s' opened)."),
			*PlayerName, *MediaSourcePath);
		// Source is opened even if Play() failed — leave it open so callers can
		// still Seek/Pause/etc. We still consider PlayMediaSource a failure.
		return FString();
	}

	UE_LOG(LogRevvyMedia, Log,
		TEXT("PlayMediaSource: player '%s' playing source '%s'."),
		*PlayerName, *MediaSourcePath);
	return PlayerName;
}

// =================================================================
// Control
// =================================================================

bool UMediaService::PauseMediaPlayer(const FString& PlayerName)
{
	MediaWarnIfNotGameThread(TEXT("PauseMediaPlayer"));

	UMediaPlayer* Player = FindPlayer(PlayerName);
	if (!Player)
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("PauseMediaPlayer: player '%s' is not registered."), *PlayerName);
		return false;
	}

	Player->Pause();
	UE_LOG(LogRevvyMedia, Verbose, TEXT("PauseMediaPlayer: '%s' paused."), *PlayerName);
	return true;
}

bool UMediaService::StopMediaPlayer(const FString& PlayerName)
{
	MediaWarnIfNotGameThread(TEXT("StopMediaPlayer"));

	UMediaPlayer* Player = FindPlayer(PlayerName);
	if (!Player)
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("StopMediaPlayer: player '%s' is not registered."), *PlayerName);
		return false;
	}

	// Close() releases the open source so no transient objects leak on Stop.
	Player->Close();
	UE_LOG(LogRevvyMedia, Verbose, TEXT("StopMediaPlayer: '%s' closed."), *PlayerName);
	return true;
}

bool UMediaService::SeekMediaPlayer(const FString& PlayerName, float TimeSeconds)
{
	MediaWarnIfNotGameThread(TEXT("SeekMediaPlayer"));

	UMediaPlayer* Player = FindPlayer(PlayerName);
	if (!Player)
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("SeekMediaPlayer: player '%s' is not registered."), *PlayerName);
		return false;
	}

	const FTimespan Target = FTimespan::FromSeconds(FMath::Max(0.0f, TimeSeconds));
	Player->Seek(Target);
	UE_LOG(LogRevvyMedia, Verbose,
		TEXT("SeekMediaPlayer: '%s' seek → %.3fs."),
		*PlayerName, TimeSeconds);
	return true;
}

// =================================================================
// Query
// =================================================================

FString UMediaService::GetMediaPlayerState(const FString& PlayerName)
{
	MediaWarnIfNotGameThread(TEXT("GetMediaPlayerState"));

	UMediaPlayer* Player = FindPlayer(PlayerName);
	if (!Player)
	{
		UE_LOG(LogRevvyMedia, Warning,
			TEXT("GetMediaPlayerState: player '%s' is not registered."), *PlayerName);
		return FString();
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("is_playing"), Player->IsPlaying());
	Obj->SetBoolField(TEXT("is_paused"), Player->IsPaused());
	Obj->SetNumberField(TEXT("current_time"), Player->GetTime().GetTotalSeconds());
	Obj->SetNumberField(TEXT("duration"), Player->GetDuration().GetTotalSeconds());
	Obj->SetBoolField(TEXT("is_ready"), Player->IsReady());
	Obj->SetBoolField(TEXT("is_looping"), Player->IsLooping());

	return MediaJsonObjectToString(Obj);
}
