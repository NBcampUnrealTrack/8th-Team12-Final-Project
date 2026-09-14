// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UStringTableService.generated.h"

/**
 * Information about a single String Table entry (key -> source string).
 */
USTRUCT(BlueprintType)
struct FStringTableEntryInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "StringTable")
	FString Key;

	UPROPERTY(BlueprintReadWrite, Category = "StringTable")
	FString SourceString;
};

/**
 * String Table service exposed directly to Python.
 *
 * Mirrors the Epic-native StringTableTools toolset. Provides these actions:
 * - list_string_tables: List all String Table assets under a path
 * - string_table_exists: Check if a String Table exists at a path
 * - create_string_table: Create a new String Table asset (optionally with a namespace)
 * - get_entries: Get all key/source-string entries from a String Table
 * - get_entry_count: Get the number of entries in a String Table
 * - set_entry: Add or update a key/source-string entry
 * - get_entry: Read the source string for a key
 * - remove_entry: Remove a key/source-string entry
 * - set_meta_data: Set a metadata value for a key
 * - save_string_table: Save the String Table asset to disk
 *
 * Python Usage:
 *   import unreal
 *
 *   # List all string tables
 *   tables = unreal.StringTableService.list_string_tables("/Game")
 *
 *   # Create a new string table
 *   path = unreal.StringTableService.create_string_table("/Game/Localization", "ST_UI", "UI")
 *
 *   # Check existence
 *   if unreal.StringTableService.string_table_exists("/Game/Localization/ST_UI"):
 *       # Add / update entries
 *       unreal.StringTableService.set_entry("/Game/Localization/ST_UI", "Hello", "Hello, world!")
 *       unreal.StringTableService.set_entry("/Game/Localization/ST_UI", "Bye", "Goodbye!")
 *
 *   # Read an entry
 *   source = unreal.StringTableService.get_entry("/Game/Localization/ST_UI", "Hello")
 *
 *   # Enumerate entries
 *   for entry in unreal.StringTableService.get_entries("/Game/Localization/ST_UI"):
 *       print(entry.key, entry.source_string)
 *
 *   count = unreal.StringTableService.get_entry_count("/Game/Localization/ST_UI")
 *
 *   # Metadata + persist
 *   unreal.StringTableService.set_meta_data("/Game/Localization/ST_UI", "Hello", "Comment", "Greeting text")
 *   unreal.StringTableService.save_string_table("/Game/Localization/ST_UI")
 *
 *   # Remove an entry
 *   unreal.StringTableService.remove_entry("/Game/Localization/ST_UI", "Bye")
 *
 * @note This mirrors the Epic-native StringTableTools MCP toolset.
 */
UCLASS(BlueprintType)
class REVVY_API UStringTableService : public UObject
{
	GENERATED_BODY()

public:
	// ============================================
	// Discovery Actions
	// ============================================

	/**
	 * List all String Table assets under the given path.
	 *
	 * @param PathFilter - Path to search in (default: /Game)
	 * @return Array of String Table object paths
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static TArray<FString> ListStringTables(const FString& PathFilter = TEXT("/Game"));

	/**
	 * Check if a String Table exists at the given path.
	 *
	 * @param Path - Full path to the String Table
	 * @return True if the String Table exists
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static bool StringTableExists(const FString& Path);

	// ============================================
	// Lifecycle Actions
	// ============================================

	/**
	 * Create a new String Table asset.
	 *
	 * @param PackagePath - Directory path for the new asset (e.g. "/Game/Localization")
	 * @param AssetName - Name for the new String Table
	 * @param Namespace - Optional namespace for the String Table
	 * @return Full object path if successful, empty string on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static FString CreateStringTable(const FString& PackagePath, const FString& AssetName, const FString& Namespace = TEXT(""));

	// ============================================
	// Entry Read Actions
	// ============================================

	/**
	 * Get all key/source-string entries from a String Table.
	 *
	 * @param Path - Full path to the String Table
	 * @return Array of entry info structs
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static TArray<FStringTableEntryInfo> GetEntries(const FString& Path);

	/**
	 * Get the number of entries in a String Table.
	 *
	 * @param Path - Full path to the String Table
	 * @return Number of entries, or 0 on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static int32 GetEntryCount(const FString& Path);

	/**
	 * Get the source string for a key.
	 *
	 * @param Path - Full path to the String Table
	 * @param Key - Entry key
	 * @return Source string, or empty string if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static FString GetEntry(const FString& Path, const FString& Key);

	// ============================================
	// Entry Mutation Actions
	// ============================================

	/**
	 * Add or update a key/source-string entry.
	 *
	 * @param Path - Full path to the String Table
	 * @param Key - Entry key
	 * @param SourceString - Source string value
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static bool SetEntry(const FString& Path, const FString& Key, const FString& SourceString);

	/**
	 * Remove a key/source-string entry.
	 *
	 * @param Path - Full path to the String Table
	 * @param Key - Entry key
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static bool RemoveEntry(const FString& Path, const FString& Key);

	/**
	 * Set a metadata value for a key.
	 *
	 * @param Path - Full path to the String Table
	 * @param Key - Entry key
	 * @param MetaDataId - Metadata identifier (e.g. "Comment")
	 * @param Value - Metadata value
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static bool SetMetaData(const FString& Path, const FString& Key, const FString& MetaDataId, const FString& Value);

	// ============================================
	// Persistence
	// ============================================

	/**
	 * Save the String Table asset to disk.
	 *
	 * @param Path - Full path to the String Table
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Revvy|StringTable")
	static bool SaveStringTable(const FString& Path);

private:
	/** Helper to load a String Table asset by path. */
	static class UStringTable* LoadStringTable(const FString& Path);
};
