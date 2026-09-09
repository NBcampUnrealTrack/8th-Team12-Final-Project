// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPlayerMappableInputService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPlayerMappable, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

// Enhanced Input
#include "EnhancedInputSubsystems.h"                 // UEnhancedInputLocalPlayerSubsystem
#include "UserSettings/EnhancedInputUserSettings.h"  // UEnhancedInputUserSettings + FMapPlayerKeyArgs
#include "InputAction.h"                             // UInputAction (for GetPathName on soft ref)

// Input keys
#include "InputCoreTypes.h"

// GameplayTags (failure reason container)
#include "GameplayTagContainer.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString PlayerMappableJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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
	FString PlayerMappableJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/**
	 * Resolve the best-available UWorld from the editor.
	 * Prefers the PIE world (where a valid GameInstance / LocalPlayer exists)
	 * and falls back to the editor world.
	 */
	UWorld* ResolveActiveWorld()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			// Prefer PIE — that's where LocalPlayers actually exist.
			if (const FWorldContext* PIEContext = GEditor->GetPIEWorldContext())
			{
				if (UWorld* PIEWorld = PIEContext->World())
				{
					return PIEWorld;
				}
			}

			// Fallback: editor world (usually has no GameInstance).
			if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			{
				return EditorWorld;
			}
		}
#endif
		return nullptr;
	}

	/**
	 * Walk World -> GameInstance -> first LocalPlayer -> EnhancedInput subsystem
	 * -> UserSettings -> active KeyProfile with clear per-step logging.
	 *
	 * @param OutWorld        Populated with resolved UWorld* if non-null was
	 *                        needed further up the chain (may be null).
	 * @param OutLocalPlayer  Populated with the first LocalPlayer.
	 * @param OutSubsystem    Populated with the LocalPlayer's EnhancedInput subsystem.
	 * @param OutUserSettings Populated with the subsystem's UserSettings.
	 * @param bRequireProfile If true, also require GetActiveKeyProfile() != null.
	 * @param OutProfile      Populated with the active key profile when required.
	 * @return true only when every requested step resolved to a non-null pointer.
	 */
	bool ResolveEnhancedInputChain(
		UWorld*& OutWorld,
		ULocalPlayer*& OutLocalPlayer,
		UEnhancedInputLocalPlayerSubsystem*& OutSubsystem,
		UEnhancedInputUserSettings*& OutUserSettings,
		bool bRequireProfile,
		UEnhancedPlayerMappableKeyProfile*& OutProfile,
		const TCHAR* CallerLabel)
	{
		OutWorld = nullptr;
		OutLocalPlayer = nullptr;
		OutSubsystem = nullptr;
		OutUserSettings = nullptr;
		OutProfile = nullptr;

		OutWorld = ResolveActiveWorld();
		if (!OutWorld)
		{
			UE_LOG(LogRevvyPlayerMappable, Warning,
				TEXT("%s: no active UWorld (editor + PIE both unavailable)."), CallerLabel);
			return false;
		}

		UGameInstance* GameInstance = OutWorld->GetGameInstance();
		if (!GameInstance)
		{
			UE_LOG(LogRevvyPlayerMappable, Warning,
				TEXT("%s: UWorld '%s' has no UGameInstance. Enter PIE to gain a LocalPlayer."),
				CallerLabel, *OutWorld->GetName());
			return false;
		}

		OutLocalPlayer = GameInstance->GetFirstGamePlayer();
		if (!OutLocalPlayer)
		{
			UE_LOG(LogRevvyPlayerMappable, Warning,
				TEXT("%s: UGameInstance has no LocalPlayer at index 0."), CallerLabel);
			return false;
		}

		OutSubsystem = OutLocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (!OutSubsystem)
		{
			UE_LOG(LogRevvyPlayerMappable, Warning,
				TEXT("%s: LocalPlayer has no UEnhancedInputLocalPlayerSubsystem."), CallerLabel);
			return false;
		}

		OutUserSettings = OutSubsystem->GetUserSettings();
		if (!OutUserSettings)
		{
			UE_LOG(LogRevvyPlayerMappable, Warning,
				TEXT("%s: UEnhancedInputLocalPlayerSubsystem has no UserSettings. "
				     "Ensure bEnableUserSettings=true in Enhanced Input project settings."),
				CallerLabel);
			return false;
		}

		if (bRequireProfile)
		{
			OutProfile = OutUserSettings->GetActiveKeyProfile();
			if (!OutProfile)
			{
				UE_LOG(LogRevvyPlayerMappable, Warning,
					TEXT("%s: UserSettings has no active UEnhancedPlayerMappableKeyProfile."),
					CallerLabel);
				return false;
			}
		}

		return true;
	}

	/**
	 * Convert an EPlayerMappableKeySlot enum to a stable integer for JSON.
	 * Matches the underlying uint8 representation (0=First .. 7=Unspecified).
	 */
	int32 SlotToInt(EPlayerMappableKeySlot InSlot)
	{
		return static_cast<int32>(InSlot);
	}

	/**
	 * Convert an integer coming from Python to EPlayerMappableKeySlot,
	 * clamping out-of-range values to Unspecified.
	 */
	EPlayerMappableKeySlot IntToSlot(int32 InSlot)
	{
		if (InSlot < 0 || InSlot >= static_cast<int32>(EPlayerMappableKeySlot::Max))
		{
			return EPlayerMappableKeySlot::Unspecified;
		}
		return static_cast<EPlayerMappableKeySlot>(InSlot);
	}
}

