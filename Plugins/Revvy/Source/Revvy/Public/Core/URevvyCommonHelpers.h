// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "URevvyCommonHelpers.generated.h"

class AActor;
class APlayerController;
class UWorld;

/**
 * Shared static utility helpers for Revvy Python-facing services.
 *
 * Consolidates the actor lookup, player controller lookup, class loading,
 * and editor world access helpers that were previously duplicated
 * across ~23 individual *Service.cpp translation units. Migration is
 * opt-in — existing services are NOT modified by adding this class.
 *
 * All UFUNCTIONs are static + BlueprintCallable so they are reachable
 * from Python as `unreal.RevvyCommonHelpers.<snake_case_name>(...)`.
 *
 * @note Editor-only. Requires GEditor + UEditorActorSubsystem.
 */
UCLASS(BlueprintType)
class REVVY_API URevvyCommonHelpers : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Resolve an actor by (a) case-insensitive GetActorLabel exact match,
	 * (b) GetName exact match, (c) case-insensitive contains-on-label,
	 * (d) FSoftObjectPath resolve, (e) UEditorAssetLibrary::LoadAsset.
	 *
	 * @param WorldContextObject Any UObject bound to a UWorld (may be null → GEditor world).
	 * @param NameOrPath         Actor label, actor name, soft object path, or asset package path.
	 * @return Matching AActor* or nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers", meta=(WorldContext="WorldContextObject"))
	static AActor* FindActorByNameOrPath(UObject* WorldContextObject, const FString& NameOrPath);

	/**
	 * Find a player controller via FindActorByNameOrPath + Cast<APlayerController>.
	 * If NameOrPath is empty, returns the world's first PlayerController.
	 *
	 * @param WorldContextObject Any UObject bound to a UWorld (may be null → GEditor world).
	 * @param NameOrPath         Optional PC identifier; empty string → first PC.
	 * @return Matching APlayerController* or nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers", meta=(WorldContext="WorldContextObject"))
	static APlayerController* FindPlayerControllerByNameOrPath(UObject* WorldContextObject, const FString& NameOrPath);

	/**
	 * Load a UClass* from a path with common fallback strategies:
	 *   1. LoadClass<UObject>(nullptr, *ClassPath)
	 *   2. Same call with "_C" appended (Blueprint generated class suffix)
	 *   3. StaticLoadObject(UClass::StaticClass(), ...)
	 * Validates IsChildOf(RequiredBaseClass) when RequiredBaseClass is non-null.
	 *
	 * @param ClassPath         Package path to the class (e.g. "/Game/BP_Foo.BP_Foo_C").
	 * @param RequiredBaseClass Optional required base; if provided and mismatched, returns nullptr.
	 * @return Loaded UClass* or nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers")
	static UClass* LoadClassWithFallback(const FString& ClassPath, UClass* RequiredBaseClass);

	/**
	 * Return the current editor world, preferring the active PIE world when
	 * PIE is running so runtime queries (e.g. player controllers) work.
	 *
	 * @return UWorld* (PIE PlayWorld > editor world context) or nullptr.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers")
	static UWorld* GetEditorWorldSafe();

	/**
	 * Marker UFUNCTION signaling that URevvyCommonHelpers is available for
	 * migration in this build. Callers (Python side, other services) can
	 * grep the reflection registry for this name as an availability probe.
	 *
	 * @return JSON string documenting migration readiness.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Meta")
	static FString IsHelperMigrationReady();

	/**
	 * Build a condensed JSON error payload of the form
	 *   {"error":"<ErrorMessage>"[,"<ExtraKey>":"<ExtraValue>"]}
	 * When ExtraKey is empty, no extra field is written.
	 *
	 * @param ErrorMessage Human-readable error message.
	 * @param ExtraKey     Optional extra string key (skipped when empty).
	 * @param ExtraValue   Value paired with ExtraKey.
	 * @return Condensed JSON string.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Json")
	static FString MakeErrorJson(const FString& ErrorMessage, const FString& ExtraKey, const FString& ExtraValue);

	/**
	 * Build a condensed JSON object encoding an FVector as
	 *   {"x":<X>,"y":<Y>,"z":<Z>}.
	 *
	 * @param V Vector to serialize.
	 * @return Condensed JSON string.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Json")
	static FString MakeVectorJson(FVector V);

	/**
	 * Build a condensed JSON object encoding an FRotator as
	 *   {"pitch":<P>,"yaw":<Y>,"roll":<R>}.
	 *
	 * @param R Rotator to serialize.
	 * @return Condensed JSON string.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Json")
	static FString MakeRotatorJson(FRotator R);

	/**
	 * Build a condensed JSON object encoding an FTransform as
	 *   {"location":{...},"rotation":{...},"scale":{...}}.
	 *
	 * @param T Transform to serialize.
	 * @return Condensed JSON string.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Json")
	static FString MakeTransformJson(FTransform T);

	/**
	 * Build a condensed JSON object encoding an FLinearColor as
	 *   {"r":<R>,"g":<G>,"b":<B>,"a":<A>}.
	 *
	 * @param C Color to serialize.
	 * @return Condensed JSON string.
	 */
	UFUNCTION(BlueprintCallable, Category="Revvy|Helpers|Json")
	static FString MakeColorJson(FLinearColor C);
};
