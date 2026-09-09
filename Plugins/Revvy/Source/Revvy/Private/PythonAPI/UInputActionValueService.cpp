// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UInputActionValueService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyInputActionValue, Log, All);

// Engine
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

// Enhanced Input
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"                // ETriggerEvent
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"

// Asset loading helpers
#include "UObject/SoftObjectPath.h"
#include "EditorAssetLibrary.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Load a UInputAction asset by soft/package path. */
	UInputAction* LoadInputActionAsset(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* Loaded = FSoftObjectPath(AssetPath).TryLoad())
		{
			return Cast<UInputAction>(Loaded);
		}

		if (AssetPath.StartsWith(TEXT("/")))
		{
			if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath))
			{
				return Cast<UInputAction>(Loaded);
			}
		}

		return nullptr;
	}

	/**
	 * Walk PC -> LocalPlayer -> UEnhancedInputLocalPlayerSubsystem -> UEnhancedPlayerInput.
	 * Emits a warning on each null step under the given CallerLabel.
	 */
	bool ResolveInputChain(
		APlayerController* PC,
		UEnhancedInputLocalPlayerSubsystem*& OutSubsystem,
		UEnhancedPlayerInput*& OutPlayerInput,
		const TCHAR* CallerLabel)
	{
		OutSubsystem = nullptr;
		OutPlayerInput = nullptr;

		if (!PC)
		{
			UE_LOG(LogRevvyInputActionValue, Warning,
				TEXT("%s: PlayerController is null."), CallerLabel);
			return false;
		}

		ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		if (!LocalPlayer)
		{
			UE_LOG(LogRevvyInputActionValue, Warning,
				TEXT("%s: PC '%s' has no LocalPlayer."),
				CallerLabel, *PC->GetName());
			return false;
		}

		OutSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (!OutSubsystem)
		{
			UE_LOG(LogRevvyInputActionValue, Warning,
				TEXT("%s: LocalPlayer has no UEnhancedInputLocalPlayerSubsystem."),
				CallerLabel);
			return false;
		}

		OutPlayerInput = OutSubsystem->GetPlayerInput();
		if (!OutPlayerInput)
		{
			UE_LOG(LogRevvyInputActionValue, Warning,
				TEXT("%s: EnhancedInputSubsystem has no UEnhancedPlayerInput."),
				CallerLabel);
			return false;
		}
		return true;
	}

	/** Return a stable string label for FInputActionValue::EType. */
	FString ValueTypeToString(EInputActionValueType Type)
	{
		switch (Type)
		{
			case EInputActionValueType::Boolean: return TEXT("Boolean");
			case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
			case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
			case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
			default:                             return TEXT("Unknown");
		}
	}

	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString InputActionValueJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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
	FString InputActionValueJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Build a JSON error payload with a single "note" field. */
	FString MakeNoteJson(const FString& Note)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("note"), Note);
		return InputActionValueJsonObjectToString(Obj);
	}
}

// =================================================================
// Query
// =================================================================

FString UInputActionValueService::GetInputActionValue(
	const FString& PlayerControllerActorNameOrPath,
	const FString& InputActionAssetPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("GetInputActionValue: PlayerController '%s' not found (need active PIE?)."),
			*PlayerControllerActorNameOrPath);
		return MakeNoteJson(TEXT("PlayerController not found; enter PIE and pass the PC label."));
	}

	UInputAction* Action = LoadInputActionAsset(InputActionAssetPath);
	if (!Action)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("GetInputActionValue: failed to load UInputAction '%s'."),
			*InputActionAssetPath);
		return MakeNoteJson(TEXT("UInputAction asset not found at the supplied path."));
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedPlayerInput* PlayerInput = nullptr;
	if (!ResolveInputChain(PC, Subsystem, PlayerInput, TEXT("GetInputActionValue")))
	{
		return MakeNoteJson(TEXT("EnhancedInput chain unresolved (LocalPlayer/Subsystem/PlayerInput)."));
	}

	const FInputActionValue Value = PlayerInput->GetActionValue(Action);
	const EInputActionValueType Type = Value.GetValueType();
	const FVector Axis3D = Value.Get<FVector>();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("value_type"), ValueTypeToString(Type));
	Obj->SetBoolField  (TEXT("bool_value"), Value.Get<bool>());
	Obj->SetNumberField(TEXT("axis1d"),     static_cast<double>(Axis3D.X));

	TSharedPtr<FJsonObject> Axis2DObj = MakeShared<FJsonObject>();
	Axis2DObj->SetNumberField(TEXT("x"), static_cast<double>(Axis3D.X));
	Axis2DObj->SetNumberField(TEXT("y"), static_cast<double>(Axis3D.Y));
	Obj->SetObjectField(TEXT("axis2d"), Axis2DObj);

	TSharedPtr<FJsonObject> Axis3DObj = MakeShared<FJsonObject>();
	Axis3DObj->SetNumberField(TEXT("x"), static_cast<double>(Axis3D.X));
	Axis3DObj->SetNumberField(TEXT("y"), static_cast<double>(Axis3D.Y));
	Axis3DObj->SetNumberField(TEXT("z"), static_cast<double>(Axis3D.Z));
	Obj->SetObjectField(TEXT("axis3d"), Axis3DObj);

	return InputActionValueJsonObjectToString(Obj);
}

