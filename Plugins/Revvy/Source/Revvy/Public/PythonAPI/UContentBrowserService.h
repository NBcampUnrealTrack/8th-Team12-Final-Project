// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UContentBrowserService.generated.h"

/**
 * Content Browser 2 virtual-path service exposed directly to Python.
 *
 * Complements UAssetDiscoveryService (which operates on classical
 * AssetRegistry paths under /Game, /Engine, etc.) by exposing the UE 5.8
 * Content Browser 2 *virtual* path layer. Virtual paths include the user's
 * favorite folders, filter roots, plugin virtualization overlays, and the
 * source-control / recently-modified virtual folders that live outside the
 * AssetRegistry.
 *
 * Mirrors the UPerformanceService pattern: all UFUNCTIONs are static +
 * BlueprintCallable so they are reachable from Python as
 * `unreal.ContentBrowserService.<snake_case_name>(...)`.
 *
 * Read-only editor query surface — no FScopedTransaction, no asset
 * mutation. All operations are editor-only; non-editor builds return
 * empty payloads.
 *
 * Python Usage:
 *   import unreal, json
 *   svc = unreal.ContentBrowserService
 *
 *   # Enumerate everything under a virtual path
 *   items = json.loads(svc.enumerate_at_virtual_path("/All/Game/Blueprints", "All"))
 *   folders = json.loads(svc.enumerate_at_virtual_path("/All/Game", "Folder"))
 *   files   = json.loads(svc.enumerate_at_virtual_path("/All/Game", "File"))
 *
 *   # Current selection (may include virtual items, not just AssetData)
 *   sel = json.loads(svc.get_current_content_browser_selection())
 *
 *   # Focus + sync the Content Browser to a virtual folder
 *   svc.focus_content_browser_to_virtual_path("/All/Game/Blueprints")
 *
 *   # Top-level virtual roots (data-source mount points)
 *   roots = json.loads(svc.get_virtual_path_tree_roots())
 */
UCLASS(BlueprintType)
class REVVY_API UContentBrowserService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Virtual-path enumeration (UContentBrowserDataSubsystem)
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Enumerate content-browser items at the given virtual path via the
	 * UContentBrowserDataSubsystem.
	 *
	 * @param VirtualPath     Content-browser virtual path
	 *                        (e.g. "/All", "/All/Game", "/All/Game/Blueprints").
	 * @param ItemTypeFilter  One of "All" | "Folder" | "File" (case-insensitive).
	 *                        Anything else is treated as "All".
	 * @return JSON array of objects:
	 *         [{"virtual_path", "physical_path", "is_folder",
	 *           "display_name", "mount_root"}].
	 *         Empty array (with {"error"} wrapper) if the subsystem is not
	 *         available (non-editor build, GEditor null).
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ContentBrowser|Enumerate")
	static FString EnumerateAtVirtualPath(const FString& VirtualPath, const FString& ItemTypeFilter);

	/**
	 * Snapshot the current Content Browser selection using the CB2 item
	 * abstraction (may include virtual items that have no FAssetData).
	 *
	 * @return JSON array of objects:
	 *         [{"virtual_path", "physical_path", "is_folder",
	 *           "display_name", "mount_root"}].
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ContentBrowser|Query")
	static FString GetCurrentContentBrowserSelection();

	/**
	 * Focus the primary Content Browser panel and sync its navigation to
	 * the given virtual folder path.
	 *
	 * @param VirtualPath  Content-browser virtual folder path.
	 * @return True if the sync request was dispatched; false if the CB
	 *         module is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ContentBrowser|Focus")
	static bool FocusContentBrowserToVirtualPath(const FString& VirtualPath);

	/**
	 * Enumerate the top-level roots of the CB2 virtual-path tree — one
	 * entry per active data source mount point (Game, Engine, Plugins,
	 * Favorites, Filters, etc.).
	 *
	 * @return JSON array of objects: [{"root", "source_name"}].
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|ContentBrowser|Query")
	static FString GetVirtualPathTreeRoots();
};
