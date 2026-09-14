// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UAssetVersionDiffService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyAssetDiff, Log, All);

// Editor / engine
#include "Editor.h"

// Filesystem + package name resolution
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"

// Package / UObject
#include "UObject/Package.h"

// Asset registry
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"

// JSON
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Local helpers
// =================================================================
namespace
{
	/** Access the AssetRegistry module (loads it if not yet loaded). */
	IAssetRegistry& AssetVersionDiffGetAssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	}

	/** Serialize a JSON object to a compact FString. */
	FString AssetVersionDiffJsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return OutStr;
	}

	/**
	 * Given a user-provided path (object path OR package path), return the
	 * package-only portion (e.g. "/Game/Enemies/BP_Boss"). Strips a trailing
	 * ".ObjectName" suffix if present.
	 */
	FString ToPackagePath(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return FString();
		}

		FString PackagePath = AssetPath;
		int32 DotIndex = INDEX_NONE;
		if (PackagePath.FindChar(TEXT('.'), DotIndex))
		{
			PackagePath = PackagePath.Left(DotIndex);
		}
		return PackagePath;
	}

	/**
	 * Resolve a package path to its on-disk .uasset filename. Returns true on
	 * success. Handles both existing .uasset and .umap by asking PackageName
	 * for the right extension.
	 */
	bool ResolveFilesystemFilename(const FString& PackagePath, FString& OutFilename)
	{
		if (PackagePath.IsEmpty())
		{
			return false;
		}

		// Try LongPackageName -> filename with the default asset extension first.
		if (FPackageName::TryConvertLongPackageNameToFilename(
				PackagePath, OutFilename, FPackageName::GetAssetPackageExtension()))
		{
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutFilename))
			{
				return true;
			}
		}

		// Fall back to .umap for map packages.
		if (FPackageName::TryConvertLongPackageNameToFilename(
				PackagePath, OutFilename, FPackageName::GetMapPackageExtension()))
		{
			if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*OutFilename))
			{
				return true;
			}
		}

		OutFilename.Reset();
		return false;
	}
}

// =================================================================
// GetAssetSize
// =================================================================
int64 UAssetVersionDiffService::GetAssetSize(const FString& AssetPath)
{
	const FString PackagePath = ToPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		UE_LOG(LogRevvyAssetDiff, Warning, TEXT("GetAssetSize: empty asset path."));
		return -1;
	}

	FString Filename;
	if (!ResolveFilesystemFilename(PackagePath, Filename))
	{
		UE_LOG(LogRevvyAssetDiff, Warning,
			TEXT("GetAssetSize: could not resolve filesystem path for '%s'."), *PackagePath);
		return -1;
	}

	const int64 Size = IFileManager::Get().FileSize(*Filename);
	if (Size < 0)
	{
		UE_LOG(LogRevvyAssetDiff, Warning,
			TEXT("GetAssetSize: FileSize failed for '%s'."), *Filename);
		return -1;
	}
	return Size;
}

// =================================================================
// GetAssetLastModifiedTime
// =================================================================
FString UAssetVersionDiffService::GetAssetLastModifiedTime(const FString& AssetPath)
{
	const FString PackagePath = ToPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		UE_LOG(LogRevvyAssetDiff, Warning, TEXT("GetAssetLastModifiedTime: empty asset path."));
		return FString();
	}

	FString Filename;
	if (!ResolveFilesystemFilename(PackagePath, Filename))
	{
		UE_LOG(LogRevvyAssetDiff, Warning,
			TEXT("GetAssetLastModifiedTime: could not resolve filesystem path for '%s'."), *PackagePath);
		return FString();
	}

	const FDateTime Time = IFileManager::Get().GetTimeStamp(*Filename);
	if (Time == FDateTime::MinValue())
	{
		UE_LOG(LogRevvyAssetDiff, Warning,
			TEXT("GetAssetLastModifiedTime: GetTimeStamp returned MinValue for '%s'."), *Filename);
		return FString();
	}
	return Time.ToIso8601();
}

// =================================================================
// IsAssetDirty
// =================================================================
bool UAssetVersionDiffService::IsAssetDirty(const FString& AssetPath)
{
	const FString PackagePath = ToPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		return false;
	}

	UPackage* Pkg = FindPackage(nullptr, *PackagePath);
	if (!Pkg)
	{
		// An unloaded package cannot be dirty.
		return false;
	}
	return Pkg->IsDirty();
}

// =================================================================
// GetAssetPackageDiagnostics
// =================================================================
FString UAssetVersionDiffService::GetAssetPackageDiagnostics(const FString& AssetPath)
{
	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();

	// Defaults for the missing-asset case.
	int64  PackageSizeBytes = -1;
	int32  DepCount         = 0;
	int32  RefCount         = 0;
	FString LastModified;
	bool   bIsDirty         = false;
	uint32 PackageFlags     = 0;

	const FString PackagePath = ToPackagePath(AssetPath);
	if (PackagePath.IsEmpty())
	{
		UE_LOG(LogRevvyAssetDiff, Warning, TEXT("GetAssetPackageDiagnostics: empty asset path."));
	}
	else
	{
		// Filesystem side: size + last-modified.
		FString Filename;
		if (ResolveFilesystemFilename(PackagePath, Filename))
		{
			const int64 Size = IFileManager::Get().FileSize(*Filename);
			if (Size >= 0)
			{
				PackageSizeBytes = Size;
			}
			const FDateTime Time = IFileManager::Get().GetTimeStamp(*Filename);
			if (Time != FDateTime::MinValue())
			{
				LastModified = Time.ToIso8601();
			}
		}
		else
		{
			UE_LOG(LogRevvyAssetDiff, Warning,
				TEXT("GetAssetPackageDiagnostics: no filesystem path for '%s'."), *PackagePath);
		}

		// AssetRegistry side: dep + ref counts.
		IAssetRegistry& AR = AssetVersionDiffGetAssetRegistry();
		const FName PackageFName(*PackagePath);

		TArray<FName> Deps;
		AR.GetDependencies(PackageFName, Deps, UE::AssetRegistry::EDependencyCategory::Package);
		DepCount = Deps.Num();

		TArray<FName> Refs;
		AR.GetReferencers(PackageFName, Refs, UE::AssetRegistry::EDependencyCategory::Package);
		RefCount = Refs.Num();

		// In-memory side: dirty flag + package flags.
		if (UPackage* Pkg = FindPackage(nullptr, *PackagePath))
		{
			bIsDirty     = Pkg->IsDirty();
			PackageFlags = Pkg->GetPackageFlags();
		}
	}

	Out->SetNumberField(TEXT("package_size_bytes"), static_cast<double>(PackageSizeBytes));
	Out->SetNumberField(TEXT("dep_count"),          static_cast<double>(DepCount));
	Out->SetNumberField(TEXT("ref_count"),          static_cast<double>(RefCount));
	Out->SetStringField(TEXT("last_modified"),      LastModified);
	Out->SetBoolField  (TEXT("is_dirty"),           bIsDirty);
	Out->SetNumberField(TEXT("package_flags"),      static_cast<double>(PackageFlags));

	return AssetVersionDiffJsonObjectToString(Out);
}
