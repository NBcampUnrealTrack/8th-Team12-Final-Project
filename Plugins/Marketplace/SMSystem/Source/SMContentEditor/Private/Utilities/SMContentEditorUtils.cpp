// Copyright Recursoft LLC. All Rights Reserved.

#include "Utilities/SMContentEditorUtils.h"

#include "Assets/SMContentAsset.h"
#include "Assets/SMInstalledContentAsset.h"
#include "SMContentEditorLog.h"

#include "Blueprints/SMBlueprint.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetViewUtils.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManagerGeneric.h"
#include "HAL/PlatformFileManager.h"
#include "InputMappingContext.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/SecureHash.h"
#include "PakFileUtilities.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "SMContentEditorUtils"

namespace LD::ContentEditorPackageUtils
{
	static TMap<TWeakObjectPtr<const USMContentAsset>, TArray<FName>> CachedSourcePaths;
	
	TArray<FSMStoredGameplayTag> GatherGameplayTags()
	{
		TArray<FSMStoredGameplayTag> OutTags;

		// Use the dedicated INI file.
		const FString IniFilePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / FGameplayTagSource::GetDefaultName().ToString());

		if (!FPaths::FileExists(IniFilePath))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s not found at %s"), *FGameplayTagSource::GetDefaultName().ToString(), *IniFilePath);
			return OutTags;
		}

		FConfigFile ConfigFile;
		ConfigFile.Read(IniFilePath);

		// Find the "GameplayTags" section.
		const FConfigSection* TagSection = ConfigFile.FindSection(TEXT("/Script/GameplayTags.GameplayTagsSettings"));
		if (!TagSection)
		{
			UE_LOG(LogTemp, Warning, TEXT("Section [GameplayTags] not found in %s"), *IniFilePath);
			return OutTags;
		}

		for (const TTuple<FName, FConfigValue>& Entry : *TagSection)
		{
			if (Entry.Key != TEXT("+GameplayTagList"))
			{
				continue;
			}

			// The value should look like: (Tag="MyGame.ExampleTag", DevComment="Some comment")
			FString EntryValue = Entry.Value.GetValue();

			// Parse out the Tag value. We look for Tag="  then extract until the closing quote.
			FString ParsedTag;
			if (FParse::Value(*EntryValue, TEXT("Tag=\""), ParsedTag))
			{
				// Remove anything after the closing quote.
				int32 QuoteIndex = ParsedTag.Find(TEXT("\""));
				if (QuoteIndex != INDEX_NONE)
				{
					ParsedTag = ParsedTag.Left(QuoteIndex);
				}

				// Request the gameplay tag. The "false" parameter prevents auto-adding unknown tags.
				const FGameplayTag GameplayTag = FGameplayTag::RequestGameplayTag(FName(*ParsedTag), false);
				if (GameplayTag.IsValid())
				{
					OutTags.Add(GameplayTag.ToString());
				}
			}
		}

		return OutTags;
	}
}

void LD::ContentEditorPackageUtils::PackageContent(USMContentAsset* InAsset, TArray<FString>& OutFilesProcessed)
{
	check(InAsset);

	const FString SourceFolderPath = InAsset->GetDirectoryPath() / USMContentAsset::GetStagedFilesDirectoryName();
	const FString DestFilePath = InAsset->GetPackFilePath();

	TArray<FAssetData> StagedAssets = InAsset->GetStagedAssets();
	if (!ensureMsgf(StagedAssets.Num() > 0, TEXT("No assets found to be packaged under folder path %s."), *SourceFolderPath))
	{
		return;
	}

	if (InAsset->bAutoPopulateGameplayTags)
	{
		TArray<FSMStoredGameplayTag> GameplayTags = GatherGameplayTags();
		InAsset->SetGameplayTags(MoveTemp(GameplayTags));
	}
	
	InAsset->SetInputMappingContext(nullptr);

	OutFilesProcessed.Reset(StagedAssets.Num());
	for (FAssetData& Asset : StagedAssets)
	{
		OutFilesProcessed.Add(Asset.AssetName.ToString());

		if (const UInputMappingContext* InputMappingContext = Cast<UInputMappingContext>(Asset.GetAsset()))
		{
			if (ensureMsgf(InAsset->GetInputActionsMapping().Num() == 0, TEXT("Only one input mapping context is supported per content sample.")))
			{
				InAsset->SetInputMappingContext(InputMappingContext);
			}
		}
	}

	const FString CommandLineArgs = FString::Printf(TEXT("%s -Create=%s/ -compress -compressionformat=Oodle -compressionblocksize=256KB"), *DestFilePath, *SourceFolderPath);

	check(ExecuteUnrealPak(*CommandLineArgs));
}

