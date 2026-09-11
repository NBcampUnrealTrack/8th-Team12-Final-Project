// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UAIPerceptionService.generated.h"

/**
 * AI Perception service exposed directly to Python for authoring
 * UAIPerceptionComponent sight / hearing configuration and reporting
 * runtime stimulus events (noise, sight source registration).
 *
 * Mirrors the UCameraShakeService / UPerformanceService pattern: all
 * UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.AIPerceptionService.<snake_case_name>(...)`.
 *
 * This is a runtime state service — no FScopedTransaction is used because
 * stimulus reporting and perception state updates are transient runtime
 * events (not persisted asset / actor state).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.AIPerceptionService
 *
 *   # Config
 *   svc.set_sight_radius("EnemyBP0", 1500.0, 1700.0, 90.0)
 *   svc.set_hearing_range("EnemyBP0", 2000.0)
 *
 *   info = json.loads(svc.get_perception_component_info("EnemyBP0"))
 *
 *   # Stimulus
 *   svc.report_noise_event("PC0", unreal.Vector(100, 200, 0), 1.0, 3000.0)
 *   svc.report_sight_event("Player0", unreal.Vector(0, 0, 90))
 *
 *   # Query
 *   perceived = json.loads(svc.get_perceived_actors("EnemyBP0", "Sight"))
 */
UCLASS(BlueprintType)
class REVVY_API UAIPerceptionService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Query — configuration & inspection
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return a JSON snapshot of the UAIPerceptionComponent attached to the
	 * given actor, including sight and hearing config and registered senses.
	 *
	 * @param ActorNameOrPath  Actor label / name / path.
	 * @return JSON:
	 *   {
	 *     "sight_config":   { "radius": float, "lose_sight": float, "peripheral_angle": float },
	 *     "hearing_config": { "range":  float },
	 *     "senses":         [ "Sight", "Hearing", ... ]
	 *   }
	 *   Returns "{}" when no perception component exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Query")
	static FString GetPerceptionComponentInfo(const FString& ActorNameOrPath);

	/**
	 * List the actors currently perceived by an AI's perception component
	 * for a specific sense (or all senses).
	 *
	 * @param AIActorNameOrPath  Owner actor of the UAIPerceptionComponent.
	 * @param SenseNameStr       "Sight" | "Hearing" | "All".
	 * @return JSON array of { "actor_label": str, "class": str }.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Query")
	static FString GetPerceivedActors(const FString& AIActorNameOrPath, const FString& SenseNameStr);

	// ═══════════════════════════════════════════════════════════════════
	// Config
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Update the UAISenseConfig_Sight parameters on the target actor's
	 * perception component and request a stimuli listener refresh so the
	 * change takes effect in the AI perception system.
	 *
	 * @return True on success, false if actor / component / sight config missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Config")
	static bool SetSightRadius(
		const FString& AIActorNameOrPath,
		float SightRadius,
		float LoseSightRadius,
		float PeripheralVisionAngleDeg);

	/**
	 * Update the UAISenseConfig_Hearing::HearingRange on the target actor's
	 * perception component and refresh the stimuli listener.
	 *
	 * @return True on success, false if actor / component / hearing config missing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Config")
	static bool SetHearingRange(const FString& AIActorNameOrPath, float HearingRange);

	// ═══════════════════════════════════════════════════════════════════
	// Stimulus
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Broadcast a hearing stimulus to the AI perception system using
	 * UAISense_Hearing::ReportNoiseEvent.
	 *
	 * @param InstigatorActorNameOrPath  Instigator (may be empty; falls back to nullptr).
	 * @param NoiseLocation              World-space noise origin.
	 * @param Loudness                   Sense-space loudness (typically ~1.0).
	 * @param MaxRange                   Optional max range in cm (0 = default).
	 * @return True if dispatched to a valid perception system.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Stimulus")
	static bool ReportNoiseEvent(
		const FString& InstigatorActorNameOrPath,
		FVector NoiseLocation,
		float Loudness,
		float MaxRange);

	/**
	 * Register a target actor as a passive sight source with the perception
	 * system so any UAIPerceptionComponent configured with sight can
	 * evaluate it. UAISense_Sight is passively evaluated — there is no
	 * direct "ReportSightEvent" API in the engine, so this uses
	 * UAIPerceptionSystem::RegisterSource<UAISense_Sight>(Target).
	 *
	 * @param TargetActorNameOrPath  Actor to register as a sight source.
	 * @param TargetLocation         Advisory — unused by the engine registration
	 *                               (sight evaluates the actor's live location).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AIPerception|Stimulus")
	static bool ReportSightEvent(const FString& TargetActorNameOrPath, FVector TargetLocation);
};
