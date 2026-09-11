// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Core/URevvyCommonHelpers.h"

// Editor / Engine
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

// Editor actor subsystem (label helpers)
#include "Subsystems/EditorActorSubsystem.h"

// Asset loading helpers (for actor path resolution)
#include "EditorAssetLibrary.h"
#include "UObject/SoftObjectPath.h"

// JSON serialization helpers
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCommonHelpers, Log, All);

// =================================================================
// GetEditorWorldSafe
// =================================================================

UWorld* URevvyCommonHelpers::GetEditorWorldSafe()
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

// =================================================================
// FindActorByNameOrPath
// =================================================================

AActor* URevvyCommonHelpers::FindActorByNameOrPath(UObject* WorldContextObject, const FString& NameOrPath)
{
	if (NameOrPath.IsEmpty())
	{
		return nullptr;
	}

	// Resolve world: prefer explicit context, fall back to GEditor world.
	UWorld* World = nullptr;
	if (WorldContextObject)
	{
		World = WorldContextObject->GetWorld();
	}
	if (!World)
	{
		World = GetEditorWorldSafe();
	}
	if (!World)
	{
		UE_LOG(LogRevvyCommonHelpers, Warning,
			TEXT("FindActorByNameOrPath: No valid UWorld for query '%s'"), *NameOrPath);
		return nullptr;
	}

	const FString LowerSearch = NameOrPath.ToLower();

	// Prefer UEditorActorSubsystem::GetAllLevelActors when available.
	TArray<AActor*> AllActors;
	if (GEditor)
	{
		if (UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
		{
			AllActors = ActorSubsystem->GetAllLevelActors();
		}
	}

	// Fallback: iterate the passed world if the editor subsystem was unreachable
	// (e.g. commandlet-driven or PIE-only paths).
	if (AllActors.Num() == 0)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (AActor* A = *It)
			{
				AllActors.Add(A);
			}
		}
	}

	// (a) Exact GetActorLabel match (case-insensitive)
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetActorLabel().ToLower() == LowerSearch)
		{
			return Actor;
		}
	}

	// (b) Exact GetName match
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName() == NameOrPath)
		{
			return Actor;
		}
	}

	// (c) Contains match on label (case-insensitive)
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetActorLabel().ToLower().Contains(LowerSearch))
		{
			return Actor;
		}
	}

	// (d) FSoftObjectPath resolve fallback
	{
		FSoftObjectPath SoftPath(NameOrPath);
		if (SoftPath.IsValid())
		{
			if (UObject* Resolved = SoftPath.ResolveObject())
			{
				if (AActor* AsActor = Cast<AActor>(Resolved))
				{
					return AsActor;
				}
			}
		}
	}

	// (e) UEditorAssetLibrary::LoadAsset fallback (package path form)
	if (NameOrPath.StartsWith(TEXT("/")))
	{
		if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(NameOrPath))
		{
			if (AActor* AsActor = Cast<AActor>(Loaded))
			{
				return AsActor;
			}
		}
	}

	return nullptr;
}

// =================================================================
// FindPlayerControllerByNameOrPath
// =================================================================

APlayerController* URevvyCommonHelpers::FindPlayerControllerByNameOrPath(UObject* WorldContextObject, const FString& NameOrPath)
{
	// Empty query → first PC in world (common PIE case)
	if (NameOrPath.IsEmpty())
	{
		UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
		if (!World)
		{
			World = GetEditorWorldSafe();
		}
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<APlayerController> It(World); It; ++It)
		{
			if (APlayerController* PC = *It)
			{
				return PC;
			}
		}
		return nullptr;
	}

	if (AActor* Found = FindActorByNameOrPath(WorldContextObject, NameOrPath))
	{
		if (APlayerController* AsPC = Cast<APlayerController>(Found))
		{
			return AsPC;
		}
	}
	return nullptr;
}

// =================================================================
// LoadClassWithFallback
// =================================================================

