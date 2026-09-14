// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGameplayAbilitySystemService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGAS, Log, All);

// Engine
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// GAS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
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
	UAbilitySystemComponent* ResolveASC(AActor* Actor)
	{
		if (!Actor)
		{
			return nullptr;
		}

		// Prefer IAbilitySystemInterface
		if (IAbilitySystemInterface* AsIface = Cast<IAbilitySystemInterface>(Actor))
		{
			if (UAbilitySystemComponent* ASC = AsIface->GetAbilitySystemComponent())
			{
				return ASC;
			}
		}

		// Fallback: global helper (searches by component & PlayerState)
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*bLookForComponent*/ true);
	}

	UAbilitySystemComponent* AbilitySystemResolveActorASC(const FString& ActorNameOrPath)
	{
		AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
		if (!Actor)
		{
			UE_LOG(LogRevvyGAS, Warning, TEXT("Actor '%s' not found"), *ActorNameOrPath);
			return nullptr;
		}

		UAbilitySystemComponent* ASC = ResolveASC(Actor);
		if (!ASC)
		{
			UE_LOG(LogRevvyGAS, Warning, TEXT("Actor '%s' has no UAbilitySystemComponent"), *ActorNameOrPath);
			return nullptr;
		}
		return ASC;
	}

	/**
	 * Resolve an FGameplayAttribute on the given ASC by name. Matches on
	 * FGameplayAttribute::GetName() case-insensitively, with fallback to
	 * "SetClass.AttrName" style. Returns invalid FGameplayAttribute on miss.
	 */
	FGameplayAttribute FindAttributeByName(UAbilitySystemComponent* ASC, const FString& AttributeName)
	{
		FGameplayAttribute NullAttr;
		if (!ASC || AttributeName.IsEmpty())
		{
			return NullAttr;
		}

		const FString LowerName = AttributeName.ToLower();

		TArray<FGameplayAttribute> AllAttrs;
		ASC->GetAllAttributes(AllAttrs);
		for (const FGameplayAttribute& Attr : AllAttrs)
		{
			if (!Attr.IsValid()) continue;

			const FString AttrName = Attr.GetName();
			if (AttrName.ToLower() == LowerName)
			{
				return Attr;
			}

			// Also try "AttributeSet.Attr" fully qualified match
			if (Attr.GetAttributeSetClass())
			{
				const FString Qualified = FString::Printf(TEXT("%s.%s"),
					*Attr.GetAttributeSetClass()->GetName(), *AttrName);
				if (Qualified.ToLower() == LowerName)
				{
					return Attr;
				}
			}
		}
		return NullAttr;
	}
}

// =================================================================
// Attributes
// =================================================================

float UGameplayAbilitySystemService::GetAttributeValue(const FString& ActorNameOrPath, const FString& AttributeName)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return -1.f;
	}

	FGameplayAttribute Attr = FindAttributeByName(ASC, AttributeName);
	if (!Attr.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Attribute '%s' not found on '%s'"),
			*AttributeName, *ActorNameOrPath);
		return -1.f;
	}

	return ASC->GetNumericAttribute(Attr);
}

float UGameplayAbilitySystemService::GetAttributeBase(const FString& ActorNameOrPath, const FString& AttributeName)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return -1.f;
	}

	FGameplayAttribute Attr = FindAttributeByName(ASC, AttributeName);
	if (!Attr.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Attribute '%s' not found on '%s'"),
			*AttributeName, *ActorNameOrPath);
		return -1.f;
	}

	return ASC->GetNumericAttributeBase(Attr);
}

bool UGameplayAbilitySystemService::SetAttributeBase(const FString& ActorNameOrPath, const FString& AttributeName, float NewValue)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	FGameplayAttribute Attr = FindAttributeByName(ASC, AttributeName);
	if (!Attr.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Attribute '%s' not found on '%s'"),
			*AttributeName, *ActorNameOrPath);
		return false;
	}

	ASC->SetNumericAttributeBase(Attr, NewValue);
	return true;
}

// =================================================================
// Abilities
// =================================================================

FString UGameplayAbilitySystemService::GrantAbility(const FString& ActorNameOrPath, const FString& AbilityClassPath, int32 Level, int32 InputID)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return FString();
	}

	UClass* AbilityClass = URevvyCommonHelpers::LoadClassWithFallback(AbilityClassPath, UGameplayAbility::StaticClass());
	if (!AbilityClass)
	{
		return FString();
	}

	FGameplayAbilitySpec Spec(AbilityClass, Level, InputID);
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
	if (!Handle.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("GiveAbility failed for '%s' on '%s'"),
			*AbilityClassPath, *ActorNameOrPath);
		return FString();
	}
	return Handle.ToString();
}

