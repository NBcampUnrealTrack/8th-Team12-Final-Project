// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UGameplayCueService.generated.h"

/**
 * Gameplay Cue dispatch operations exposed directly to Python.
 *
 * Sibling to UGameplayAbilitySystemService: that service handles Attributes /
 * Abilities / Effects / Tags, while this service handles Gameplay Cues — the
 * cosmetic/gameplay-event layer that dispatches to UGameplayCueNotify_* actors,
 * VFX/SFX static notifies, and BP GameplayCueNotify UObjects.
 *
 * All UFUNCTIONs are static + BlueprintCallable so they are reachable from
 * Python as `unreal.GameplayCueService.<snake_case_name>(...)`.
 *
 * Runtime-only ops. No FScopedTransaction — these mutate live ASC state, not
 * editor assets.
 *
 * Actor lookup uses the standard Revvy pattern:
 *   1. Exact GetActorLabel / GetName match (case-insensitive)
 *   2. FSoftObjectPath resolve
 *   3. Contains match
 *
 * ASC resolution:
 *   - UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true)
 *     (also honours IAbilitySystemInterface fallbacks internally).
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.GameplayCueService
 *
 *   # Fire-and-forget one-shot cue (VFX burst, SFX hit)
 *   svc.execute_gameplay_cue("Hero_BP", "GameplayCue.Hit.Slash")
 *
 *   # Persistent cue (looping VFX / status FX) — Add / Remove pair
 *   svc.add_gameplay_cue    ("Hero_BP", "GameplayCue.Status.Burning")
 *   svc.remove_gameplay_cue ("Hero_BP", "GameplayCue.Status.Burning")
 *
 *   # Introspect currently-active persistent cues
 *   active = json.loads(svc.list_active_gameplay_cues("Hero_BP"))
 */
UCLASS(BlueprintType)
class REVVY_API UGameplayCueService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Execute (fire-and-forget)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Fire a one-shot gameplay cue on the target actor's ASC.
	 *
	 * Routes through UAbilitySystemComponent::ExecuteGameplayCue, which
	 * dispatches to the GameplayCueManager for one-shot notify types
	 * (impacts, hits, bursts, transient SFX).
	 *
	 * @param TargetActorNameOrPath  Actor label / name / soft path.
	 * @param CueTagName             GameplayCue tag (e.g. "GameplayCue.Hit.Slash").
	 * @return True on success. False if actor / ASC / tag not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameplayCue|Execute")
	static bool ExecuteGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName);

	// ═══════════════════════════════════════════════════════════════════
	// Persistent (add / remove pair)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Add a persistent gameplay cue to the target actor's ASC.
	 *
	 * Persistent cues are typically looping VFX / status effects — they
	 * remain active until RemoveGameplayCue is called with the same tag
	 * (or the ASC is destroyed).
	 *
	 * @return True on success. False if actor / ASC / tag not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameplayCue|Persistent")
	static bool AddGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName);

	/**
	 * Remove a previously-added persistent gameplay cue.
	 *
	 * @return True on success. False if actor / ASC / tag not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameplayCue|Persistent")
	static bool RemoveGameplayCue(const FString& TargetActorNameOrPath, const FString& CueTagName);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all currently-active persistent gameplay cues on the target
	 * actor's ASC (i.e. cues added via AddGameplayCue and not yet removed).
	 *
	 * @return JSON array of {cue_tag, source}. Empty JSON array "[]" on error.
	 *         "source" is the display name of the EffectCauser / Instigator /
	 *         SourceObject stored in the cue's parameters, or "" if none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|GameplayCue|Query")
	static FString ListActiveGameplayCues(const FString& TargetActorNameOrPath);
};
