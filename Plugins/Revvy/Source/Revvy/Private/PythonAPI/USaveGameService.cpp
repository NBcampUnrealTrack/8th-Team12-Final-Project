// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/USaveGameService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvySaveGame, Log, All);

// Engine / SaveGame API
#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "SaveGameSystem.h"        // ISaveGameSystem
#include "PlatformFeatures.h"      // IPlatformFeaturesModule

// Object lifetime
#include "UObject/StrongObjectPtr.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

// JSON
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Handle registry
// =================================================================
//
// USaveGame instances are transient UObjects. To keep them alive across
// Python calls we hold them in a static TMap keyed by a caller-supplied
// handle string. TStrongObjectPtr prevents GC while the entry is live.

namespace
{
	using FSaveGameRegistry = TMap<FString, TStrongObjectPtr<USaveGame>>;

	FSaveGameRegistry& GetRegistry()
	{
		static FSaveGameRegistry Registry;
		return Registry;
	}

	/** Insert or overwrite. Logs a Warning on collision (documented behaviour). */
	void RegisterSaveGame(const FString& Handle, USaveGame* SaveGame, const TCHAR* CallerLabel)
	{
		FSaveGameRegistry& Registry = GetRegistry();
		if (Registry.Contains(Handle))
		{
			UE_LOG(LogRevvySaveGame, Warning,
				TEXT("%s: Handle '%s' already registered — overwriting existing SaveGame."),
				CallerLabel, *Handle);
		}
		Registry.Add(Handle, TStrongObjectPtr<USaveGame>(SaveGame));
	}

	/** Return the SaveGame at Handle, or nullptr if missing / stale. */
	USaveGame* LookupSaveGame(const FString& Handle)
	{
		FSaveGameRegistry& Registry = GetRegistry();
		if (TStrongObjectPtr<USaveGame>* Entry = Registry.Find(Handle))
		{
			return Entry->Get();
		}
		return nullptr;
	}

	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString SaveGameJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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

	/** Serialize a JSON array to a compact FString. */
	FString SaveGameJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}
}

// =================================================================
// Create
// =================================================================

FString USaveGameService::CreateSaveGameObject(const FString& SaveGameClassPath, const FString& Handle)
{
	static const TCHAR* Caller = TEXT("CreateSaveGameObject");

	if (Handle.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: Handle is empty."), Caller);
		return FString();
	}
	if (SaveGameClassPath.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: SaveGameClassPath is empty."), Caller);
		return FString();
	}

	UClass* SaveGameClass = LoadObject<UClass>(nullptr, *SaveGameClassPath);
	if (!SaveGameClass)
	{
		// Fallback path — StaticLoadClass for Blueprint-generated classes.
		SaveGameClass = StaticLoadClass(USaveGame::StaticClass(), nullptr, *SaveGameClassPath);
	}
	if (!SaveGameClass)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: unable to resolve UClass '%s'."), Caller, *SaveGameClassPath);
		return FString();
	}

	if (!SaveGameClass->IsChildOf(USaveGame::StaticClass()))
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: class '%s' does not derive from USaveGame."), Caller, *SaveGameClassPath);
		return FString();
	}

	USaveGame* Instance = UGameplayStatics::CreateSaveGameObject(SaveGameClass);
	if (!Instance)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: CreateSaveGameObject returned null for '%s'."), Caller, *SaveGameClassPath);
		return FString();
	}

	RegisterSaveGame(Handle, Instance, Caller);
	UE_LOG(LogRevvySaveGame, Log,
		TEXT("%s: registered '%s' as %s."), Caller, *Handle, *SaveGameClass->GetPathName());
	return Handle;
}

// =================================================================
// Persist
// =================================================================

bool USaveGameService::SaveGameToSlot(const FString& Handle, const FString& SlotName, int32 UserIndex)
{
	static const TCHAR* Caller = TEXT("SaveGameToSlot");

	USaveGame* SaveGame = LookupSaveGame(Handle);
	if (!SaveGame)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: Handle '%s' not registered."), Caller, *Handle);
		return false;
	}
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: SlotName is empty."), Caller);
		return false;
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, UserIndex);
	if (!bOk)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: UGameplayStatics::SaveGameToSlot failed (slot='%s', user=%d)."),
			Caller, *SlotName, UserIndex);
	}
	return bOk;
}

FString USaveGameService::LoadGameFromSlot(const FString& SlotName, int32 UserIndex, const FString& Handle)
{
	static const TCHAR* Caller = TEXT("LoadGameFromSlot");

	if (Handle.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: Handle is empty."), Caller);
		return FString();
	}
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: SlotName is empty."), Caller);
		return FString();
	}

	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex))
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: slot '%s' (user=%d) does not exist."), Caller, *SlotName, UserIndex);
		return FString();
	}

	USaveGame* Loaded = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);
	if (!Loaded)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: LoadGameFromSlot returned null for slot '%s' (user=%d)."),
			Caller, *SlotName, UserIndex);
		return FString();
	}

	RegisterSaveGame(Handle, Loaded, Caller);
	UE_LOG(LogRevvySaveGame, Log,
		TEXT("%s: loaded '%s' (user=%d) as handle '%s' (%s)."),
		Caller, *SlotName, UserIndex, *Handle, *Loaded->GetClass()->GetPathName());
	return Handle;
}

