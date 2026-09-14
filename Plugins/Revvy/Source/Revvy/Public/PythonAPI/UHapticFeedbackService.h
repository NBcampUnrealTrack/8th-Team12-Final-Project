// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UHapticFeedbackService.generated.h"

/**
 * Haptic Feedback service exposed directly to Python for runtime playback
 * of UHapticFeedbackEffect_Base assets and force-feedback rumble on the
 * target APlayerController.
 *
 * Mirrors the UCameraShakeService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.HapticFeedbackService.<snake_case_name>(...)`.
 *
 * This is a runtime state service — no FScopedTransaction is required
 * because haptic / rumble effects are transient input-hardware effects
 * (not persisted asset / actor state).
 *
 * HandStr parameter accepts (case-insensitive):
 *   "Left"  → EControllerHand::Left
 *   "Right" → EControllerHand::Right
 *   "Gun"   → EControllerHand::Gun
 *   (any other value falls back to EControllerHand::Left).
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.HapticFeedbackService
 *
 *   svc.play_haptic_effect("PC0", "/Game/FX/HF_Recoil.HF_Recoil", "Right", 1.0)
 *   svc.stop_haptic_effect("PC0", "Right")
 *
 *   svc.play_dynamic_force_feedback("PC0", 1.0, 0.25)  # intensity, duration
 *   svc.set_force_feedback_values("PC0", 0.6, 0.9)     # large_motor, small_motor
 *   svc.stop_all_force_feedback("PC0")
 *
 * @note SetForceFeedbackValues is implemented on top of PlayDynamicForceFeedback
 *       — the underlying APlayerController::ForceFeedbackValues field is protected
 *       state, so this service issues a short dynamic rumble whose Intensity is
 *       max(LargeMotor, SmallMotor). This is a rumble-strength approximation of
 *       the direct large/small-motor set operation.
 *
 * @note All 5 haptic feedback operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UHapticFeedbackService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Haptic Effect Playback
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Play a UHapticFeedbackEffect_Base asset on the target
	 * APlayerController's controller hand.
	 *
	 * @param PlayerControllerActorNameOrPath  Actor label / name / path of
	 *                                         the target APlayerController.
	 * @param EffectAssetPath                  Full path to the haptic effect
	 *                                         asset (e.g. "/Game/FX/HF_Foo.HF_Foo").
	 * @param HandStr                          "Left" | "Right" | "Gun"
	 *                                         (case-insensitive; unknown → Left).
	 * @param Scale                            Scale to apply to the effect amplitude.
	 * @return True if the effect asset loaded and the request was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Haptic|Play")
	static bool PlayHapticEffect(
		const FString& PlayerControllerActorNameOrPath,
		const FString& EffectAssetPath,
		const FString& HandStr,
		float Scale);

	/**
	 * Stop any playing haptic effect on the target hand.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param HandStr                          "Left" | "Right" | "Gun".
	 * @return True if the stop request was dispatched.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Haptic|Stop")
	static bool StopHapticEffect(
		const FString& PlayerControllerActorNameOrPath,
		const FString& HandStr);

	// ═══════════════════════════════════════════════════════════════════
	// Force Feedback (Rumble)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Apply large / small motor rumble strengths on the target controller.
	 *
	 * Implemented via a short PlayDynamicForceFeedback call
	 * (Intensity = max(Large, Small), Duration = 0.1s) because
	 * APlayerController::ForceFeedbackValues is protected state.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param LargeMotor                       Large motor amplitude 0..1.
	 * @param SmallMotor                       Small motor amplitude 0..1.
	 * @return True if dispatched to a valid PlayerController.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Haptic|ForceFeedback")
	static bool SetForceFeedbackValues(
		const FString& PlayerControllerActorNameOrPath,
		float LargeMotor,
		float SmallMotor);

	/**
	 * Stop every currently playing force-feedback effect on the target
	 * controller (equivalent to ClientStopForceFeedback with no tag).
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @return True if dispatched to a valid PlayerController.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Haptic|ForceFeedback")
	static bool StopAllForceFeedback(const FString& PlayerControllerActorNameOrPath);

	/**
	 * Play a dynamic (procedural) force-feedback rumble on the target
	 * controller for the given duration. Affects left+right large+small
	 * motors, does not affect triggers.
	 *
	 * @param PlayerControllerActorNameOrPath  Target APlayerController.
	 * @param Intensity                        Rumble intensity 0..1.
	 * @param Duration                         Duration in seconds (<0 = infinite until stopped).
	 * @return True if dispatched to a valid PlayerController.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Haptic|ForceFeedback")
	static bool PlayDynamicForceFeedback(
		const FString& PlayerControllerActorNameOrPath,
		float Intensity,
		float Duration);
};
