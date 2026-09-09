// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "USubsystemQueryService.generated.h"

/**
 * Tiny read-only introspection service that enumerates the four
 * canonical UE subsystem families (Engine, Editor, GameInstance,
 * World) and returns their live instances as JSON.
 *
 * Mirrors the UPerformanceService pattern: all UFUNCTIONs are
 * static + BlueprintCallable so they are reachable from Python as
 * `unreal.SubsystemQueryService.<snake_case_name>()`.
 *
 * No mutation → no FScopedTransaction required.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.SubsystemQueryService
 *   engine_subs = json.loads(svc.list_engine_subsystems())
 *   editor_subs = json.loads(svc.list_editor_subsystems())
 *   gi_subs     = json.loads(svc.list_game_instance_subsystems())
 *   world_subs  = json.loads(svc.list_world_subsystems())
 *
 * Returned JSON is always an array of {class_path, class_name}.
 * On missing runtime handles (no GEngine / no World / no GI) the
 * array is empty and a warning is logged to LogRevvySubsystem.
 */
UCLASS(BlueprintType)
class REVVY_API USubsystemQueryService : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Enumerate all live UEngineSubsystem instances hosted by GEngine.
	 *
	 * @return JSON array of {class_path, class_name}. Empty array if
	 *         GEngine is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Subsystem|Query")
	static FString ListEngineSubsystems();

	/**
	 * Enumerate all live UEditorSubsystem instances hosted by GEditor.
	 *
	 * @return JSON array of {class_path, class_name}. Empty array if
	 *         GEditor is null or the build is non-editor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Subsystem|Query")
	static FString ListEditorSubsystems();

	/**
	 * Enumerate all live UGameInstanceSubsystem instances hosted by
	 * the current editor world's UGameInstance.
	 *
	 * @return JSON array of {class_path, class_name}. Empty array if
	 *         no World / no GameInstance is available.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Subsystem|Query")
	static FString ListGameInstanceSubsystems();

	/**
	 * Enumerate all live UWorldSubsystem instances hosted by the
	 * current editor world.
	 *
	 * @return JSON array of {class_path, class_name}. Empty array if
	 *         no World is available.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Subsystem|Query")
	static FString ListWorldSubsystems();
};