// =================================================================
// Query
// =================================================================

FString UPlayerMappableInputService::ListPlayerKeyMappings()
{
	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/true, Profile,
	                               TEXT("ListPlayerKeyMappings")))
	{
		return FString();
	}

	const TMap<FName, FKeyMappingRow>& Rows = Profile->GetPlayerMappingRows();

	TArray<TSharedPtr<FJsonValue>> JsonRows;
	JsonRows.Reserve(Rows.Num());

	for (const TPair<FName, FKeyMappingRow>& RowPair : Rows)
	{
		const FName RowName = RowPair.Key;
		const FKeyMappingRow& Row = RowPair.Value;

		for (const FPlayerKeyMapping& Mapping : Row.Mappings)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

			const FKey& CurrentKey = Mapping.GetCurrentKey();
			const FKey& DefaultKey = Mapping.GetDefaultKey();

			Obj->SetStringField(TEXT("mapping_name"), RowName.ToString());

			FString ActionPath;
			if (const UInputAction* Action = Mapping.GetAssociatedInputAction())
			{
				ActionPath = Action->GetPathName();
			}
			Obj->SetStringField(TEXT("action_path"),   ActionPath);
			Obj->SetStringField(TEXT("key_name"),      CurrentKey.GetFName().ToString());
			Obj->SetNumberField(TEXT("slot"),          SlotToInt(Mapping.GetSlot()));
			Obj->SetStringField(TEXT("current_key"),   CurrentKey.GetFName().ToString());
			Obj->SetStringField(TEXT("default_key"),   DefaultKey.GetFName().ToString());
			Obj->SetBoolField  (TEXT("is_customized"), Mapping.IsCustomized());

			JsonRows.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	return PlayerMappableJsonArrayToString(JsonRows);
}

FString UPlayerMappableInputService::GetActiveKeyProfile()
{
	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/true, Profile,
	                               TEXT("GetActiveKeyProfile")))
	{
		return FString();
	}

	const FString& ProfileId   = Profile->GetProfileIdString();
	const FText&   DisplayText = Profile->GetProfileDisplayName();
	const int32    RowCount    = Profile->GetPlayerMappingRows().Num();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("profile_id"),   ProfileId);
	Obj->SetStringField(TEXT("display_name"), DisplayText.ToString());
	Obj->SetNumberField(TEXT("row_count"),    RowCount);

	return PlayerMappableJsonObjectToString(Obj);
}

// =================================================================
// Rebind
// =================================================================

