// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAudioComponentService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyAudioComp, Log, All);

#include "Engine/World.h"
#include "UObject/SoftObjectPath.h"
#include "EditorAssetLibrary.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"    // EAttachLocation
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	/** Load a USoundBase from a package path. */
	USoundBase* LoadSoundChecked(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			UE_LOG(LogRevvyAudioComp, Warning, TEXT("Sound asset path is empty."));
			return nullptr;
		}

		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		if (!Loaded)
		{
			// Fallback: soft object path try-load
			Loaded = FSoftObjectPath(AssetPath).TryLoad();
		}

		USoundBase* Sound = Cast<USoundBase>(Loaded);
		if (!Sound)
		{
			UE_LOG(LogRevvyAudioComp, Warning,
				TEXT("Failed to load USoundBase at '%s'."), *AssetPath);
			return nullptr;
		}
		return Sound;
	}

	/** Return the first UAudioComponent owned by Actor, or nullptr. */
	UAudioComponent* FindAudioComponent(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}
		return Actor->FindComponentByClass<UAudioComponent>();
	}
}

// =================================================================
// Spawn
// =================================================================

FString UAudioComponentService::SpawnSoundAtLocation(
	const FString& SoundAssetPath,
	FVector Location,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	UWorld* World = URevvyCommonHelpers::GetEditorWorldSafe();
	if (!World)
	{
		UE_LOG(LogRevvyAudioComp, Warning, TEXT("SpawnSoundAtLocation: no editor world."));
		return FString();
	}

	USoundBase* Sound = LoadSoundChecked(SoundAssetPath);
	if (!Sound)
	{
		return FString();
	}

	// Spawn a lightweight owning actor so we get a stable label/handle for
	// later param tweaks and stop calls. UGameplayStatics::SpawnSoundAtLocation
	// creates its own auto-destroy owner and the returned component may go
	// away shortly after playback ends — the manual spawn gives Python a
	// reliable actor label as long as playback is active (bAutoDestroy still
	// tears the actor down on completion).
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), TEXT("Revvy_AudioSpawn"));

	AActor* AudioActor = World->SpawnActor<AActor>(
		AActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!AudioActor)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SpawnSoundAtLocation: failed to spawn owning actor for '%s'."),
			*SoundAssetPath);
		return FString();
	}

	USceneComponent* Root = NewObject<USceneComponent>(
		AudioActor, USceneComponent::StaticClass(), TEXT("SceneRoot"));
	AudioActor->SetRootComponent(Root);
	Root->RegisterComponent();
	AudioActor->SetActorLocation(Location);

	UAudioComponent* Comp = NewObject<UAudioComponent>(AudioActor);
	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SpawnSoundAtLocation: failed to allocate UAudioComponent."));
		AudioActor->Destroy();
		return FString();
	}

	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Comp->SetSound(Sound);
	Comp->SetVolumeMultiplier(VolumeMultiplier);
	Comp->SetPitchMultiplier(PitchMultiplier);
	Comp->bAutoDestroy = true;
	Comp->Play();

#if WITH_EDITOR
	AudioActor->SetActorLabel(TEXT("Revvy_AudioSpawn"));
#endif

	UE_LOG(LogRevvyAudioComp, Log,
		TEXT("Spawned sound '%s' at %s -> owner '%s' (vol=%f pitch=%f)."),
		*SoundAssetPath, *Location.ToString(),
		*AudioActor->GetActorLabel(), VolumeMultiplier, PitchMultiplier);

	return AudioActor->GetActorLabel();
}

