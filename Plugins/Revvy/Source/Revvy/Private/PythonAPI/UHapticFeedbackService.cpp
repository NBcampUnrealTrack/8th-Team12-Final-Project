// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UHapticFeedbackService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyHaptic, Log, All);

// PlayerController + Haptics + ForceFeedback
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "InputCoreTypes.h"

// Shared helpers (player controller + world resolution)
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/** Parse a hand string ("Left"|"Right"|"Gun", case-insensitive). Unknown → Left. */
	EControllerHand ParseControllerHand(const FString& HandStr)
	{
		const FString Lower = HandStr.ToLower();
		if (Lower == TEXT("right"))
		{
			return EControllerHand::Right;
		}
		if (Lower == TEXT("gun"))
		{
			return EControllerHand::Gun;
		}
		if (Lower != TEXT("left"))
		{
			UE_LOG(LogRevvyHaptic, Warning,
				TEXT("ParseControllerHand: unknown hand '%s' — defaulting to Left"), *HandStr);
		}
		return EControllerHand::Left;
	}

	/** Load a UHapticFeedbackEffect_Base asset by path. */
	UHapticFeedbackEffect_Base* LoadHapticEffect(const FString& EffectAssetPath)
	{
		if (EffectAssetPath.IsEmpty())
		{
			return nullptr;
		}

		UHapticFeedbackEffect_Base* Effect = LoadObject<UHapticFeedbackEffect_Base>(nullptr, *EffectAssetPath);
		if (!Effect)
		{
			UE_LOG(LogRevvyHaptic, Warning,
				TEXT("LoadHapticEffect: failed to load '%s'"), *EffectAssetPath);
		}
		return Effect;
	}
}

// =================================================================
// Haptic Effect Playback
// =================================================================

bool UHapticFeedbackService::PlayHapticEffect(
	const FString& PlayerControllerActorNameOrPath,
	const FString& EffectAssetPath,
	const FString& HandStr,
	float Scale)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("PlayHapticEffect: PlayerController '%s' not found (need active PIE?)"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	UHapticFeedbackEffect_Base* Effect = LoadHapticEffect(EffectAssetPath);
	if (!Effect)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("PlayHapticEffect: haptic effect '%s' failed to load"), *EffectAssetPath);
		return false;
	}

	const EControllerHand Hand = ParseControllerHand(HandStr);
	PC->PlayHapticEffect(Effect, Hand, Scale, /*bLoop=*/ false);
	return true;
}

bool UHapticFeedbackService::StopHapticEffect(
	const FString& PlayerControllerActorNameOrPath,
	const FString& HandStr)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("StopHapticEffect: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	const EControllerHand Hand = ParseControllerHand(HandStr);
	PC->StopHapticEffect(Hand);
	return true;
}

// =================================================================
// Force Feedback (Rumble)
// =================================================================

bool UHapticFeedbackService::SetForceFeedbackValues(
	const FString& PlayerControllerActorNameOrPath,
	float LargeMotor,
	float SmallMotor)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("SetForceFeedbackValues: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	// APlayerController::ForceFeedbackValues is protected state; approximate
	// large/small motor amplitudes via a short dynamic force-feedback pulse
	// whose intensity is the stronger of the two motor requests.
	const float Intensity = FMath::Max(LargeMotor, SmallMotor);
	PC->PlayDynamicForceFeedback(
		Intensity,
		/*Duration=*/ 0.1f,
		/*bAffectsLeftLarge=*/ true,
		/*bAffectsLeftSmall=*/ true,
		/*bAffectsRightLarge=*/ true,
		/*bAffectsRightSmall=*/ true,
		EDynamicForceFeedbackAction::Start);
	return true;
}

bool UHapticFeedbackService::StopAllForceFeedback(const FString& PlayerControllerActorNameOrPath)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("StopAllForceFeedback: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	PC->ClientStopForceFeedback(/*ForceFeedbackEffect=*/ nullptr, /*Tag=*/ NAME_None);
	return true;
}

bool UHapticFeedbackService::PlayDynamicForceFeedback(
	const FString& PlayerControllerActorNameOrPath,
	float Intensity,
	float Duration)
{
	APlayerController* PC = URevvyCommonHelpers::FindPlayerControllerByNameOrPath(nullptr, PlayerControllerActorNameOrPath);
	if (!PC)
	{
		UE_LOG(LogRevvyHaptic, Warning,
			TEXT("PlayDynamicForceFeedback: PlayerController '%s' not found"),
			*PlayerControllerActorNameOrPath);
		return false;
	}

	PC->PlayDynamicForceFeedback(
		Intensity,
		Duration,
		/*bAffectsLeftLarge=*/ true,
		/*bAffectsLeftSmall=*/ true,
		/*bAffectsRightLarge=*/ true,
		/*bAffectsRightSmall=*/ true,
		EDynamicForceFeedbackAction::Start);
	return true;
}
