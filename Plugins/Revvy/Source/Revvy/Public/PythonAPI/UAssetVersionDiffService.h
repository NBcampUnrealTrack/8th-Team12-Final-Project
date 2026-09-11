// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UAssetVersionDiffService.generated.h"

/**
 * Read-only asset diagnostics helper exposed to Python.
 *
 * Complements the discovery / tag / write services with a "version diff" axis:
 * inspect a single asset's on-disk footprint (bytes, last-modified time),
 * in-memory dirty state, and package-level graph metadata (dependency and
 * referencer counts) without mutating anything.
 *
 * Nothing here loads or modifies the asset unless strictly necessary — all
 * queries hit the filesystem, the AssetRegistry, or the already-loaded
 * UPackage cache. No FScopedTransaction is used.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.AssetVersionDiffService
 *
 *   size_bytes = svc.get_asset_size("/Game/Enemies/BP_Boss")
 *   modified   = svc.get_asset_last_modified_time("/Game/Enemies/BP_Boss")
 *   is_dirty   = svc.is_asset_dirty("/Game/Enemies/BP_Boss")
 *   diag       = json.loads(svc.get_asset_package_diagnostics("/Game/Enemies/BP_Boss"))
 *
 * @note All methods static, editor-time, read-only.
 */
UCLASS(BlueprintType)
class REVVY_API UAssetVersionDiffService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Size
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return the on-disk size (bytes) of the .uasset backing AssetPath.
	 *
	 * @param AssetPath Object path or package path (e.g. "/Game/Enemies/BP_Boss"
	 *                  or "/Game/Enemies/BP_Boss.BP_Boss"). Object suffix is
	 *                  stripped internally.
	 * @return Byte count from IFileManager::FileSize, or -1 if the file cannot
	 *         be resolved / does not exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetDiff|Size")
	static int64 GetAssetSize(const FString& AssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Time
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return the last-modified timestamp of the .uasset backing AssetPath,
	 * formatted as ISO-8601 (UTC).
	 *
	 * @return ISO-8601 string, or an empty string if the file cannot be
	 *         resolved / the timestamp is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetDiff|Time")
	static FString GetAssetLastModifiedTime(const FString& AssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Dirty
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Return true if the package for AssetPath is currently loaded and has
	 * unsaved edits (UPackage::IsDirty). If the package is not loaded, returns
	 * false — an unloaded package cannot be dirty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetDiff|Dirty")
	static bool IsAssetDirty(const FString& AssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Diagnostics
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Aggregate diagnostics for the package backing AssetPath.
	 *
	 * @return JSON object with:
	 *   {
	 *     "package_size_bytes": <int64>,   // -1 if unknown
	 *     "dep_count":          <int32>,   // AssetRegistry hard deps
	 *     "ref_count":          <int32>,   // AssetRegistry hard referencers
	 *     "last_modified":      "<iso8601>",
	 *     "is_dirty":           <bool>,
	 *     "package_flags":      <uint32>   // loaded UPackage->GetPackageFlags, 0 if unloaded
	 *   }
	 *   On missing asset: same shape with default/empty values.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|AssetDiff|Diagnostics")
	static FString GetAssetPackageDiagnostics(const FString& AssetPath);
};
