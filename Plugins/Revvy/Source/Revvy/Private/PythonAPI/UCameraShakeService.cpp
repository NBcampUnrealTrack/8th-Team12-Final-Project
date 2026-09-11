// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UCameraShakeService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCameraShake, Log, All);

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

// PlayerController + CameraManager + CameraShake
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/CameraShakeSourceComponent.h"

// Editor actor subsystem (label helpers)
#include "Subsystems/EditorActorSubsystem.h"

// Asset loading helpers (for actor path resolution)
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
	/** Return the current editor world (PIE-preferred if running). */
	UWorld* CameraShakeGetEditorWorldLocal()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		// Prefer PIE world when running so player controllers are alive.
		if (UWorld* PIEWorld = GEditor->PlayWorld)
		{
			return PIEWorld;
		}

		return GEditor->GetEditorWorldContext().World();
	}

	/** Load a UCameraShakeBase subclass by path. */
	TSubclassOf<UCameraShakeBase> LoadShakeClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}

		UClass* Loaded = LoadClass<UCameraShakeBase>(nullptr, *ClassPath);
		if (!Loaded)
		{
			// Try appending _C for Blueprint generated classes.
			FString Fixed = ClassPath;
			if (!Fixed.EndsWith(TEXT("_C")))
			{
				Fixed += TEXT("_C");
				Loaded = LoadClass<UCameraShakeBase>(nullptr, *Fixed);
			}
		}

		if (!Loaded)
		{
			return nullptr;
		}

		if (!Loaded->IsChildOf(UCameraShakeBase::StaticClass()))
		{
			UE_LOG(LogRevvyCameraShake, Warning,
				TEXT("LoadShakeClass: '%s' is not a UCameraShakeBase subclass"), *ClassPath);
			return nullptr;
		}
		return Loaded;
	}

	/** Apply a display label to the actor if a non-empty name is provided. */
	FString ApplyLabel(AActor* Actor, const FString& DesiredName)
	{
		if (!Actor)
		{
			return FString();
		}
		if (!DesiredName.IsEmpty())
		{
			Actor->SetActorLabel(DesiredName);
		}
		return Actor->GetActorLabel();
	}

	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString CameraShakeJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
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
	FString CameraShakeJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}
}

// =================================================================
// Playback
// =================================================================

bool UCameraShakeService::PlayCameraShake(
	const FString& PlayerControllerActorNameOrPath,
	const FString& ShakeClassPath,
	float Scale)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("PlayCameraShake: PlayerController '%s' not found (need active PIE?)"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("PlayCameraShake: PlayerCameraManager null on '%s'"),
			*PC->GetName());
		return false;
	}

	TSubclassOf<UCameraShakeBase> ShakeClass = LoadShakeClass(ShakeClassPath);
	if (!ShakeClass)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("PlayCameraShake: failed to load shake class '%s'"), *ShakeClassPath);
		return false;
	}

	UCameraShakeBase* Instance = CamMgr->StartCameraShake(
		ShakeClass,
		Scale,
		ECameraShakePlaySpace::CameraLocal,
		FRotator::ZeroRotator);

	return Instance != nullptr;
}

// =================================================================
// Stop
// =================================================================

bool UCameraShakeService::StopCameraShakeByClass(
	const FString& PlayerControllerActorNameOrPath,
	const FString& ShakeClassPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("StopCameraShakeByClass: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("StopCameraShakeByClass: PlayerCameraManager null on '%s'"),
			*PC->GetName());
		return false;
	}

	TSubclassOf<UCameraShakeBase> ShakeClass = LoadShakeClass(ShakeClassPath);
	if (!ShakeClass)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("StopCameraShakeByClass: failed to load class '%s'"), *ShakeClassPath);
		return false;
	}

	CamMgr->StopAllInstancesOfCameraShake(ShakeClass, /*bImmediately=*/ true);
	return true;
}

bool UCameraShakeService::StopAllCameraShakes(const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("StopAllCameraShakes: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("StopAllCameraShakes: PlayerCameraManager null on '%s'"),
			*PC->GetName());
		return false;
	}

	CamMgr->StopAllCameraShakes(/*bImmediately=*/ true);
	return true;
}

