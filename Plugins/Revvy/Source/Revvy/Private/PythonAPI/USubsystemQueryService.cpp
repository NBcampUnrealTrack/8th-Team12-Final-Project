// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/USubsystemQueryService.h"

// Editor / Engine
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// Subsystem families
#include "Subsystems/EngineSubsystem.h"
#include "EditorSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvySubsystem, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON array to a compact FString. */
	FString SubsystemQueryJsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArr)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonArr, Writer);
		return OutStr;
	}

	/** Build one {class_path, class_name} entry for a subsystem instance. */
	template <typename TSub>
	TSharedPtr<FJsonValue> MakeSubsystemEntry(TSub* Sub)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (Sub && Sub->GetClass())
		{
			Entry->SetStringField(TEXT("class_path"), Sub->GetClass()->GetPathName());
			Entry->SetStringField(TEXT("class_name"), Sub->GetClass()->GetName());
		}
		else
		{
			Entry->SetStringField(TEXT("class_path"), FString());
			Entry->SetStringField(TEXT("class_name"), FString());
		}
		return MakeShared<FJsonValueObject>(Entry);
	}

	/** Editor-world resolver — best-effort, returns nullptr if unavailable. */
	UWorld* GetEditorWorldSafe()
	{
#if WITH_EDITOR
		if (GEditor)
		{
			return GEditor->GetEditorWorldContext().World();
		}
#endif
		return nullptr;
	}
}

// =================================================================
// Engine subsystems
// =================================================================

FString USubsystemQueryService::ListEngineSubsystems()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	if (!GEngine)
	{
		UE_LOG(LogRevvySubsystem, Warning, TEXT("ListEngineSubsystems: GEngine is null."));
		return SubsystemQueryJsonArrayToString(Entries);
	}

	// UE 5.8: GetEngineSubsystemArray was renamed to GetEngineSubsystemArrayCopy (returns a copy).
	const TArray<UEngineSubsystem*> Subsystems = GEngine->GetEngineSubsystemArrayCopy<UEngineSubsystem>();
	Entries.Reserve(Subsystems.Num());

	for (UEngineSubsystem* Sub : Subsystems)
	{
		if (!Sub) { continue; }
		Entries.Add(MakeSubsystemEntry(Sub));
	}

	return SubsystemQueryJsonArrayToString(Entries);
}

// =================================================================
// Editor subsystems
// =================================================================

FString USubsystemQueryService::ListEditorSubsystems()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

#if WITH_EDITOR
	if (!GEditor)
	{
		UE_LOG(LogRevvySubsystem, Warning, TEXT("ListEditorSubsystems: GEditor is null."));
		return SubsystemQueryJsonArrayToString(Entries);
	}

	// UE 5.8: GetEditorSubsystemArray was renamed to GetEditorSubsystemArrayCopy (returns a copy).
	const TArray<UEditorSubsystem*> Subsystems = GEditor->GetEditorSubsystemArrayCopy<UEditorSubsystem>();
	Entries.Reserve(Subsystems.Num());

	for (UEditorSubsystem* Sub : Subsystems)
	{
		if (!Sub) { continue; }
		Entries.Add(MakeSubsystemEntry(Sub));
	}
#else
	UE_LOG(LogRevvySubsystem, Warning, TEXT("ListEditorSubsystems: not available in non-editor builds."));
#endif

	return SubsystemQueryJsonArrayToString(Entries);
}

// =================================================================
// GameInstance subsystems
// =================================================================

FString USubsystemQueryService::ListGameInstanceSubsystems()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	UWorld* World = GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvySubsystem, Warning, TEXT("ListGameInstanceSubsystems: editor world is null."));
		return SubsystemQueryJsonArrayToString(Entries);
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogRevvySubsystem, Warning, TEXT("ListGameInstanceSubsystems: GameInstance is null (no active PIE?)."));
		return SubsystemQueryJsonArrayToString(Entries);
	}

	// UE 5.8: GetSubsystemArray was renamed to GetSubsystemArrayCopy (returns a copy).
	const TArray<UGameInstanceSubsystem*> Subsystems = GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>();
	Entries.Reserve(Subsystems.Num());

	for (UGameInstanceSubsystem* Sub : Subsystems)
	{
		if (!Sub) { continue; }
		Entries.Add(MakeSubsystemEntry(Sub));
	}

	return SubsystemQueryJsonArrayToString(Entries);
}

// =================================================================
// World subsystems
// =================================================================

FString USubsystemQueryService::ListWorldSubsystems()
{
	TArray<TSharedPtr<FJsonValue>> Entries;

	UWorld* World = GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvySubsystem, Warning, TEXT("ListWorldSubsystems: editor world is null."));
		return SubsystemQueryJsonArrayToString(Entries);
	}

	// UE 5.8: GetSubsystemArray was renamed to GetSubsystemArrayCopy (returns a copy).
	const TArray<UWorldSubsystem*> Subsystems = World->GetSubsystemArrayCopy<UWorldSubsystem>();
	Entries.Reserve(Subsystems.Num());

	for (UWorldSubsystem* Sub : Subsystems)
	{
		if (!Sub) { continue; }
		Entries.Add(MakeSubsystemEntry(Sub));
	}

	return SubsystemQueryJsonArrayToString(Entries);
}