UClass* URevvyCommonHelpers::LoadClassWithFallback(const FString& ClassPath, UClass* RequiredBaseClass)
{
	if (ClassPath.IsEmpty())
	{
		return nullptr;
	}

	// 1. Direct LoadClass
	UClass* Loaded = LoadClass<UObject>(nullptr, *ClassPath);

	// 2. Append "_C" suffix for Blueprint generated classes
	if (!Loaded)
	{
		FString Fixed = ClassPath;
		if (!Fixed.EndsWith(TEXT("_C")))
		{
			Fixed += TEXT("_C");
			Loaded = LoadClass<UObject>(nullptr, *Fixed);
		}
	}

	// 3. StaticLoadObject(UClass::StaticClass()) fallback
	if (!Loaded)
	{
		Loaded = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *ClassPath));
	}

	if (!Loaded)
	{
		UE_LOG(LogRevvyCommonHelpers, Verbose,
			TEXT("LoadClassWithFallback: Could not resolve class '%s'"), *ClassPath);
		return nullptr;
	}

	// Validate against required base if provided
	if (RequiredBaseClass && !Loaded->IsChildOf(RequiredBaseClass))
	{
		UE_LOG(LogRevvyCommonHelpers, Warning,
			TEXT("LoadClassWithFallback: '%s' is not a subclass of '%s'"),
			*ClassPath, *RequiredBaseClass->GetName());
		return nullptr;
	}

	return Loaded;
}

// =================================================================
// IsHelperMigrationReady
// =================================================================

FString URevvyCommonHelpers::IsHelperMigrationReady()
{
	// Marker UFN — callers grep the reflection registry for this name.
	return TEXT(
		"{"
		"\"status\":\"ready\","
		"\"class\":\"URevvyCommonHelpers\","
		"\"module\":\"Revvy\","
		"\"helpers\":["
		"\"FindActorByNameOrPath\","
		"\"FindPlayerControllerByNameOrPath\","
		"\"LoadClassWithFallback\","
		"\"GetEditorWorldSafe\""
		"],"
		"\"migration\":\"opt-in\","
		"\"note\":\"Existing services unchanged; call this UFN to probe availability.\""
		"}");
}

// =================================================================
// MakeErrorJson
// =================================================================

FString URevvyCommonHelpers::MakeErrorJson(const FString& ErrorMessage, const FString& ExtraKey, const FString& ExtraValue)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("error"), ErrorMessage);
	if (!ExtraKey.IsEmpty())
	{
		Obj->SetStringField(ExtraKey, ExtraValue);
	}

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

// =================================================================
// MakeVectorJson
// =================================================================

FString URevvyCommonHelpers::MakeVectorJson(FVector V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

// =================================================================
// MakeRotatorJson
// =================================================================

FString URevvyCommonHelpers::MakeRotatorJson(FRotator R)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("pitch"), R.Pitch);
	Obj->SetNumberField(TEXT("yaw"), R.Yaw);
	Obj->SetNumberField(TEXT("roll"), R.Roll);

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

// =================================================================
// MakeTransformJson
// =================================================================

FString URevvyCommonHelpers::MakeTransformJson(FTransform T)
{
	const FVector Loc = T.GetLocation();
	const FRotator Rot = T.Rotator();
	const FVector Scl = T.GetScale3D();

	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X);
	LocObj->SetNumberField(TEXT("y"), Loc.Y);
	LocObj->SetNumberField(TEXT("z"), Loc.Z);

	TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
	RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
	RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
	RotObj->SetNumberField(TEXT("roll"), Rot.Roll);

	TSharedPtr<FJsonObject> SclObj = MakeShared<FJsonObject>();
	SclObj->SetNumberField(TEXT("x"), Scl.X);
	SclObj->SetNumberField(TEXT("y"), Scl.Y);
	SclObj->SetNumberField(TEXT("z"), Scl.Z);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetObjectField(TEXT("location"), LocObj);
	Obj->SetObjectField(TEXT("rotation"), RotObj);
	Obj->SetObjectField(TEXT("scale"), SclObj);

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

// =================================================================
// MakeColorJson
// =================================================================

FString URevvyCommonHelpers::MakeColorJson(FLinearColor C)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("r"), C.R);
	Obj->SetNumberField(TEXT("g"), C.G);
	Obj->SetNumberField(TEXT("b"), C.B);
	Obj->SetNumberField(TEXT("a"), C.A);

	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}