void LD::ContentEditorPackageUtils::ExtractPackage(const USMContentAsset* InAsset)
{
	check(InAsset);

	const FString SourceFilePath = InAsset->GetPackFilePath();
	const FString DestFolderPath = InAsset->GetDirectoryPath() / USMContentAsset::GetStagedFilesDirectoryName();

	const FString CommandLineArgs = FString::Printf(TEXT("%s -Extract %s"), *SourceFilePath, *DestFolderPath);

	if (!ensureMsgf(ExecuteUnrealPak(*CommandLineArgs), TEXT("Failed to extract Pak file.")))
	{
		return;
	}

	// Find all of the file paths and scan them into the asset registry.
	{
		TArray<FString> FilePaths;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.IterateDirectoryRecursively(*DestFolderPath, [&] (const TCHAR* InFilenameOrDirectory, const bool bInIsDirectory)-> bool
		{
			if (!bInIsDirectory)
			{
				const FString FilePath(InFilenameOrDirectory);
				FilePaths.Add(FilePath);
			}
			return true;
		});

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();
		AssetRegistry.ScanFilesSynchronous(FilePaths, true);
	}

	TArray<FName>& PackagePaths = CachedSourcePaths.FindOrAdd(InAsset);
	if (PackagePaths.Num() == 0)
	{
		TArray<FAssetData> Assets = InAsset->GetStagedAssets();
		check(Assets.Num() > 0);

		for (const FAssetData& Asset : Assets)
		{
			PackagePaths.Add(Asset.PackageName);
		}
	}
	else
	{
		// This has been run already for this session. The asset scan isn't enough and the package needs to reload.
		// This might be a bug in UE. Loading the package manually fixes the problem.
		for (const FName& PackagePath : PackagePaths)
		{
			const UPackage* NewPackage = LoadPackage(nullptr, *PackagePath.ToString(), LOAD_None);
			ensure(NewPackage);
		}
	}

	ensureMsgf(InAsset->IsExtractedAndStaged(), TEXT("Not all files extracted for asset %s."), *InAsset->Name.ToString());
}

void LD::ContentEditorPackageUtils::CleanupExtractedPackage(const USMContentAsset* InAsset)
{
	check(InAsset);

	constexpr bool bRelative = false;
	const FString SourcePath = InAsset->GetDirectoryPath(bRelative) / USMContentAsset::GetStagedFilesDirectoryName();
	ensureMsgf(FFileManagerGeneric::Get().DeleteDirectory(*SourcePath, true, true),
		TEXT("Could not cleanup staging directory %s. You may need to delete this manually."), *SourcePath);
}