bool UGameplayAbilitySystemService::TryActivateAbilityByClass(const FString& ActorNameOrPath, const FString& AbilityClassPath)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	UClass* AbilityClass = URevvyCommonHelpers::LoadClassWithFallback(AbilityClassPath, UGameplayAbility::StaticClass());
	if (!AbilityClass)
	{
		return false;
	}

	return ASC->TryActivateAbilityByClass(AbilityClass);
}

bool UGameplayAbilitySystemService::CancelAbilityByClass(const FString& ActorNameOrPath, const FString& AbilityClassPath)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	UClass* AbilityClass = URevvyCommonHelpers::LoadClassWithFallback(AbilityClassPath, UGameplayAbility::StaticClass());
	if (!AbilityClass)
	{
		return false;
	}

	bool bCancelledAny = false;
	// Iterate a copy — CancelAbilityHandle mutates the internal spec list on activate cleanup
	TArray<FGameplayAbilitySpec> SpecsCopy = ASC->GetActivatableAbilities();
	for (const FGameplayAbilitySpec& Spec : SpecsCopy)
	{
		if (Spec.Ability && Spec.Ability->GetClass()->IsChildOf(AbilityClass) && Spec.IsActive())
		{
			ASC->CancelAbilityHandle(Spec.Handle);
			bCancelledAny = true;
		}
	}

	if (!bCancelledAny)
	{
		UE_LOG(LogRevvyGAS, Verbose, TEXT("No active ability of class '%s' on '%s'"),
			*AbilityClassPath, *ActorNameOrPath);
	}
	return bCancelledAny;
}

// =================================================================
// Gameplay Effects
// =================================================================

bool UGameplayAbilitySystemService::ApplyGameplayEffectToSelf(const FString& ActorNameOrPath, const FString& EffectClassPath, float Level)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	UClass* EffectClass = URevvyCommonHelpers::LoadClassWithFallback(EffectClassPath, UGameplayEffect::StaticClass());
	if (!EffectClass)
	{
		return false;
	}

	const UGameplayEffect* GE = EffectClass->GetDefaultObject<UGameplayEffect>();
	if (!GE)
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("No CDO for gameplay effect '%s'"), *EffectClassPath);
		return false;
	}

	FGameplayEffectContextHandle Ctx = ASC->MakeEffectContext();
	const FActiveGameplayEffectHandle Handle = ASC->ApplyGameplayEffectToSelf(GE, Level, Ctx);
	return Handle.IsValid();
}

FString UGameplayAbilitySystemService::GetActiveGameplayEffects(const FString& ActorNameOrPath)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return TEXT("[]");
	}

	UWorld* World = ASC->GetWorld();
	const float WorldTime = World ? World->GetTimeSeconds() : 0.f;

	TArray<TSharedPtr<FJsonValue>> Out;

	FGameplayEffectQuery Query;
	TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(Query);
	for (const FActiveGameplayEffectHandle& Handle : Handles)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		if (!Active)
		{
			continue;
		}

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("handle"), Handle.ToString());

		const FString EffectName = Active->Spec.Def
			? Active->Spec.Def->GetName()
			: FString(TEXT("<unknown>"));
		Obj->SetStringField(TEXT("effect"), EffectName);

		const float Duration = Active->GetDuration();
		Obj->SetNumberField(TEXT("duration"), Duration);
		Obj->SetNumberField(TEXT("time_remaining"), Active->GetTimeRemaining(WorldTime));
		Obj->SetNumberField(TEXT("level"), Active->Spec.GetLevel());
		Obj->SetNumberField(TEXT("stack_count"), Active->Spec.GetStackCount());

		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Result;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(Out, Writer);
	return Result;
}

// =================================================================
// Gameplay Tags
// =================================================================

bool UGameplayAbilitySystemService::HasGameplayTag(const FString& ActorNameOrPath, const FString& TagName)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Gameplay tag '%s' is not registered"), *TagName);
		return false;
	}
	return ASC->HasMatchingGameplayTag(Tag);
}

bool UGameplayAbilitySystemService::AddLooseGameplayTag(const FString& ActorNameOrPath, const FString& TagName)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Gameplay tag '%s' is not registered"), *TagName);
		return false;
	}
	ASC->AddLooseGameplayTag(Tag);
	return true;
}

bool UGameplayAbilitySystemService::RemoveLooseGameplayTag(const FString& ActorNameOrPath, const FString& TagName)
{
	UAbilitySystemComponent* ASC = AbilitySystemResolveActorASC(ActorNameOrPath);
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagName), /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		UE_LOG(LogRevvyGAS, Warning, TEXT("Gameplay tag '%s' is not registered"), *TagName);
		return false;
	}
	ASC->RemoveLooseGameplayTag(Tag);
	return true;
}
