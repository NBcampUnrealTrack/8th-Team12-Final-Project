// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGameplayCueService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGameplayCue, Log, All);

// Engine
#include "GameFramework/Actor.h"

// GAS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "GameplayCueManager.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Helpers
// =================================================================

namespace
{
	UAbilitySystemComponent* GameplayCueResolveActorASC(const FString& ActorNameOrPath)
	{
		AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
		if (!Actor)
		{
			UE_LOG(LogRevvyGameplayCue, Warning, TEXT("Actor '%s' not found"), *ActorNameOrPath);
			return nullptr;
		}

		// UAbilitySystemGlobals::GetAbilitySystemComponentFromActor honours
		// IAbilitySystemInterface + component-search fallbacks internally.
		UAbilitySystemComponent* ASC =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*bLookForComponent*/ true);
		if (!ASC)
		{
			UE_LOG(LogRevvyGameplayCue, Warning,
				TEXT("Actor '%s' has no UAbilitySystemComponent"), *ActorNameOrPath);
			return nullptr;
		}
		return ASC;
	}

	/**
	 * Resolve a gameplay-cue tag by name. Cue tags are just FGameplayTag values,
	 * usually rooted at "GameplayCue.*". Returns invalid FGameplayTag on miss.
	 */
	FGameplayTag ResolveCueTag(const FString& CueTagName)
	{
		if (CueTagName.IsEmpty())
		{
			return FGameplayTag();
		}

		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*CueTagName), /*ErrorIfNotFound*/ false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogRevvyGameplayCue, Warning,
				TEXT("Gameplay cue tag '%s' is not registered"), *CueTagName);
		}
		return Tag;
	}

	/**
	 * UE 5.8 made UAbilitySystemComponent::ActiveGameplayCues protected with no public
	 * accessor for the FActiveGameplayCue entries (or their source Parameters). This
	 * access shim re-exposes the protected member via a using-declaration so read-only
	 * introspection code can enumerate the active cues.
	 */
	struct FActiveGameplayCuesAccessor : public UAbilitySystemComponent
	{
		using UAbilitySystemComponent::ActiveGameplayCues;
	};
}

// =================================================================
// Execute (fire-and-forget)
// =================================================================

bool UGameplayCueService::ExecuteGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName)
{
	UAbilitySystemComponent* ASC = GameplayCueResolveActorASC(TargetActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag CueTag = ResolveCueTag(CueTagName);
	if (!CueTag.IsValid())
	{
		return false;
	}

	ASC->ExecuteGameplayCue(CueTag, FGameplayEffectContextHandle());
	return true;
}

// =================================================================
// Persistent (add / remove pair)
// =================================================================

bool UGameplayCueService::AddGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName)
{
	UAbilitySystemComponent* ASC = GameplayCueResolveActorASC(TargetActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag CueTag = ResolveCueTag(CueTagName);
	if (!CueTag.IsValid())
	{
		return false;
	}

	ASC->AddGameplayCue(CueTag, FGameplayEffectContextHandle());
	return true;
}

bool UGameplayCueService::RemoveGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName)
{
	UAbilitySystemComponent* ASC = GameplayCueResolveActorASC(TargetActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag CueTag = ResolveCueTag(CueTagName);
	if (!CueTag.IsValid())
	{
		return false;
	}

	ASC->RemoveGameplayCue(CueTag);
	return true;
}

// =================================================================
// Query
// =================================================================

FString UGameplayCueService::ListActiveGameplayCues(const FString& TargetActorNameOrPath)
{
	UAbilitySystemComponent* ASC = GameplayCueResolveActorASC(TargetActorNameOrPath);
	if (!ASC)
	{
		return TEXT("[]");
	}

	TArray<TSharedPtr<FJsonValue>> Out;

	// FActiveGameplayCueContainer::GameplayCues is a public array; the owning
	// ActiveGameplayCues member is protected in UE 5.8, so reach it via the access shim.
	const FActiveGameplayCueContainer& Container =
		static_cast<const FActiveGameplayCuesAccessor*>(ASC)->ActiveGameplayCues;
	for (const FActiveGameplayCue& ActiveCue : Container.GameplayCues)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("cue_tag"), ActiveCue.GameplayCueTag.ToString());

		// Source display: prefer EffectCauser, fall back to Instigator, then SourceObject
		FString SourceName;
		if (AActor* Causer = ActiveCue.Parameters.EffectCauser.Get())
		{
			SourceName = Causer->GetName();
		}
		else if (AActor* Instigator = ActiveCue.Parameters.Instigator.Get())
		{
			SourceName = Instigator->GetName();
		}
		else if (const UObject* SourceObject = ActiveCue.Parameters.SourceObject.Get())
		{
			SourceName = SourceObject->GetName();
		}
		Obj->SetStringField(TEXT("source"), SourceName);

		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Result;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(Out, Writer);
	return Result;
}
