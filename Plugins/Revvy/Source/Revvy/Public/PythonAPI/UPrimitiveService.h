// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UPrimitiveService.generated.h"

/**
 * Result of spawning a primitive actor into the editor world.
 */
USTRUCT(BlueprintType)
struct FPrimitiveSpawnResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Primitive")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Primitive")
	FString ActorLabel;

	UPROPERTY(BlueprintReadWrite, Category = "Primitive")
	FString ActorPath;

	UPROPERTY(BlueprintReadWrite, Category = "Primitive")
	FString PrimitiveType;

	UPROPERTY(BlueprintReadWrite, Category = "Primitive")
	FString ErrorMessage;
};

/**
 * Primitive geometry service exposed directly to Python.
 *
 * Mirrors the Epic-native PrimitiveTools toolset for adding basic shape geometry
 * (Cube, Sphere, Cylinder, Cone, Plane) into the active editor world. Operations
 * use GEditor->GetEditorWorldContext().World() and the engine basic shape meshes
 * under /Engine/BasicShapes/.
 *
 * Provides the following actions:
 * - spawn_primitive_actor: Spawn an AStaticMeshActor with a basic shape mesh
 * - add_primitive_component_to_actor: Add a UStaticMeshComponent (basic shape) to an existing actor
 * - list_primitive_types: List the supported primitive type names
 *
 * Python Usage:
 *   import unreal
 *
 *   # List supported primitive types
 *   types = unreal.PrimitiveService.list_primitive_types()
 *   # -> ["Cube", "Sphere", "Cylinder", "Cone", "Plane"]
 *
 *   # Spawn a cube at the origin
 *   path = unreal.PrimitiveService.spawn_primitive_actor(
 *       "Cube",
 *       unreal.Vector(0.0, 0.0, 0.0),
 *       unreal.Rotator(0.0, 0.0, 0.0),
 *       unreal.Vector(1.0, 1.0, 1.0),
 *       "MyCube")
 *   print(path)
 *
 *   # Add a sphere component to an existing actor (by label or name)
 *   comp = unreal.PrimitiveService.add_primitive_component_to_actor(
 *       "MyCube", "Sphere", "ExtraSphere")
 *   print(comp)
 *
 * @note This mirrors the Epic-native PrimitiveTools MCP toolset.
 */
UCLASS(BlueprintType)
class REVVY_API UPrimitiveService : public UObject
{
	GENERATED_BODY()

public:
	// ============================================
	// Spawn Actions
	// ============================================

	/**
	 * Spawn a primitive AStaticMeshActor into the editor world using a basic shape mesh.
	 *
	 * @param PrimitiveType - One of: Cube, Sphere, Cylinder, Cone, Plane (case-insensitive)
	 * @param Location - World location for the new actor
	 * @param Rotation - World rotation for the new actor
	 * @param Scale - World scale for the new actor
	 * @param ActorLabel - Optional editor label for the actor (auto-generated if empty)
	 * @return The spawned actor's path/label, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Primitive")
	static FString SpawnPrimitiveActor(
		const FString& PrimitiveType,
		FVector Location = FVector::ZeroVector,
		FRotator Rotation = FRotator::ZeroRotator,
		FVector Scale = FVector(1.0f, 1.0f, 1.0f),
		const FString& ActorLabel = TEXT(""));

	// ============================================
	// Component Actions
	// ============================================

	/**
	 * Add a primitive UStaticMeshComponent (basic shape) to an existing actor in the editor world.
	 * The actor is located by its editor label or object name.
	 *
	 * @param ActorPathOrLabel - The actor's editor label, object name, or path
	 * @param PrimitiveType - One of: Cube, Sphere, Cylinder, Cone, Plane (case-insensitive)
	 * @param ComponentName - Name for the new component
	 * @return The created component name, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Primitive")
	static FString AddPrimitiveComponentToActor(
		const FString& ActorPathOrLabel,
		const FString& PrimitiveType,
		const FString& ComponentName);

	// ============================================
	// Discovery Actions
	// ============================================

	/**
	 * List the supported primitive type names.
	 *
	 * @return Array of supported type names: Cube, Sphere, Cylinder, Cone, Plane
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Primitive")
	static TArray<FString> ListPrimitiveTypes();

private:
	/** Resolve the engine basic shape static mesh for a primitive type name (/Engine/BasicShapes/<X>.<X>). */
	static class UStaticMesh* ResolveBasicShape(const FString& Type);

	/** Find an actor in the editor world by editor label, object name, or path. */
	static class AActor* FindActorInEditorWorld(const FString& ActorPathOrLabel);

	/** Get the active editor world (GEditor->GetEditorWorldContext().World()). */
	static class UWorld* GetEditorWorld();
};