// =================================================================
// Spatial source
// =================================================================

FString UCameraShakeService::SpawnCameraShakeSourceActor(
	const FString& Name,
	FVector Location,
	const FString& ShakeClassPath,
	float Intensity,
	float OuterRadius)
{
	UWorld* World = CameraShakeGetEditorWorldLocal();
	if (!World)
	{
		UE_LOG(LogRevvyCameraShake, Warning, TEXT("SpawnCameraShakeSourceActor: no editor world available"));
		return FString();
	}

	TSubclassOf<UCameraShakeBase> ShakeClass = LoadShakeClass(ShakeClassPath);
	if (!ShakeClass)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("SpawnCameraShakeSourceActor: failed to load shake class '%s'"), *ShakeClassPath);
		return FString();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnT;
	SpawnT.SetLocation(Location);
	SpawnT.SetRotation(FQuat::Identity);
	SpawnT.SetScale3D(FVector::OneVector);

	AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnT, SpawnParams);
	if (!NewActor)
	{
		UE_LOG(LogRevvyCameraShake, Warning, TEXT("SpawnCameraShakeSourceActor: SpawnActor returned null"));
		return FString();
	}

	// Add a root scene component if the actor has none, then attach the shake source.
	USceneComponent* Root = NewActor->GetRootComponent();
	if (!Root)
	{
		Root = NewObject<USceneComponent>(NewActor, USceneComponent::StaticClass(), TEXT("Root"));
		if (Root)
		{
			NewActor->SetRootComponent(Root);
			Root->RegisterComponent();
			Root->SetWorldLocation(Location);
		}
	}

	UCameraShakeSourceComponent* SourceComp = NewObject<UCameraShakeSourceComponent>(
		NewActor, UCameraShakeSourceComponent::StaticClass(), TEXT("CameraShakeSource"));
	if (!SourceComp)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("SpawnCameraShakeSourceActor: failed to create UCameraShakeSourceComponent"));
		NewActor->Destroy();
		return FString();
	}

	SourceComp->CameraShake            = ShakeClass;
	// UE 5.8: UCameraShakeSourceComponent has no persistent 'Intensity' member.
	// Shake magnitude is a per-play value supplied via the 'Scale' argument of
	// StartCameraShake(...) at playback time, so there is no field to set here.
	// 'Intensity' is retained by callers and passed to StartCameraShake when the
	// shake is triggered.
	(void)Intensity;
	SourceComp->OuterAttenuationRadius = OuterRadius;

	if (Root)
	{
		SourceComp->SetupAttachment(Root);
	}
	SourceComp->RegisterComponent();

	return ApplyLabel(NewActor, Name);
}

// =================================================================
// Query
// =================================================================

FString UCameraShakeService::ListActiveCameraShakes(const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(
		GEditor ? GEditor->GetEditorWorldContext().World() : nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("ListActiveCameraShakes: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return CameraShakeJsonArrayToString(TArray<TSharedPtr<FJsonValue>>());
	}

	APlayerCameraManager* CamMgr = PC->PlayerCameraManager;
	if (!CamMgr)
	{
		UE_LOG(LogRevvyCameraShake, Warning,
			TEXT("ListActiveCameraShakes: PlayerCameraManager null on '%s'"), *PC->GetName());
		return CameraShakeJsonArrayToString(TArray<TSharedPtr<FJsonValue>>());
	}

	// The authoritative list of active shakes lives on the
	// UCameraModifier_CameraShake modifier attached to the camera manager
	// as protected state, and is not exposed via a stable public API in
	// UE 5.8. Return a JSON payload with a note so callers can handle
	// the limitation gracefully.
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetArrayField(TEXT("shakes"), TArray<TSharedPtr<FJsonValue>>());
	Obj->SetStringField(TEXT("note"),
		TEXT("APlayerCameraManager active shake list is not publicly accessible in UE 5.8; ")
		TEXT("track spawned instances at the caller (returned by play_camera_shake)."));

	UE_LOG(LogRevvyCameraShake, Warning,
		TEXT("ListActiveCameraShakes: active shake list not publicly accessible in UE 5.8; returning empty payload."));

	return CameraShakeJsonObjectToString(Obj);
}
