// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UServiceScaffoldGeneratorService.h"

// Editor / Engine
#include "Editor.h"

// File IO
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// JSON
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyScaffold, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a JSON object to a compact FString. */
	FString ServiceScaffoldJsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return OutStr;
	}

	/** True iff Name follows the Revvy "U*Service" convention. */
	bool IsValidServiceClassName(const FString& Name)
	{
		if (Name.Len() < 2) { return false; }
		if (!Name.StartsWith(TEXT("U"))) { return false; }
		if (!Name.EndsWith(TEXT("Service"))) { return false; }
		// Reject bare "UService" (no domain segment).
		if (Name.Equals(TEXT("UService"))) { return false; }
		// Second char must be uppercase (PascalCase after the 'U' prefix).
		const TCHAR Second = Name[1];
		if (!(Second >= TEXT('A') && Second <= TEXT('Z'))) { return false; }
		return true;
	}

	/**
	 * Sanitize a category domain segment so it is safe to embed inside a
	 * UFUNCTION Category = "Revvy|<Domain>|Query" string literal and inside a
	 * JSON return payload. Only [A-Za-z0-9_] survive; everything else is
	 * dropped. Empty input falls back to "Generic".
	 */
	FString SanitizeCategoryDomain(const FString& InDomain)
	{
		FString Out;
		Out.Reserve(InDomain.Len());
		for (int32 i = 0; i < InDomain.Len(); ++i)
		{
			const TCHAR Ch = InDomain[i];
			const bool bIsAlpha = (Ch >= TEXT('a') && Ch <= TEXT('z')) || (Ch >= TEXT('A') && Ch <= TEXT('Z'));
			const bool bIsDigit = (Ch >= TEXT('0') && Ch <= TEXT('9'));
			const bool bIsUnderscore = (Ch == TEXT('_'));
			if (bIsAlpha || bIsDigit || bIsUnderscore)
			{
				Out.AppendChar(Ch);
			}
		}
		if (Out.IsEmpty())
		{
			Out = TEXT("Generic");
		}
		return Out;
	}

	/** Ensure directory exists (recursive). Returns true on success. */
	bool EnsureDirectoryExists(const FString& InDir)
	{
		if (InDir.IsEmpty()) { return false; }
		IFileManager& FM = IFileManager::Get();
		if (FM.DirectoryExists(*InDir)) { return true; }
		return FM.MakeDirectory(*InDir, /*Tree=*/ true);
	}

	/** Compose the header (.h) file text for the given service class. */
	FString BuildHeaderText(const FString& ClassName, const FString& CategoryDomain)
	{
		return FString::Printf(
			TEXT("// Copyright Buckley Builds LLC 2026 All Rights Reserved.\r\n")
			TEXT("\r\n")
			TEXT("#pragma once\r\n")
			TEXT("\r\n")
			TEXT("#include \"CoreMinimal.h\"\r\n")
			TEXT("#include \"UObject/NoExportTypes.h\"\r\n")
			TEXT("#include \"%s.generated.h\"\r\n")
			TEXT("\r\n")
			TEXT("/**\r\n")
			TEXT(" * Auto-generated Revvy service.\r\n")
			TEXT(" * Category: Revvy|%s\r\n")
			TEXT(" * Python: unreal.%s.<ufn>()\r\n")
			TEXT(" */\r\n")
			TEXT("UCLASS(BlueprintType)\r\n")
			TEXT("class REVVY_API %s : public UObject\r\n")
			TEXT("{\r\n")
			TEXT("\tGENERATED_BODY()\r\n")
			TEXT("\r\n")
			TEXT("public:\r\n")
			TEXT("\tUFUNCTION(BlueprintCallable, Category = \"Revvy|%s|Query\")\r\n")
			TEXT("\tstatic FString GetInfo();\r\n")
			TEXT("};\r\n"),
			*ClassName,
			*CategoryDomain,
			*ClassName,
			*ClassName,
			*CategoryDomain);
	}

	/** Compose the implementation (.cpp) file text for the given service class. */
	FString BuildCppText(const FString& ClassName, const FString& CategoryDomain)
	{
		return FString::Printf(
			TEXT("// Copyright Buckley Builds LLC 2026 All Rights Reserved.\r\n")
			TEXT("\r\n")
			TEXT("#include \"PythonAPI/%s.h\"\r\n")
			TEXT("\r\n")
			TEXT("DEFINE_LOG_CATEGORY_STATIC(LogRevvy%s, Log, All);\r\n")
			TEXT("\r\n")
			TEXT("FString %s::GetInfo()\r\n")
			TEXT("{\r\n")
			TEXT("\treturn TEXT(\"{\\\"service\\\":\\\"%s\\\",\\\"stub\\\":true}\");\r\n")
			TEXT("}\r\n"),
			*ClassName,
			*CategoryDomain,
			*ClassName,
			*ClassName);
	}
}

// =================================================================
// UServiceScaffoldGeneratorService
// =================================================================

FString UServiceScaffoldGeneratorService::GenerateServiceScaffold(const FString& ServiceClassName, const FString& CategoryDomain, const FString& OutputDir)
{
	// --- validation ---------------------------------------------------------
	if (!IsValidServiceClassName(ServiceClassName))
	{
		UE_LOG(LogRevvyScaffold, Error,
			TEXT("GenerateServiceScaffold: invalid class name '%s' (must start with 'U' and end with 'Service')."),
			*ServiceClassName);
		return FString();
	}

	if (OutputDir.IsEmpty())
	{
		UE_LOG(LogRevvyScaffold, Error, TEXT("GenerateServiceScaffold: OutputDir is empty."));
		return FString();
	}

	const FString SafeDomain = SanitizeCategoryDomain(CategoryDomain);

	// --- ensure directory ---------------------------------------------------
	const FString NormalizedDir = FPaths::ConvertRelativePathToFull(OutputDir);
	if (!EnsureDirectoryExists(NormalizedDir))
	{
		UE_LOG(LogRevvyScaffold, Error,
			TEXT("GenerateServiceScaffold: could not create OutputDir '%s'."),
			*NormalizedDir);
		return FString();
	}

	// --- compose paths ------------------------------------------------------
	const FString HeaderPath = FPaths::Combine(NormalizedDir, ServiceClassName + TEXT(".h"));
	const FString CppPath    = FPaths::Combine(NormalizedDir, ServiceClassName + TEXT(".cpp"));

	// --- compose text -------------------------------------------------------
	const FString HeaderText = BuildHeaderText(ServiceClassName, SafeDomain);
	const FString CppText    = BuildCppText(ServiceClassName, SafeDomain);

	// --- write files --------------------------------------------------------
	if (!FFileHelper::SaveStringToFile(HeaderText, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRevvyScaffold, Error, TEXT("GenerateServiceScaffold: failed to write header '%s'."), *HeaderPath);
		return FString();
	}
	if (!FFileHelper::SaveStringToFile(CppText, *CppPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogRevvyScaffold, Error, TEXT("GenerateServiceScaffold: failed to write cpp '%s'."), *CppPath);
		return FString();
	}

	UE_LOG(LogRevvyScaffold, Log,
		TEXT("GenerateServiceScaffold: emitted '%s' + '%s' (domain='%s')."),
		*HeaderPath, *CppPath, *SafeDomain);

	// --- build JSON result --------------------------------------------------
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("header_path"),     HeaderPath);
	Root->SetStringField(TEXT("cpp_path"),        CppPath);
	Root->SetStringField(TEXT("class_name"),      ServiceClassName);
	Root->SetStringField(TEXT("category_domain"), SafeDomain);
	return ServiceScaffoldJsonObjectToString(Root);
}
