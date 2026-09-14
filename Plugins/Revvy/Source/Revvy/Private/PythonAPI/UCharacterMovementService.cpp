// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UCharacterMovementService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCharMove, Log, All);

#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	/** Resolve an actor query to an ACharacter and return its UCharacterMovementComponent. */
	UCharacterMovementComponent* FindCharacterMovementComponent(
		AActor* Actor,
		const TCHAR* Context,
		const FString& Query)
	{
		if (!Actor)
		{
			UE_LOG(LogRevvyCharMove, Warning,
				TEXT("%s: actor '%s' not found."), Context, *Query);
			return nullptr;
		}

		ACharacter* Character = Cast<ACharacter>(Actor);
		if (!Character)
		{
			UE_LOG(LogRevvyCharMove, Warning,
				TEXT("%s: actor '%s' is not an ACharacter."), Context, *Query);
			return nullptr;
		}

		UCharacterMovementComponent* Comp = Character->GetCharacterMovement();
		if (!Comp)
		{
			UE_LOG(LogRevvyCharMove, Warning,
				TEXT("%s: character '%s' has no UCharacterMovementComponent."),
				Context, *Query);
			return nullptr;
		}

		return Comp;
	}
}

// =================================================================
// Speed
// =================================================================

bool UCharacterMovementService::SetMaxWalkSpeed(
	const FString& CharacterActorNameOrPath,
	float MaxWalkSpeed)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CharacterActorNameOrPath);
	UCharacterMovementComponent* Comp = FindCharacterMovementComponent(
		Actor, TEXT("SetMaxWalkSpeed"), CharacterActorNameOrPath);
	if (!Comp)
	{
		return false;
	}

	Comp->MaxWalkSpeed = MaxWalkSpeed;

	UE_LOG(LogRevvyCharMove, Log,
		TEXT("SetMaxWalkSpeed: '%s' MaxWalkSpeed=%f"),
		*CharacterActorNameOrPath, MaxWalkSpeed);
	return true;
}

// =================================================================
// Jump
// =================================================================

bool UCharacterMovementService::SetJumpZVelocity(
	const FString& CharacterActorNameOrPath,
	float JumpZVelocity)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CharacterActorNameOrPath);
	UCharacterMovementComponent* Comp = FindCharacterMovementComponent(
		Actor, TEXT("SetJumpZVelocity"), CharacterActorNameOrPath);
	if (!Comp)
	{
		return false;
	}

	Comp->JumpZVelocity = JumpZVelocity;

	UE_LOG(LogRevvyCharMove, Log,
		TEXT("SetJumpZVelocity: '%s' JumpZVelocity=%f"),
		*CharacterActorNameOrPath, JumpZVelocity);
	return true;
}

// =================================================================
// Gravity
// =================================================================

bool UCharacterMovementService::SetGravityScale(
	const FString& CharacterActorNameOrPath,
	float GravityScale)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CharacterActorNameOrPath);
	UCharacterMovementComponent* Comp = FindCharacterMovementComponent(
		Actor, TEXT("SetGravityScale"), CharacterActorNameOrPath);
	if (!Comp)
	{
		return false;
	}

	Comp->GravityScale = GravityScale;

	UE_LOG(LogRevvyCharMove, Log,
		TEXT("SetGravityScale: '%s' GravityScale=%f"),
		*CharacterActorNameOrPath, GravityScale);
	return true;
}

// =================================================================
// Crouch
// =================================================================

bool UCharacterMovementService::SetCrouchedHalfHeight(
	const FString& CharacterActorNameOrPath,
	float HalfHeight)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CharacterActorNameOrPath);
	UCharacterMovementComponent* Comp = FindCharacterMovementComponent(
		Actor, TEXT("SetCrouchedHalfHeight"), CharacterActorNameOrPath);
	if (!Comp)
	{
		return false;
	}

	Comp->SetCrouchedHalfHeight(HalfHeight);

	UE_LOG(LogRevvyCharMove, Log,
		TEXT("SetCrouchedHalfHeight: '%s' CrouchedHalfHeight=%f"),
		*CharacterActorNameOrPath, HalfHeight);
	return true;
}

// =================================================================
// Query
// =================================================================

FString UCharacterMovementService::GetCharacterMovementInfo(
	const FString& CharacterActorNameOrPath)
{
	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, CharacterActorNameOrPath);
	UCharacterMovementComponent* Comp = FindCharacterMovementComponent(
		Actor, TEXT("GetCharacterMovementInfo"), CharacterActorNameOrPath);
	if (!Comp)
	{
		return TEXT("{}");
	}

	ACharacter* Character = Cast<ACharacter>(Actor);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("max_walk_speed"), Comp->MaxWalkSpeed);
	Root->SetNumberField(TEXT("max_walk_speed_crouched"), Comp->MaxWalkSpeedCrouched);
	Root->SetNumberField(TEXT("jump_z_velocity"), Comp->JumpZVelocity);
	Root->SetNumberField(TEXT("gravity_scale"), Comp->GravityScale);
	Root->SetNumberField(TEXT("movement_mode"), static_cast<int32>(Comp->MovementMode.GetValue()));
	Root->SetBoolField(TEXT("is_falling"), Comp->IsFalling());
	Root->SetBoolField(TEXT("is_crouching"), Character ? Character->bIsCrouched : false);

	const FVector Velocity = Comp->Velocity;
	TSharedPtr<FJsonObject> VelObj = MakeShared<FJsonObject>();
	VelObj->SetNumberField(TEXT("x"), Velocity.X);
	VelObj->SetNumberField(TEXT("y"), Velocity.Y);
	VelObj->SetNumberField(TEXT("z"), Velocity.Z);
	Root->SetObjectField(TEXT("current_velocity"), VelObj);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogRevvyCharMove, Verbose,
		TEXT("GetCharacterMovementInfo: '%s' -> %s"),
		*CharacterActorNameOrPath, *Output);
	return Output;
}