bool USaveGameService::AsyncSaveGameToSlot(const FString& Handle, const FString& SlotName, int32 UserIndex)
{
	static const TCHAR* Caller = TEXT("AsyncSaveGameToSlot");

	USaveGame* SaveGame = LookupSaveGame(Handle);
	if (!SaveGame)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: Handle '%s' not registered."), Caller, *Handle);
		return false;
	}
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: SlotName is empty."), Caller);
		return false;
	}

	// Log-only completion delegate — Python callers can poll with
	// DoesSaveGameExist if they need to confirm completion.
	FAsyncSaveGameToSlotDelegate CompletionDelegate;
	CompletionDelegate.BindLambda(
		[](const FString& CompletedSlot, const int32 CompletedUser, bool bSuccess)
		{
			UE_LOG(LogRevvySaveGame, Log,
				TEXT("AsyncSaveGameToSlot: completed slot='%s' user=%d success=%d"),
				*CompletedSlot, CompletedUser, bSuccess ? 1 : 0);
		});

	UGameplayStatics::AsyncSaveGameToSlot(SaveGame, SlotName, UserIndex, CompletionDelegate);
	return true;
}

bool USaveGameService::DeleteSaveSlot(const FString& SlotName, int32 UserIndex)
{
	static const TCHAR* Caller = TEXT("DeleteSaveSlot");

	if (SlotName.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("%s: SlotName is empty."), Caller);
		return false;
	}

	const bool bOk = UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
	if (!bOk)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: DeleteGameInSlot returned false for slot='%s' user=%d."),
			Caller, *SlotName, UserIndex);
	}
	return bOk;
}

// =================================================================
// Query
// =================================================================

bool USaveGameService::DoesSaveGameExist(const FString& SlotName, int32 UserIndex)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogRevvySaveGame, Warning, TEXT("DoesSaveGameExist: SlotName is empty."));
		return false;
	}
	return UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex);
}

FString USaveGameService::ListSaveSlots(int32 UserIndex)
{
	static const TCHAR* Caller = TEXT("ListSaveSlots");

	ISaveGameSystem* System = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!System)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: no ISaveGameSystem from IPlatformFeaturesModule."), Caller);
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("supported"), false);
		Obj->SetStringField(TEXT("note"), TEXT("ISaveGameSystem unavailable on this platform."));
		return SaveGameJsonObjectToString(Obj);
	}

	TArray<FString> SlotNames;
	const bool bEnumerated = System->GetSaveGameNames(SlotNames, UserIndex);
	if (!bEnumerated)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: ISaveGameSystem::GetSaveGameNames returned false — "
			     "platform likely does not support enumeration."), Caller);
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("supported"), false);
		Obj->SetStringField(TEXT("note"), TEXT("ISaveGameSystem does not support slot enumeration on this platform."));
		return SaveGameJsonObjectToString(Obj);
	}

	TArray<TSharedPtr<FJsonValue>> ArrayValues;
	ArrayValues.Reserve(SlotNames.Num());
	for (const FString& Slot : SlotNames)
	{
		ArrayValues.Add(MakeShared<FJsonValueString>(Slot));
	}
	return SaveGameJsonArrayToString(ArrayValues);
}

FString USaveGameService::GetSaveGamePropertiesJson(const FString& Handle)
{
	static const TCHAR* Caller = TEXT("GetSaveGamePropertiesJson");

	USaveGame* SaveGame = LookupSaveGame(Handle);
	if (!SaveGame)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: Handle '%s' not registered."), Caller, *Handle);
		return FString();
	}

	const UClass* Class = SaveGame->GetClass();
	if (!Class)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: SaveGame at '%s' has no UClass."), Caller, *Handle);
		return FString();
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	// Serialize UPROPERTYs on the SaveGame's UClass. UStructToJsonObject
	// mirrors what FJsonObjectConverter produces for gameplay saves and
	// respects UPROPERTY flags automatically.
	const bool bOk = FJsonObjectConverter::UStructToJsonObject(
		Class,
		SaveGame,
		Root,
		/*CheckFlags=*/ 0,
		/*SkipFlags=*/ 0);

	if (!bOk)
	{
		UE_LOG(LogRevvySaveGame, Warning,
			TEXT("%s: UStructToJsonObject failed for handle '%s' (class '%s')."),
			Caller, *Handle, *Class->GetPathName());
		return FString();
	}

	// Convenience metadata.
	Root->SetStringField(TEXT("__handle"), Handle);
	Root->SetStringField(TEXT("__class"), Class->GetPathName());
	return SaveGameJsonObjectToString(Root);
}