bool LD::ContentEditorPackageUtils::InstallContent(const USMContentAsset* InAsset, const FString& InTargetPath,
	const USMContentProjectConfigurationViewModel* InProjectConfigViewModel, bool bOpenMapOnInstall)
{
	check(InAsset);

	const bool bInstallingToPluginContent = InAsset->bInstallToPluginContent;

	FString TargetPath = InTargetPath;

	// Plugin content will manage its own path, otherwise the provided path is required for project installs.
	const FString AbsoluteTargetPath = bInstallingToPluginContent ? TargetPath :
	FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(TargetPath + TEXT("/")));

	// Install any dependencies.
	for (const TSoftObjectPtr<USMContentAsset>& Dependency : InAsset->ContentAssetDependencies)
	{
		const USMContentAsset* ContentAsset = Dependency.LoadSynchronous();
		if (!ensure(ContentAsset) || !ensureMsgf(ContentAsset->bInstallToPluginContent,
			TEXT("Only plugin content can be used as a dependency. Project content dependences are not supported.")))
		{
			return false;
		}

		InstallContent(ContentAsset, ContentAsset->GetDirectoryPath(true));
	}

	if (bInstallingToPluginContent)
	{
		LDCONTENTEDITOR_LOG_INFO(TEXT("Installing '%s' to plugin content folder."), *InAsset->Name.ToString());

		bool bNeedsInstall = true;

		// Check if already installed.
		if (InAsset->IsExtractedAndStaged())
		{
			if (IsInstalledContentUpToDate(InAsset))
			{
				LDCONTENTEDITOR_LOG_INFO(TEXT("Plugin content '%s' is already up-to-date."), *InAsset->Name.ToString());
				bNeedsInstall = false;
			}
			else
			{
				LDCONTENTEDITOR_LOG_INFO(TEXT("Plugin content '%s' is outdated, attempting reinstall."), *InAsset->Name.ToString());
			}
		}

		if (!bNeedsInstall)
		{
			return false;
		}
	}
	
	if (!bInstallingToPluginContent && IFileManager::Get().DirectoryExists(*AbsoluteTargetPath))
	{
		// Don't create over an existing directory.
		FNotificationInfo Info(FText::Format(LOCTEXT("DirectoryExists", "Cannot install content '{0}', the directory '{1}' already exists in the installation path."),
			InAsset->Name, FText::FromString(TargetPath)));

		Info.bUseLargeFont = false;
		Info.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return false;
	}

	// Load any tags.
	const TArray<FSMStoredGameplayTag>& StoredGameplayTags = InAsset->GetGameplayTags();
	if (StoredGameplayTags.Num() > 0)
	{
		IGameplayTagsEditorModule& GameplayTagsEditorModule = FModuleManager::LoadModuleChecked<IGameplayTagsEditorModule>("GameplayTagsEditor");

		// Iterate through each stored gameplay tag.
		for (const FSMStoredGameplayTag& StoredTag : StoredGameplayTags)
		{
			const FString TagString = StoredTag.Tag;
			
			const FGameplayTag ExistingTag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
			if (ExistingTag.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("Tag \"%s\" already exists. Skipping injection to preserve additional data."), *TagString);
				continue;
			}

			const FString DevComment = FString::Printf(TEXT("Added by Logic Driver %s Sample"), *InAsset->GetName());
			
			GameplayTagsEditorModule.AddNewGameplayTagToINI(
				TagString,				// New tag in string format ("MyGame.TagName")
				DevComment,				// Comment that will appear in the INI file
				NAME_None,				// Tag source name (none to use default)
				false,					// bIsRestrictedTag: set to true if the tag is restricted
				true					// bAllowNonRestrictedChildren: allows children for non-restricted tags
			);

			UE_LOG(LogTemp, Log, TEXT("Injected tag: %s"), *TagString);
		}
	}
	
	// Extract UAssets from the pak, stored within plugin content directory.
	ExtractPackage(InAsset);

	if (bInstallingToPluginContent)
	{
		// Create the installed content data asset so versioning can be tracked.
		UpdateOrCreateInstalledContentAsset(InAsset);

		// Nothing else left to do.
		return true;
	}

	TArray<FAssetData> Assets = InAsset->GetStagedAssets();

	TArray<FString> AssetSourcePaths;
	for (const FAssetData& Asset : Assets)
	{
		AssetSourcePaths.Add(Asset.PackagePath.ToString());
	}

	const bool bDirectoryCreated = IFileManager::Get().MakeDirectory(*AbsoluteTargetPath);
	if (!ensure(bDirectoryCreated))
	{
		CleanupExtractedPackage(InAsset);

		// Don't create over an existing directory.
		FNotificationInfo Info(FText::Format(LOCTEXT("DirectoryCreateFail", "Failed to create context directory '{0}'"),
			FText::FromString(TargetPath)));

		Info.bUseLargeFont = false;
		Info.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}

		return false;
	}

	TargetPath.RemoveFromEnd(TEXT("/"));

	// Migrate the files to the correct install location.

	const FString SourcePath = InAsset->GetStagedFilesPath(true);
	const bool bFoldersMoved = AssetViewUtils::MoveFolders(TArray<FString>{SourcePath}, TargetPath);
	if (!ensure(bFoldersMoved))
	{
		CleanupExtractedPackage(InAsset);

		FNotificationInfo Info(FText::Format(LOCTEXT("FolderMoveFailed", "Failed to move folders from '{0}' to '{1}'"),
			FText::FromString(SourcePath), FText::FromString(TargetPath)));

		Info.bUseLargeFont = false;
		Info.ExpireDuration = 5.0f;

		TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
		if (Notification.IsValid())
		{
			Notification->SetCompletionState(SNotificationItem::CS_Fail);
		}
		return false;
	}

	FString MapToLoad;
	TSet<UPackage*> PackagesLoaded;
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();
		TArray<FAssetData> DestinationContentAssets;
		AssetRegistry.GetAssetsByPath(*TargetPath, DestinationContentAssets, true);

		for (const FAssetData& DestinationAsset : DestinationContentAssets)
		{
			if (USMBlueprint* Blueprint = Cast<USMBlueprint>(DestinationAsset.GetAsset()))
			{
				// Compile state machine blueprints. Can help relink references such as for string tables.
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
			}
			else if (UWorld* World = Cast<UWorld>(DestinationAsset.GetAsset()))
			{
				if (World->GetName() == InAsset->PrimaryMapName)
				{
					MapToLoad = DestinationAsset.PackageName.ToString();
				}
			}

			PackagesLoaded.Add(DestinationAsset.GetPackage());
		}
	}

	if (GEditor)
	{
		if (GEditor->Trans)
		{
			GEditor->Trans->Reset(LOCTEXT("ContentClearTransactions", "Undo stack cleared after Logic Driver content load."));
		}
		GEditor->ForceGarbageCollection(true);
	}
	
	bool bNewLevelLoaded = false;
	if (bOpenMapOnInstall && !MapToLoad.IsEmpty())
	{
		const FString MapPackageFilename = FPackageName::LongPackageNameToFilename(MapToLoad, FPackageName::GetMapPackageExtension());
		FText Error;
		if (FEditorFileUtils::IsValidMapFilename(MapPackageFilename, Error))
		{
			if (FPaths::FileExists(MapPackageFilename))
			{
				// If there are any unsaved changes to the current level, see if the user wants to save those first.
				if (FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/true, /*bSaveMapPackages*/true, /*bSaveContentPackages*/false))
				{
					bNewLevelLoaded = FEditorFileUtils::LoadMap(*MapPackageFilename);
				}
			}
		}
	}

	if (!bNewLevelLoaded)
	{
		// Unload packages
		bool bValue = UPackageTools::UnloadPackages(PackagesLoaded.Array());
		ensure(bValue);
	}

	return true;
}

