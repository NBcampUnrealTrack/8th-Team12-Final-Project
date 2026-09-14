// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "UPhysicsHandleService.generated.h"

/**
 * PhysicsHandle service exposed directly to Python for grabbing, moving, and
 * releasing physics bodies in the current editor world.
 *
 * Mirrors the UActorPoolService pattern: a named-handle registry
 * (TMap<FString, TWeakObjectPtr<UPhysicsHandleComponent>>) is held in the .cpp
 * anon namespace, and every UFUNCTION is static + BlueprintCallable so it is
 * reachable from Python as
 * `unreal.PhysicsHandleService.<snake_case_name>(...)`.
 *
 * State model:
 *   Each SpawnPhysicsHandle creates a UPhysicsHandleComponent that is owned by
 *   an actor already present in the editor world. The component is registered
 *   with the world and tracked by a caller-supplied HandleName. Weak refs are
 *   used so that if the owning actor is destroyed the entry naturally goes
 *   stale (calls that encounter a stale entry log a warning and fail).
 *
 *   No FScopedTransaction is used — physics handle state is runtime only and
 *   undo/redo does not make sense for grab/move/release.
 *
 * World scope:
 *   Handles live on their owning actor's world (usually the editor world
 *   returned by `GEditor->GetEditorWorldContext().World()`). Callers that
 *   grab PIE actors should spawn the handle on that PIE actor.
 *
 * Threading:
 *   GameThread only. Registry access is not guarded.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.PhysicsHandleService
 *
 *   svc.spawn_physics_handle("PickupRig", "MainHandle")
 *
 *   svc.grab_object(
 *       "MainHandle", "Crate_01", "",
 *       unreal.Vector(0, 0, 100),
 *       unreal.Rotator(0, 0, 0))
 *
 *   svc.move_grabbed_object(
 *       "MainHandle",
 *       unreal.Vector(200, 0, 150),
 *       unreal.Rotator(0, 45, 0))
 *
 *   svc.release_object("MainHandle")
 */
UCLASS(BlueprintType)
class REVVY_API UPhysicsHandleService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Lifecycle
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new UPhysicsHandleComponent on the given owner actor, register it
	 * with the world, and add it to the registry under HandleName.
	 *
	 * @param OwnerActorNameOrPath - Label / name / soft path of the actor that will own the handle.
	 * @param HandleName           - Unique registry key for this handle. Empty / duplicate → rejected.
	 * @return The HandleName on success, empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsHandle|Lifecycle")
	static FString SpawnPhysicsHandle(const FString& OwnerActorNameOrPath, const FString& HandleName);

	// ═══════════════════════════════════════════════════════════════════
	// Grab
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Grab the target actor's root primitive component with the named handle at
	 * the supplied world-space location and rotation.
	 *
	 * @param HandleName             - Registered handle name.
	 * @param TargetActorNameOrPath  - Label / name / soft path of the actor to grab.
	 * @param BoneName               - Optional bone name (skeletal targets); use "" for simple bodies.
	 * @param GrabLocation           - World-space grab point.
	 * @param GrabRotation           - World-space grab rotation.
	 * @return True on successful grab.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsHandle|Grab")
	static bool GrabObject(
		const FString& HandleName,
		const FString& TargetActorNameOrPath,
		const FString& BoneName,
		FVector GrabLocation,
		FRotator GrabRotation);

	// ═══════════════════════════════════════════════════════════════════
	// Move
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Update the target location and rotation of a currently grabbed body.
	 *
	 * @param HandleName    - Registered handle name.
	 * @param NewLocation   - New world-space target location.
	 * @param NewRotation   - New world-space target rotation.
	 * @return True on successful update.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsHandle|Move")
	static bool MoveGrabbedObject(
		const FString& HandleName,
		FVector NewLocation,
		FRotator NewRotation);

	// ═══════════════════════════════════════════════════════════════════
	// Release
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Release the currently grabbed component (if any) held by the named handle.
	 *
	 * @param HandleName - Registered handle name.
	 * @return True on successful release.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|PhysicsHandle|Release")
	static bool ReleaseObject(const FString& HandleName);
};
