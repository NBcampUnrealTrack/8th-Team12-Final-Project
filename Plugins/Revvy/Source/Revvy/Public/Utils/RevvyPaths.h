// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Utility class for resolving Revvy plugin paths.
 * Works whether plugin is installed in Project/Plugins, Engine/Plugins/Marketplace, or elsewhere.
 */
class REVVY_API FRevvyPaths
{
public:
	/**
	 * Get the base directory of the Revvy plugin.
	 * Uses IPluginManager to find the actual plugin location regardless of install type.
	 * 
	 * @return Absolute path to the Revvy plugin directory, or empty string if not found
	 */
	static FString GetPluginDir();

	/**
	 * Get the Content directory of the Revvy plugin.
	 * 
	 * @return Absolute path to Revvy/Content, or empty string if not found
	 */
	static FString GetPluginContentDir();

	/**
	 * Get the Help content directory.
	 * 
	 * @return Absolute path to Revvy/Content/Help, or empty string if not found
	 */
	static FString GetHelpDir();

	/**
	 * Get the instructions content directory.
	 * 
	 * @return Absolute path to Revvy/Content/instructions, or empty string if not found
	 */
	static FString GetInstructionsDir();

	/**
	 * Get the Config directory of the Revvy plugin.
	 * 
	 * @return Absolute path to Revvy/Config, or empty string if not found
	 */
	static FString GetConfigDir();

	/**
	 * Get the Revvy Screenshots directory under Project/Saved.
	 * Creates the directory if it doesn't exist.
	 * 
	 * @return Absolute path to Project/Saved/Revvy/Screenshots
	 */
	static FString GetScreenshotsDir();

	/**
	 * Get the Revvy chat memory directory under Project/Saved.
	 * This is the persistent, per-project store for the in-editor chat memory tool.
	 * Creates the directory if it doesn't exist.
	 *
	 * @return Absolute path to Project/Saved/Revvy/Memory
	 */
	static FString GetMemoryDir();

	/**
	 * Get the Revvy plugin version name (e.g. "3.0").
	 * Reads from the plugin descriptor via IPluginManager.
	 *
	 * @return Version string, or "unknown" if not found
	 */
	static FString GetPluginVersionName();

	/**
	 * Clear all files in the Revvy Screenshots directory.
	 * Called on plugin startup to save disk space.
	 */
	static void ClearScreenshotsDir();

private:
	/** Cached plugin directory path */
	static FString CachedPluginDir;
	
	/** Whether cache has been initialized */
	static bool bCacheInitialized;
};
