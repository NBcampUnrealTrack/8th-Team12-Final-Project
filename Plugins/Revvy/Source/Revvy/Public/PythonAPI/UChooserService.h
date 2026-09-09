// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UChooserService.generated.h"

/**
 * Chooser Table authoring service exposed directly to Python for creating and
 * editing UChooserTable assets (UE 5.4+ Animation Chooser system), managing
 * their result rows, inspecting their structure and evaluating them against a
 * context object.
 *
 * Mirrors the UActorService / ULightingService / USequencerService pattern:
 * all UFUNCTIONs are static and BlueprintCallable so they are reachable from
 * Python as `unreal.ChooserService.<snake_case_name>(...)`.
 *
 * UE 5.8 API notes:
 *  - `UChooserTable` derives from `UChooserSignature` (5.4+); its output type
 *    is expressed via `EObjectChooserResultType ResultType` +
 *    `TObjectPtr<UClass> OutputObjectType`.
 *  - Result rows are `TArray<FInstancedStruct> ResultsStructs` (editor-only)
 *    populated with `FObjectChooserBase`-derived structs, most commonly
 *    `FAssetChooser` (hard asset ref) or `FClassChooser` (UClass ref).
 *  - Evaluation uses `FChooserEvaluationContext` + `UChooserTable::EvaluateChooser`
 *    (iterator-callback based) or `UChooserFunctionLibrary::EvaluateChooser`
 *    (BlueprintPure convenience).
 *  - The Chooser plugin ships with the engine but is NOT enabled by default.
 *    Callers must enable "Chooser" in the .uproject for these APIs to link.
 *
 * Python Usage:
 *   import unreal
 *   svc = unreal.ChooserService
 *
 *   # Create a chooser that returns UAnimMontage subclasses/instances
 *   path = svc.create_chooser_table(
 *       "/Game/Choosers/CT_AttackMontages",
 *       "/Script/Engine.AnimMontage")
 *
 *   # Rows
 *   row0 = svc.add_chooser_row(path)              # -> 0
 *   row1 = svc.add_chooser_row(path)              # -> 1
 *   dup  = svc.duplicate_chooser_row(path, 0)     # -> 2
 *   svc.remove_chooser_row(path, 1)               # True
 *
 *   # Query
 *   rows = svc.list_chooser_rows(path)            # JSON array
 *   info = svc.get_chooser_table_info(path)       # JSON object
 *
 *   # Evaluate
 *   result_path = svc.evaluate_chooser(path, some_actor)
 *
 * @note All 7 chooser operations available via Python.
 */
UCLASS(BlueprintType)
class REVVY_API UChooserService : public UObject
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════════════════
	// Create
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Create a new UChooserTable asset at the given content path.
	 *
	 * The chooser is configured for `ObjectResult` output with its
	 * `OutputObjectType` set to the class resolved from `ResultTypeClassPath`.
	 * The asset is created empty (0 rows, 0 columns); use AddChooserRow /
	 * column authoring APIs to populate it.
	 *
	 * @param OutputAssetPath      - Content path for the new asset,
	 *                                e.g. "/Game/Choosers/CT_MyChooser".
	 * @param ResultTypeClassPath  - Class path of the result type,
	 *                                e.g. "/Script/Engine.AnimMontage".
	 *                                May be empty; result class is left null.
	 * @return Package path of the created asset, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Create")
	static FString CreateChooserTable(
		const FString& OutputAssetPath,
		const FString& ResultTypeClassPath);

	// ═══════════════════════════════════════════════════════════════════
	// Rows
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Append a new (default-initialized) result row to the chooser table.
	 *
	 * The new row is authored as a default FAssetChooser (empty asset ref);
	 * callers can later swap the row's contents via a struct-editor API.
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @return Zero-based index of the new row, or INDEX_NONE (-1) on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Rows")
	static int32 AddChooserRow(const FString& ChooserAssetPath);

	/**
	 * Remove the result row at the given index.
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @param RowIndex         - Zero-based index of the row to remove.
	 * @return True on success, false if the index is out of range or the
	 *          asset cannot be loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Rows")
	static bool RemoveChooserRow(const FString& ChooserAssetPath, int32 RowIndex);

	/**
	 * Duplicate the row at SourceRowIndex and append the copy to the end
	 * of ResultsStructs.
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @param SourceRowIndex   - Zero-based index of the source row.
	 * @return Zero-based index of the new (duplicated) row, or INDEX_NONE
	 *          (-1) on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Rows")
	static int32 DuplicateChooserRow(const FString& ChooserAssetPath, int32 SourceRowIndex);

	// ═══════════════════════════════════════════════════════════════════
	// Query
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * List all result rows of the chooser table as a JSON array.
	 *
	 * Each entry has the shape:
	 *   {
	 *     "index":       <int>,
	 *     "row_type":    "<UScriptStruct name, e.g. AssetChooser>",
	 *     "result_path": "<hard/class ref path, or empty>",
	 *     "is_valid":    <bool>
	 *   }
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @return JSON array string, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Query")
	static FString ListChooserRows(const FString& ChooserAssetPath);

	/**
	 * Return a JSON object summarizing the chooser table structure:
	 *
	 *   {
	 *     "result_type":       "<ObjectResult|ClassResult|NoPrimaryResult>",
	 *     "output_class_path": "<UClass path or empty>",
	 *     "row_count":         <int>,
	 *     "column_count":      <int>,
	 *     "columns":           [ { "index": <int>, "type": "<struct name>" }, ... ]
	 *   }
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @return JSON object string, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Query")
	static FString GetChooserTableInfo(const FString& ChooserAssetPath);

	// ═══════════════════════════════════════════════════════════════════
	// Evaluate
	// ═══════════════════════════════════════════════════════════════════

	/**
	 * Evaluate the chooser table against the given context object and
	 * return the resulting asset/class path.
	 *
	 * Internally builds an `FChooserEvaluationContext` and invokes
	 * `UChooserTable::EvaluateChooser` with an iterator callback that
	 * captures the first successful result.
	 *
	 * @param ChooserAssetPath - Content path of the UChooserTable asset.
	 * @param ContextObject    - Optional context object exposed to the
	 *                            chooser's column filters. May be null.
	 * @return Package path of the resulting UObject, or empty string if
	 *          evaluation returned no result / failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|Chooser|Eval")
	static FString EvaluateChooser(
		const FString& ChooserAssetPath,
		UObject* ContextObject);
};