bool UInputActionValueService::IsInputActionTriggered(
	const FString& PlayerControllerActorNameOrPath,
	const FString& InputActionAssetPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("IsInputActionTriggered: PlayerController '%s' not found."),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	UInputAction* Action = LoadInputActionAsset(InputActionAssetPath);
	if (!Action)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("IsInputActionTriggered: failed to load UInputAction '%s'."),
			*InputActionAssetPath);
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedPlayerInput* PlayerInput = nullptr;
	if (!ResolveInputChain(PC, Subsystem, PlayerInput, TEXT("IsInputActionTriggered")))
	{
		return false;
	}

	const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Action);
	if (!Instance)
	{
		// Not currently evaluated → not triggered.
		return false;
	}

	return Instance->GetTriggerEvent() == ETriggerEvent::Triggered;
}

namespace
{
	/**
	 * UE 5.8's UEnhancedPlayerInput::GetAppliedInputContextData() is protected and there
	 * is no public API to enumerate the applied mapping contexts. This access shim
	 * re-exposes the protected accessor via a using-declaration for read-only introspection.
	 */
	struct FAppliedContextsAccessor : public UEnhancedPlayerInput
	{
		using UEnhancedPlayerInput::GetAppliedInputContextData;
	};
}

FString UInputActionValueService::ListActiveMappingContexts(
	const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("ListActiveMappingContexts: PlayerController '%s' not found."),
			*PlayerControllerActorNameOrPath);
		return MakeNoteJson(TEXT("PlayerController not found; enter PIE and pass the PC label."));
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedPlayerInput* PlayerInput = nullptr;
	if (!ResolveInputChain(PC, Subsystem, PlayerInput, TEXT("ListActiveMappingContexts")))
	{
		return MakeNoteJson(TEXT("EnhancedInput chain unresolved (LocalPlayer/Subsystem/PlayerInput)."));
	}

	// UE 5.8: value type is now FAppliedInputContextData (was int32); accessor is protected.
	const TMap<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& Applied =
		static_cast<const FAppliedContextsAccessor*>(PlayerInput)->GetAppliedInputContextData();

	TArray<TSharedPtr<FJsonValue>> Arr;
	Arr.Reserve(Applied.Num());
	for (const TPair<TObjectPtr<const UInputMappingContext>, FAppliedInputContextData>& Pair : Applied)
	{
		const UInputMappingContext* IMC = Pair.Key.Get();
		if (!IMC)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("context_path"), IMC->GetPathName());
		Obj->SetNumberField(TEXT("priority"),     Pair.Value.Priority);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return InputActionValueJsonArrayToString(Arr);
}

float UInputActionValueService::GetInputActionElapsedTriggerTime(
	const FString& PlayerControllerActorNameOrPath,
	const FString& InputActionAssetPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("GetInputActionElapsedTriggerTime: PlayerController '%s' not found."),
			*PlayerControllerActorNameOrPath);
		return -1.0f;
	}

	UInputAction* Action = LoadInputActionAsset(InputActionAssetPath);
	if (!Action)
	{
		UE_LOG(LogRevvyInputActionValue, Warning,
			TEXT("GetInputActionElapsedTriggerTime: failed to load UInputAction '%s'."),
			*InputActionAssetPath);
		return -1.0f;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
	UEnhancedPlayerInput* PlayerInput = nullptr;
	if (!ResolveInputChain(PC, Subsystem, PlayerInput, TEXT("GetInputActionElapsedTriggerTime")))
	{
		return -1.0f;
	}

	const FInputActionInstance* Instance = PlayerInput->FindActionInstanceData(Action);
	if (!Instance)
	{
		return -1.0f;
	}

	return Instance->GetElapsedTime();
}
