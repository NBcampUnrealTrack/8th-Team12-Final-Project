// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Utils/RevvyPaths.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "PluginDescriptor.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyPaths, Log, All);

// Static member initialization
FString FRevvyPaths::CachedPluginDir;
bool FRevvyPaths::bCacheInitialized = false;

FString FRevvyPaths::GetPluginDir()
{
	// Return cached value if available
	if (bCacheInitialized && !CachedPluginDir.IsEmpty())
	{
		return CachedPluginDir;
	}

	bCacheInitialized = true;

	// Method 1: Use IPluginManager (most reliable)
	IPluginManager& PluginManager = IPluginManager::Get();
	TSharedPtr<IPlugin> Plugin = PluginManager.FindPlugin(TEXT("Revvy"));
	if (Plugin.IsValid())
	{
		CachedPluginDir = Plugin->GetBaseDir();
		UE_LOG(LogRevvyPaths, Log, TEXT("Found Revvy plugin via IPluginManager: %s"), *CachedPluginDir);
		return CachedPluginDir;
	}

	// Method 2: Fallback - search known locations
	TArray<FString> SearchPaths;
	
	// Project plugins (local development)
	SearchPaths.Add(FPaths::ProjectPluginsDir() / TEXT("Revvy"));
	
	// Engine Marketplace (FAB install)
	SearchPaths.Add(FPaths::EnginePluginsDir() / TEXT("Marketplace") / TEXT("Revvy"));
	
	// Engine plugins root
	SearchPaths.Add(FPaths::EnginePluginsDir() / TEXT("Revvy"));
	
	// Scan Marketplace folder for any Revvy folder (FAB may use random names)
	FString MarketplacePath = FPaths::EnginePluginsDir() / TEXT("Marketplace");
	if (FPaths::DirectoryExists(MarketplacePath))
	{
		IFileManager& FileManager = IFileManager::Get();
		TArray<FString> Directories;
		FileManager.FindFiles(Directories, *(MarketplacePath / TEXT("*")), false, true);
		
		for (const FString& DirName : Directories)
		{
			FString PotentialPath = MarketplacePath / DirName;
			// Check if this directory contains Revvy.uplugin
			if (FPaths::FileExists(PotentialPath / TEXT("Revvy.uplugin")))
			{
				SearchPaths.Add(PotentialPath);
			}
		}
	}

	// Check each search path
	for (const FString& SearchPath : SearchPaths)
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(SearchPath);
		if (FPaths::DirectoryExists(AbsPath))
		{
			// Verify it's actually the Revvy plugin by checking for the .uplugin file
			if (FPaths::FileExists(AbsPath / TEXT("Revvy.uplugin")))
			{
				CachedPluginDir = AbsPath;
				UE_LOG(LogRevvyPaths, Log, TEXT("Found Revvy plugin via search: %s"), *CachedPluginDir);
				return CachedPluginDir;
			}
		}
	}

	UE_LOG(LogRevvyPaths, Warning, TEXT("Could not locate Revvy plugin directory"));
	return FString();
}

FString FRevvyPaths::GetPluginContentDir()
{
	FString PluginDir = GetPluginDir();
	if (PluginDir.IsEmpty())
	{
		return FString();
	}
	return PluginDir / TEXT("Content");
}

FString FRevvyPaths::GetHelpDir()
{
	FString ContentDir = GetPluginContentDir();
	if (ContentDir.IsEmpty())
	{
		return FString();
	}
	return ContentDir / TEXT("Help");
}

FString FRevvyPaths::GetInstructionsDir()
{
	FString ContentDir = GetPluginContentDir();
	if (ContentDir.IsEmpty())
	{
		return FString();
	}
	return ContentDir / TEXT("instructions");
}

FString FRevvyPaths::GetConfigDir()
{
	FString PluginDir = GetPluginDir();
	if (PluginDir.IsEmpty())
	{
		return FString();
	}
	return PluginDir / TEXT("Config");
}

FString FRevvyPaths::GetMemoryDir()
{
	// Use Project/Saved/Revvy/Memory — per-project, persistent across sessions.
	FString MemoryDir = FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("Memory");

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*MemoryDir))
	{
		PlatformFile.CreateDirectoryTree(*MemoryDir);
	}

	return MemoryDir;
}

FString FRevvyPaths::GetPluginVersionName()
{
	IPluginManager& PluginManager = IPluginManager::Get();
	TSharedPtr<IPlugin> Plugin = PluginManager.FindPlugin(TEXT("Revvy"));
	if (Plugin.IsValid())
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
		if (!Descriptor.VersionName.IsEmpty())
		{
			return Descriptor.VersionName;
		}
	}
	return TEXT("unknown");
}

FString FRevvyPaths::GetScreenshotsDir()
{
	// Use Project/Saved/Revvy/Screenshots
	FString ScreenshotsDir = FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("Screenshots");
	
	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ScreenshotsDir))
	{
		PlatformFile.CreateDirectoryTree(*ScreenshotsDir);
	}
	
	return ScreenshotsDir;
}

void FRevvyPaths::ClearScreenshotsDir()
{
	FString ScreenshotsDir = FPaths::ProjectSavedDir() / TEXT("Revvy") / TEXT("Screenshots");
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (PlatformFile.DirectoryExists(*ScreenshotsDir))
	{
		// Delete all files and subdirectories
		IFileManager& FileManager = IFileManager::Get();
		FileManager.DeleteDirectory(*ScreenshotsDir, false, true);
		
		// Recreate the empty directory
		PlatformFile.CreateDirectoryTree(*ScreenshotsDir);
		
		UE_LOG(LogRevvyPaths, Log, TEXT("Cleared Revvy screenshots directory: %s"), *ScreenshotsDir);
	}
}
