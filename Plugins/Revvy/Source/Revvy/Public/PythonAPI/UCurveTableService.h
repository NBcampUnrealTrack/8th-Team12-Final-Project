// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UCurveTableService.generated.h"

/**
 * A single keyframe on a curve table row's curve.
 */
USTRUCT(BlueprintType)
struct FCurveKeyInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	float Time = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	float Value = 0.0f;
};

/**
 * Information about a CurveTable asset.
 */
USTRUCT(BlueprintType)
struct FCurveTableInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	FString Name;

	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	FString Path;

	/** Curve storage mode: "Empty", "SimpleCurves", or "RichCurves" */
	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	FString Mode;

	UPROPERTY(BlueprintReadWrite, Category = "CurveTable")
	TArray<FString> RowNames;
};

/**
 * CurveTable service exposed directly to Python.
 *
 * Mirrors the Epic-native CurveTableTools. Provides curve-table authoring actions:
 * - list_curve_tables: List all CurveTable assets under a path filter
 * - curve_table_exists: Check whether a CurveTable exists at a path
 * - create_curve_table: Create a new CurveTable asset (Float => RichCurves)
 * - get_curve_table_info: Get name, path, mode and row names for a CurveTable
 * - list_rows: List the row (curve) names in a CurveTable
 * - add_row: Add a new empty curve row
 * - remove_row: Remove a curve row
 * - set_key: Add or update a keyframe (time, value) on a row's curve
 * - get_keys: Read all keyframes from a row's curve
 * - evaluate_curve: Evaluate a row's curve at a given time
 * - delete_row_keys: Clear all keyframes from a row's curve
 * - save_curve_table: Save the CurveTable asset to disk
 *
 * Python Usage:
 *   import unreal
 *
 *   # Create a new (Float / RichCurve) curve table
 *   path = unreal.CurveTableService.create_curve_table("/Game/Curves", "CT_Damage", "Float")
 *
 *   # Check existence
 *   if unreal.CurveTableService.curve_table_exists("/Game/Curves/CT_Damage"):
 *       # Add a row and author some keys
 *       unreal.CurveTableService.add_row("/Game/Curves/CT_Damage", "Level")
 *       unreal.CurveTableService.set_key("/Game/Curves/CT_Damage", "Level", 0.0, 10.0)
 *       unreal.CurveTableService.set_key("/Game/Curves/CT_Damage", "Level", 1.0, 25.0)
 *
 *       # Read keys back and evaluate
 *       keys = unreal.CurveTableService.get_keys("/Game/Curves/CT_Damage", "Level")
 *       value = unreal.CurveTableService.evaluate_curve("/Game/Curves/CT_Damage", "Level", 0.5)
 *
 *       # Persist changes
 *       unreal.CurveTableService.save_curve_table("/Game/Curves/CT_Damage")
 *
 *   # List tables and inspect
 *   tables = unreal.CurveTableService.list_curve_tables("/Game")
 *   info = unreal.CurveTableInfo()
 *   if unreal.CurveTableService.get_curve_table_info("/Game/Curves/CT_Damage", info):
 *       print(info.mode, info.row_names)
 *
 * @note This mirrors the Epic-native CurveTableTools MCP toolset.
 */
UCLASS(BlueprintType)
class REVVY_API UCurveTableService : public UObject
{
	GENERATED_BODY()

public:
	// ============================================
	// Discovery Actions
	// ============================================

	/**
	 * List all CurveTable assets under a path.
	 *
	 * @param PathFilter - Path to search in (default: /Game)
	 * @return Array of CurveTable object paths
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static TArray<FString> ListCurveTables(const FString& PathFilter = TEXT("/Game"));

	/**
	 * Check if a CurveTable exists at the given path.
	 *
	 * @param Path - Full path to the CurveTable
	 * @return True if a CurveTable exists at Path
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool CurveTableExists(const FString& Path);

	// ============================================
	// Lifecycle Actions
	// ============================================

	/**
	 * Create a new CurveTable asset.
	 *
	 * @param PackagePath - Directory path for the new asset (e.g. /Game/Curves)
	 * @param AssetName - Name for the new CurveTable
	 * @param CurveMode - "Float" => RichCurves (default). "Simple" => SimpleCurves.
	 * @return Full object path of the created CurveTable, or empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static FString CreateCurveTable(const FString& PackagePath, const FString& AssetName, const FString& CurveMode = TEXT("Float"));

	// ============================================
	// Info Actions
	// ============================================

	/**
	 * Get information about a CurveTable.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param OutInfo - Output struct with name, path, mode and row names
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool GetCurveTableInfo(const FString& Path, FCurveTableInfo& OutInfo);

	/**
	 * List all row (curve) names in a CurveTable.
	 *
	 * @param Path - Full path to the CurveTable
	 * @return Array of row names
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static TArray<FString> ListRows(const FString& Path);

	// ============================================
	// Row Operations
	// ============================================

	/**
	 * Add a new empty curve row to a CurveTable.
	 * The curve type (rich/simple) matches the table's current mode.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name for the new row
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool AddRow(const FString& Path, const FString& RowName);

	/**
	 * Remove a curve row from a CurveTable.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name of the row to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool RemoveRow(const FString& Path, const FString& RowName);

	// ============================================
	// Key Operations
	// ============================================

	/**
	 * Add or update a keyframe on a row's curve.
	 * Works for both rich and simple curves via FRealCurve::UpdateOrAddKey.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name of the row whose curve to modify
	 * @param Time - Keyframe time (input X value)
	 * @param Value - Keyframe value (output Y value)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool SetKey(const FString& Path, const FString& RowName, float Time, float Value);

	/**
	 * Read all keyframes from a row's curve.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name of the row whose curve to read
	 * @return Array of keyframes (time/value pairs), ordered by time
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static TArray<FCurveKeyInfo> GetKeys(const FString& Path, const FString& RowName);

	/**
	 * Evaluate a row's curve at a given time.
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name of the row whose curve to evaluate
	 * @param Time - Input time (X value)
	 * @return The evaluated curve value, or 0.0 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static float EvaluateCurve(const FString& Path, const FString& RowName, float Time);

	/**
	 * Clear all keyframes from a row's curve (keeps the row).
	 *
	 * @param Path - Full path to the CurveTable
	 * @param RowName - Name of the row whose curve to clear
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool DeleteRowKeys(const FString& Path, const FString& RowName);

	// ============================================
	// Persistence
	// ============================================

	/**
	 * Save a CurveTable asset to disk.
	 *
	 * @param Path - Full path to the CurveTable
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|CurveTable")
	static bool SaveCurveTable(const FString& Path);

private:
	/** Helper to load a CurveTable by path. Returns nullptr (and logs) on failure. */
	static class UCurveTable* LoadCurveTable(const FString& Path);

	/** Helper to find a row's curve as the polymorphic FRealCurve base. */
	static struct FRealCurve* FindRealCurve(class UCurveTable* CurveTable, const FString& RowName, bool bWarnIfNotFound = true);

	/** Helper to mark a curve table dirty and broadcast its change delegate. */
	static void NotifyCurveTableChanged(class UCurveTable* CurveTable);
};