FString UAudioComponentService::SpawnSoundAttached(
	const FString& SoundAssetPath,
	const FString& TargetActorNameOrPath,
	const FString& SocketName,
	float VolumeMultiplier,
	float PitchMultiplier)
{
	USoundBase* Sound = LoadSoundChecked(SoundAssetPath);
	if (!Sound)
	{
		return FString();
	}

	AActor* Target = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, TargetActorNameOrPath);
	if (!Target)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SpawnSoundAttached: target actor '%s' not found."),
			*TargetActorNameOrPath);
		return FString();
	}

	USceneComponent* AttachTo = Target->GetRootComponent();
	if (!AttachTo)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SpawnSoundAttached: target actor '%s' has no root scene component."),
			*TargetActorNameOrPath);
		return FString();
	}

	const FName Socket = SocketName.IsEmpty() ? NAME_None : FName(*SocketName);

	UAudioComponent* Comp = UGameplayStatics::SpawnSoundAttached(
		Sound,
		AttachTo,
		Socket,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		/*bStopWhenAttachedToDestroyed=*/ true,
		VolumeMultiplier,
		PitchMultiplier);

	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SpawnSoundAttached: SpawnSoundAttached returned null for '%s' on '%s'."),
			*SoundAssetPath, *TargetActorNameOrPath);
		return FString();
	}

	AActor* Owner = Comp->GetOwner();
	if (!Owner)
	{
		// SpawnSoundAttached usually parents the component under the target
		// actor rather than creating a fresh owner — fall back to the target
		// label in that case.
		UE_LOG(LogRevvyAudioComp, Log,
			TEXT("SpawnSoundAttached: no distinct owner, returning target label '%s'."),
			*Target->GetActorLabel());
		return Target->GetActorLabel();
	}

	UE_LOG(LogRevvyAudioComp, Log,
		TEXT("Spawned sound '%s' attached to '%s' (socket '%s') -> owner '%s'."),
		*SoundAssetPath, *Target->GetActorLabel(),
		*Socket.ToString(), *Owner->GetActorLabel());

	return Owner->GetActorLabel();
}

// =================================================================
// Params
// =================================================================

bool UAudioComponentService::SetAudioComponentVolume(
	const FString& ActorNameOrPath,
	float VolumeMultiplier)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentVolume: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UAudioComponent* Comp = FindAudioComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentVolume: no UAudioComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	Comp->SetVolumeMultiplier(VolumeMultiplier);
	UE_LOG(LogRevvyAudioComp, Verbose,
		TEXT("SetAudioComponentVolume: '%s' = %f"), *ActorNameOrPath, VolumeMultiplier);
	return true;
}

bool UAudioComponentService::SetAudioComponentPitch(
	const FString& ActorNameOrPath,
	float PitchMultiplier)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentPitch: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UAudioComponent* Comp = FindAudioComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentPitch: no UAudioComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	Comp->SetPitchMultiplier(PitchMultiplier);
	UE_LOG(LogRevvyAudioComp, Verbose,
		TEXT("SetAudioComponentPitch: '%s' = %f"), *ActorNameOrPath, PitchMultiplier);
	return true;
}

bool UAudioComponentService::SetAudioComponentBoolParam(
	const FString& ActorNameOrPath,
	const FString& ParamName,
	bool Value)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentBoolParam: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UAudioComponent* Comp = FindAudioComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("SetAudioComponentBoolParam: no UAudioComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	// Note: SetBoolParameter is a legacy SoundCue-facing hook consumed by
	// SoundCue Branch nodes. MetaSoundSource graphs do not observe this
	// channel — bind MetaSound inputs via FMetaSoundOutput instead.
	Comp->SetBoolParameter(FName(*ParamName), Value);
	UE_LOG(LogRevvyAudioComp, Verbose,
		TEXT("SetAudioComponentBoolParam: '%s'.'%s' = %s"),
		*ActorNameOrPath, *ParamName, Value ? TEXT("true") : TEXT("false"));
	return true;
}

// =================================================================
// Control
// =================================================================

bool UAudioComponentService::StopAudioComponent(
	const FString& ActorNameOrPath,
	bool bFadeOut,
	float FadeDuration)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("StopAudioComponent: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	UAudioComponent* Comp = FindAudioComponent(Actor);
	if (!Comp)
	{
		UE_LOG(LogRevvyAudioComp, Warning,
			TEXT("StopAudioComponent: no UAudioComponent on '%s'."), *ActorNameOrPath);
		return false;
	}

	if (bFadeOut)
	{
		Comp->FadeOut(FadeDuration, 0.0f);
	}
	else
	{
		Comp->Stop();
	}

	UE_LOG(LogRevvyAudioComp, Log,
		TEXT("StopAudioComponent: '%s' (fade=%d duration=%f)."),
		*ActorNameOrPath, bFadeOut ? 1 : 0, FadeDuration);
	return true;
}