const FString& LD::ContentEditorPackageUtils::GetInstalledContentAssetName()
{
	static FString InstalledContentAssetName = TEXT("InstalledContentAsset");
	return InstalledContentAssetName;
}

namespace LD::ContentEditorPackageUtils
{
	/*
	 * Retrieve the MD5 hash from a file, using a cache system for quick retrieval.
	 */
	FMD5Hash GetMD5ForFile(const FString& FileName, bool bUpdateCache)
	{
		static TMap<FString, FMD5Hash> FileHashCache;
		const FString FullFilePath = FPaths::ConvertRelativePathToFull(FileName);

		if (!bUpdateCache)
		{
			if (const FMD5Hash* Hash = FileHashCache.Find(FullFilePath))
			{
				return *Hash;
			}
		}

		const FMD5Hash Hash = FMD5Hash::HashFile(*FileName);
		FileHashCache.Add(FullFilePath, Hash);

		return Hash;
	}
}

USMInstalledContentAsset* LD::ContentEditorPackageUtils::UpdateOrCreateInstalledContentAsset(
	const USMContentAsset* InContentAsset)
{
	const FString DestinationPath = InContentAsset->GetDirectoryPath(true);
	const FString& InstalledContentAssetName = GetInstalledContentAssetName();
	FString FullPath = FPaths::Combine(DestinationPath, InstalledContentAssetName);
	FullPath = FPaths::SetExtension(FullPath, TEXT("uasset"));

	USMInstalledContentAsset* InstalledContentAsset = FindInstalledContentAsset(DestinationPath);
	if (InstalledContentAsset == nullptr)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(FullPath);
		const FString NewAssetName = FPackageName::GetLongPackageAssetName(PackageName);

		UPackage* Package = CreatePackage(*PackageName);
		check(Package);
		Package->FullyLoad();

		constexpr EObjectFlags Flags = RF_Public | RF_Standalone;
		InstalledContentAsset = NewObject<USMInstalledContentAsset>(Package, FName(*NewAssetName), Flags);

		FAssetRegistryModule::AssetCreated(InstalledContentAsset);

		Package->MarkPackageDirty();
	}

	check(InstalledContentAsset);

	InstalledContentAsset->Modify();
	InstalledContentAsset->PAKFileHash = GetMD5ForFile(*InContentAsset->GetPackFilePath(), true);
	check(InstalledContentAsset->PAKFileHash.IsValid());

	FEditorFileUtils::PromptForCheckoutAndSave({InstalledContentAsset->GetPackage()}, true, false);

	return InstalledContentAsset;
}

USMInstalledContentAsset* LD::ContentEditorPackageUtils::FindInstalledContentAsset(const FString& InDestinationPath)
{
	const FString& InstalledContentAssetName = GetInstalledContentAssetName();

	USMInstalledContentAsset* InstalledContentAsset = nullptr;

	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(*InDestinationPath);
	Filter.bRecursivePaths = true;

	TArray<FAssetData> AssetData;

	if (AssetRegistry.GetAssets(Filter, AssetData))
	{
		for (FAssetData& Asset : AssetData)
		{
			if (Asset.AssetName == *InstalledContentAssetName)
			{
				InstalledContentAsset = Cast<USMInstalledContentAsset>(Asset.GetAsset());
				break;
			}
		}
	}

	return InstalledContentAsset;
}

bool LD::ContentEditorPackageUtils::IsInstalledContentUpToDate(const USMContentAsset* InContentAsset)
{
	check(InContentAsset);
	if (const USMInstalledContentAsset* InInstalledContentAsset = FindInstalledContentAsset(InContentAsset->GetDirectoryPath(true)))
	{
		const FMD5Hash CurrentHash = GetMD5ForFile(*InContentAsset->GetPackFilePath(), false);
		check(CurrentHash.IsValid());

		return CurrentHash == InInstalledContentAsset->PAKFileHash;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
