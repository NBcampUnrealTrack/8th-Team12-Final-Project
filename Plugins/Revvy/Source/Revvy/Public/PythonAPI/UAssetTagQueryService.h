// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UAssetTagQueryService.generated.h"

/**
 * Gameplay-tag-based asset discovery & mutation service exposed to Python.
 *
 * Complements UAssetDiscoveryService (name/type/path queries) with the
 * gameplay-tag axis: find assets by tag, inspect an asset's tags, and — for
 * assets that expose an FGameplayTagContainer UPROPERTY — mutate that tag
 * container at editor time.
 *
 * Read side is stable: assets implementing IGameplayTagAssetInterface always
 * work; other assets are best-effort via FAssetData tag values from the
 * AssetRegistry.
 *
 * Write side is best-effort: we discover an FGameplayTagContainer struct
 * property via UProperty reflection and mutate it in place. If the asset has
 * no such property, the mutation returns false and a warning is logged.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.AssetTagQueryService
 *
 *   # Find assets by tag(s)
 *   hits = json.loads(svc.find_assets_with_tag("Ability.Melee", "Blueprint"))
 *   any  = json.loads(svc.find_assets_with_any_tag(["Ability.Melee", "Ability.Ranged"], ""))
 *   all  = json.loads(svc.find_assets_with_all_tags(["Enemy", "Boss"], ""))
 *
 *   # Inspect
 *   tags = json.loads(svc.get_asset_tags("/Game/Enemies/BP_Boss"))
 *
 *   # Mutate (only if the asset owns a FGameplayTagContainer UPROPERTY)
 *   svc.add_tag_to_asset("/Game/Enemies/BP_Boss",     "Enemy.Boss")
 *   svc.remove_tag_from_asset("/Game/Enemies/BP_Boss","Enemy.Boss")
 *
 * @note All methods static, editor-time.
 */
UCLASS(BlueprintType)
class REVVY_API UAssetTagQueryService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Find (query)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Find all assets carrying the given gameplay tag.
	 *
	 * @param TagName        Fully-qualified tag name (e.g. "Ability.Melee.Slash").
	 * @param AssetClassPath Optional short class name (e.g. "Blueprint") or
	 *                       fully-qualified top-level asset path
	 *                       ("/Script/Engine.Blueprint"). Empty = all classes.
	 * @return JSON array of {asset_path, class}. Empty JSON array on error.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Find")
	static FString FindAssetsWithTag(const FString& TagName, const FString& AssetClassPath);

	/**
	 * Find all assets carrying at least one of the given tags (OR semantics).
	 *
	 * @param TagNames       Fully-qualified tag names.
	 * @param AssetClassPath See FindAssetsWithTag.
	 * @return JSON array of {asset_path, class}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Find")
	static FString FindAssetsWithAnyTag(const TArray<FString>& TagNames, const FString& AssetClassPath);

	/**
	 * Find all assets carrying every one of the given tags (AND semantics).
	 *
	 * @param TagNames       Fully-qualified tag names.
	 * @param AssetClassPath See FindAssetsWithTag.
	 * @return JSON array of {asset_path, class}.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Find")
	static FString FindAssetsWithAllTags(const TArray<FString>& TagNames, const FString& AssetClassPath);

	// ═══════════════════════════════════════════════════════════════════
	// Inspect (query)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List every gameplay tag carried by the asset at AssetPath.
	 *
	 * Prefers IGameplayTagAssetInterface::GetOwnedGameplayTags on the loaded
	 * object; falls back to FAssetData tag values from the AssetRegistry.
	 *
	 * @return JSON array of tag name strings. "[]" if none / not found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Query")
	static FString GetAssetTags(const FString& AssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Mutate (best-effort)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Add TagName to the first FGameplayTagContainer UPROPERTY found on the
	 * asset. Wraps the mutation in FScopedTransaction and marks the package
	 * dirty. No-op if the asset owns no such property (returns false, warns).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Mutate")
	static bool AddTagToAsset(const FString& AssetPath, const FString& TagName);

	/**
	 * Remove TagName from the first FGameplayTagContainer UPROPERTY found on
	 * the asset. Wraps the mutation in FScopedTransaction and marks the
	 * package dirty. No-op if the asset owns no such property.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetTags|Mutate")
	static bool RemoveTagFromAsset(const FString& AssetPath, const FString& TagName);
};