bool UPlayerMappableInputService::RebindPlayerKey(const FString& MappingName, const FString& NewKeyName, int32 Slot)
{
	if (MappingName.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning, TEXT("RebindPlayerKey: MappingName is empty."));
		return false;
	}

	if (NewKeyName.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning, TEXT("RebindPlayerKey: NewKeyName is empty."));
		return false;
	}

	// Validate FKey exists in the registry.
	const FKey NewKey(*NewKeyName);
	if (!EKeys::GetKeyDetails(NewKey).IsValid())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning,
			TEXT("RebindPlayerKey: unknown FKey '%s' (EKeys::GetKeyDetails invalid)."),
			*NewKeyName);
		return false;
	}

	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/false, Profile,
	                               TEXT("RebindPlayerKey")))
	{
		return false;
	}

	FMapPlayerKeyArgs Args;
	Args.MappingName = FName(*MappingName);
	Args.Slot        = IntToSlot(Slot);
	Args.NewKey      = NewKey;
	Args.bCreateMatchingSlotIfNeeded = true;

	FGameplayTagContainer FailureReason;
	UserSettings->MapPlayerKey(Args, FailureReason);

	if (!FailureReason.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning,
			TEXT("RebindPlayerKey: MapPlayerKey reported failure for '%s' slot %d -> '%s'. Reasons: %s"),
			*MappingName, Slot, *NewKeyName, *FailureReason.ToString());
		return false;
	}

	UE_LOG(LogRevvyPlayerMappable, Log,
		TEXT("RebindPlayerKey: '%s' slot %d -> '%s' OK."),
		*MappingName, Slot, *NewKeyName);
	return true;
}

bool UPlayerMappableInputService::ResetPlayerKey(const FString& MappingName, int32 Slot)
{
	if (MappingName.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning, TEXT("ResetPlayerKey: MappingName is empty."));
		return false;
	}

	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/false, Profile,
	                               TEXT("ResetPlayerKey")))
	{
		return false;
	}

	FMapPlayerKeyArgs Args;
	Args.MappingName = FName(*MappingName);
	Args.Slot        = IntToSlot(Slot);

	FGameplayTagContainer FailureReason;
	UserSettings->UnMapPlayerKey(Args, FailureReason);

	if (!FailureReason.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning,
			TEXT("ResetPlayerKey: UnMapPlayerKey reported failure for '%s' slot %d. Reasons: %s"),
			*MappingName, Slot, *FailureReason.ToString());
		return false;
	}

	UE_LOG(LogRevvyPlayerMappable, Log,
		TEXT("ResetPlayerKey: '%s' slot %d unmapped."), *MappingName, Slot);
	return true;
}

bool UPlayerMappableInputService::ResetAllPlayerKeys()
{
	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/true, Profile,
	                               TEXT("ResetAllPlayerKeys")))
	{
		return false;
	}

	const FString& ActiveProfileId = UserSettings->GetActiveKeyProfileId();

	FGameplayTagContainer FailureReason;
	UserSettings->ResetKeyProfileIdToDefault(ActiveProfileId, FailureReason);

	if (!FailureReason.IsEmpty())
	{
		UE_LOG(LogRevvyPlayerMappable, Warning,
			TEXT("ResetAllPlayerKeys: ResetKeyProfileIdToDefault('%s') reported failure. Reasons: %s"),
			*ActiveProfileId, *FailureReason.ToString());
		return false;
	}

	UE_LOG(LogRevvyPlayerMappable, Log,
		TEXT("ResetAllPlayerKeys: profile '%s' reset to IMC defaults."), *ActiveProfileId);
	return true;
}

// =================================================================
// Persist
// =================================================================

bool UPlayerMappableInputService::SavePlayerKeyMappings()
{
	UWorld* World = nullptr;
	ULocalPlayer* LocalPlayer = nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedInputUserSettings* UserSettings = nullptr;
	UEnhancedPlayerMappableKeyProfile* Profile = nullptr;

	if (!ResolveEnhancedInputChain(World, LocalPlayer, Subsystem, UserSettings,
	                               /*bRequireProfile=*/false, Profile,
	                               TEXT("SavePlayerKeyMappings")))
	{
		return false;
	}

	UserSettings->SaveSettings();
	UE_LOG(LogRevvyPlayerMappable, Log, TEXT("SavePlayerKeyMappings: SaveSettings() dispatched."));
	return true;
}
