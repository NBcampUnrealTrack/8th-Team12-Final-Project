// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UGameplayAbilitySystemService.generated.h"

/**
 * Gameplay Ability System runtime operations exposed directly to Python.
 *
 * Mirrors the UPerformanceService / ULightingService pattern: all UFUNCTIONs
 * are static + BlueprintCallable so they are reachable from Python as
 * `unreal.GameplayAbilitySystemService.<snake_case_name>(...)`.
 *
 * Runtime-only ops (attributes, abilities, gameplay effects, gameplay tags)
 * against a target actor's UAbilitySystemComponent. No FScopedTransaction is
 * used — these operations mutate live runtime state, not editor assets.
 *
 * Actor lookup uses the standard Revvy pattern:
 *   1. Exact GetActorLabel / GetName match (case-insensitive)
 *   2. Contains match
 *   3. FSoftObjectPath resolve
 *
 * ASC resolution:
 *   - IAbilitySystemInterface::GetAbilitySystemComponent() (preferred)
 *   - UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true)
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.GameplayAbilitySystemService
 *
 *   # Attributes
 *   hp     = svc.get_attribute_value("Hero_BP", "Health")
 *   base   = svc.get_attribute_base ("Hero_BP", "Health")
 *   ok     = svc.set_attribute_base ("Hero_BP", "Health", 100.0)
 *
 *   # Abilities
 *   handle = svc.grant_ability("Hero_BP",
 *       "/Game/Abilities/GA_Dodge.GA_Dodge_C", 1, 0)
 *   svc.try_activate_ability_by_class("Hero_BP",
 *       "/Game/Abilities/GA_Dodge.GA_Dodge_C")
 *   svc.cancel_ability_by_class("Hero_BP",
 *       "/Game/Abilities/GA_Dodge.GA_Dodge_C")
 *
 *   # Gameplay Effects
 *   svc.apply_gameplay_effect_to_self("Hero_BP",
 *       "/Game/Effects/GE_Poison.GE_Poison_C", 1.0)
 *   active = json.loads(svc.get_active_gameplay_effects("Hero_BP"))
 *
 *   # Gameplay Tags
 *   has_stun = svc.has_gameplay_tag("Hero_BP", "State.Stunned")
 *   svc.add_loose_gameplay_tag   ("Hero_BP", "State.Burning")
 *   svc.remove_loose_gameplay_tag("Hero_BP", "State.Burning")
 */
UCLASS(BlueprintType)
class REVVY_API UGameplayAbilitySystemService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Attributes
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Read the current (post-modifier) numeric value of a gameplay attribute.
	 *
	 * @param ActorNameOrPath  Actor label / name / soft path.
	 * @param AttributeName    FGameplayAttribute::GetName() (e.g. "Health").
	 * @return Current numeric value, or -1.f on error (no ASC / missing attr).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Attributes")
	static float GetAttributeValue(const FString& ActorNameOrPath, const FString& AttributeName);

	/**
	 * Read the base (pre-modifier) numeric value of a gameplay attribute.
	 *
	 * @return Base numeric value, or -1.f on error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Attributes")
	static float GetAttributeBase(const FString& ActorNameOrPath, const FString& AttributeName);

	/**
	 * Set the base numeric value of a gameplay attribute.
	 *
	 * @return True on success, false if actor / ASC / attribute not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Attributes")
	static bool SetAttributeBase(const FString& ActorNameOrPath, const FString& AttributeName, float NewValue);

	// ═══════════════════════════════════════════════════════════════════
	// Abilities
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Grant a UGameplayAbility class to the target actor's ASC.
	 *
	 * @param AbilityClassPath  Object path to the ability class
	 *                          (e.g. "/Game/Abilities/GA_Dodge.GA_Dodge_C").
	 * @param Level             Ability level for the spec.
	 * @param InputID           Input ID for the spec (-1 if unbound).
	 * @return FGameplayAbilitySpecHandle::ToString() on success, "" on error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Abilities")
	static FString GrantAbility(const FString& ActorNameOrPath, const FString& AbilityClassPath, int32 Level, int32 InputID);

	/**
	 * Attempt to activate any granted ability of the given class.
	 *
	 * @return True if an ability was activated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Abilities")
	static bool TryActivateAbilityByClass(const FString& ActorNameOrPath, const FString& AbilityClassPath);

	/**
	 * Cancel all currently-active abilities matching the given class.
	 *
	 * @return True if at least one active ability was cancelled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Abilities")
	static bool CancelAbilityByClass(const FString& ActorNameOrPath, const FString& AbilityClassPath);

	// ═══════════════════════════════════════════════════════════════════
	// Gameplay Effects
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Apply a UGameplayEffect class to the target actor's own ASC.
	 *
	 * @param EffectClassPath  Object path to the gameplay-effect class.
	 * @param Level            Effect level (used by curve-based magnitudes).
	 * @return True if the effect was applied (handle valid).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Effects")
	static bool ApplyGameplayEffectToSelf(const FString& ActorNameOrPath, const FString& EffectClassPath, float Level);

	/**
	 * List all currently-active gameplay effects on the target actor's ASC.
	 *
	 * @return JSON array of {handle, effect, duration, time_remaining, level,
	 *                         stack_count}. Empty JSON array "[]" on error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Effects")
	static FString GetActiveGameplayEffects(const FString& ActorNameOrPath);

	// ═══════════════════════════════════════════════════════════════════
	// Gameplay Tags
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Query whether the target actor's ASC currently has a gameplay tag
	 * (either owned or added as a loose tag).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Tags")
	static bool HasGameplayTag(const FString& ActorNameOrPath, const FString& TagName);

	/**
	 * Add a loose gameplay tag to the target actor's ASC. Loose tags do not
	 * go through a UGameplayEffect and are not replicated by default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Tags")
	static bool AddLooseGameplayTag(const FString& ActorNameOrPath, const FString& TagName);

	/**
	 * Remove a loose gameplay tag previously added via AddLooseGameplayTag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GAS|Tags")
	static bool RemoveLooseGameplayTag(const FString& ActorNameOrPath, const FString& TagName);
};
