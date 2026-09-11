// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UBuildInfoService.h"

// Editor / Engine
#include "Editor.h"
#include "Misc/EngineVersion.h"
#include "HAL/PlatformProperties.h"

// UObject reflection
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

// JSON
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyBuildInfo, Log, All);

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a single JSON object to a compact FString. */
	FString BuildInfoJsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
	{
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return OutStr;
	}

	/** True if the class was cooked from the Revvy plugin module. */
	bool BuildInfoIsRevvyClass(const UClass* Cls)
	{
		if (!Cls) { return false; }
		const FString PathName = Cls->GetPathName();
		return PathName.StartsWith(TEXT("/Script/Revvy."));
	}

	/** True if the class name matches the "U*Service" service convention. */
	bool BuildInfoIsServiceClassName(const UClass* Cls)
	{
		if (!Cls) { return false; }
		// UHT strips the leading 'U' from GetName(); check via GetPrefixCPP() +
		// "*Service" suffix — same convention as UServiceRegistryService.
		const FString Prefix = Cls->GetPrefixCPP();
		const FString Name   = Cls->GetName();
		return Prefix == TEXT("U") && Name.EndsWith(TEXT("Service"));
	}

	/** Identify the compiler used to build this translation unit. */
	const TCHAR* GetCompilerName()
	{
#if defined(__clang__)
		return TEXT("clang");
#elif defined(_MSC_VER)
		return TEXT("msvc");
#elif defined(__GNUC__)
		return TEXT("gcc");
#else
		return TEXT("unknown");
#endif
	}
}

// =================================================================
// GetRevvyBuildInfo
// =================================================================

FString UBuildInfoService::GetRevvyBuildInfo()
{
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();

	const FString BuildTimestamp = FString(TEXT(__DATE__)) + TEXT(" ") + FString(TEXT(__TIME__));
	const FString UEVersion      = FEngineVersion::Current().ToString();
	const FString PlatformName   = FString(FPlatformProperties::PlatformName());

#if WITH_EDITOR
	const bool bIsEditor = true;
#else
	const bool bIsEditor = false;
#endif

#if UE_BUILD_DEBUG
	const bool bIsDebug = true;
#else
	const bool bIsDebug = false;
#endif

#if UE_BUILD_SHIPPING
	const bool bIsShipping = true;
#else
	const bool bIsShipping = false;
#endif

	Entry->SetStringField(TEXT("revvy_version"),   TEXT("2026.07-loop68"));
	Entry->SetStringField(TEXT("build_timestamp"), BuildTimestamp);
	Entry->SetStringField(TEXT("ue_version"),      UEVersion);
	Entry->SetStringField(TEXT("platform"),        PlatformName);
	Entry->SetBoolField  (TEXT("is_editor"),       bIsEditor);
	Entry->SetBoolField  (TEXT("is_debug"),        bIsDebug);
	Entry->SetBoolField  (TEXT("is_shipping"),     bIsShipping);
	Entry->SetStringField(TEXT("compiler"),        GetCompilerName());

	UE_LOG(LogRevvyBuildInfo, Verbose,
		TEXT("GetRevvyBuildInfo: ue=%s platform=%s editor=%d debug=%d shipping=%d compiler=%s"),
		*UEVersion, *PlatformName,
		bIsEditor ? 1 : 0, bIsDebug ? 1 : 0, bIsShipping ? 1 : 0,
		GetCompilerName());

	return BuildInfoJsonObjectToString(Entry);
}

// =================================================================
// GetLoadedRevvyServiceCount
// =================================================================

int32 UBuildInfoService::GetLoadedRevvyServiceCount()
{
	int32 Count = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls) { continue; }
		if (!BuildInfoIsRevvyClass(Cls)) { continue; }
		if (!BuildInfoIsServiceClassName(Cls)) { continue; }
		++Count;
	}

	UE_LOG(LogRevvyBuildInfo, Verbose,
		TEXT("GetLoadedRevvyServiceCount: %d Revvy U*Service classes loaded."), Count);

	return Count;
}
