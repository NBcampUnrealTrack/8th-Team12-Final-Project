// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/URenderingSettingsService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyRender, Log, All);

// Console variable manager
#include "HAL/IConsoleManager.h"

// GEngine (fallback Exec for scalability preset)
#include "Engine/Engine.h"

// Scalability presets
#include "Scalability.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// =================================================================
// Local helpers
// =================================================================

namespace
{
	/** Serialize a TSharedPtr<FJsonObject> to a compact FString. */
	FString RenderingSettingsJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObj)
	{
		if (!JsonObj.IsValid())
		{
			return FString();
		}
		FString OutStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
		return OutStr;
	}

	/**
	 * Locate a CVar by name. Logs a Warning and returns nullptr if the
	 * variable is not registered.
	 */
	IConsoleVariable* FindCVarChecked(const TCHAR* Name)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!CVar)
		{
			UE_LOG(LogRevvyRender, Warning,
				TEXT("Console variable '%s' not found."), Name);
		}
		return CVar;
	}

	/** Read an int32 from a CVar, returning INDEX_NONE if missing. */
	int32 SafeGetInt(const TCHAR* Name)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return CVar->GetInt();
		}
		return INDEX_NONE;
	}

	/** Read a float from a CVar, returning -1.0 if missing. */
	float SafeGetFloat(const TCHAR* Name)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return CVar->GetFloat();
		}
		return -1.0f;
	}
}

// =================================================================
// GI / Lumen
// =================================================================

bool URenderingSettingsService::SetLumenEnabled(bool bEnabled)
{
	IConsoleVariable* GICVar = FindCVarChecked(TEXT("r.DynamicGlobalIlluminationMethod"));
	IConsoleVariable* ReflCVar = FindCVarChecked(TEXT("r.ReflectionMethod"));
	if (!GICVar || !ReflCVar)
	{
		return false;
	}

	const int32 OldGI = GICVar->GetInt();
	const int32 OldRefl = ReflCVar->GetInt();
	const int32 NewGI = bEnabled ? 1 : 0;
	const int32 NewRefl = bEnabled ? 1 : 0;

	GICVar->Set(NewGI, ECVF_SetByCode);
	ReflCVar->Set(NewRefl, ECVF_SetByCode);

	UE_LOG(LogRevvyRender, Log,
		TEXT("SetLumenEnabled(%d): GI %d -> %d, Reflection %d -> %d"),
		bEnabled ? 1 : 0, OldGI, NewGI, OldRefl, NewRefl);
	return true;
}

bool URenderingSettingsService::SetLumenReflectionsEnabled(bool bEnabled)
{
	IConsoleVariable* CVar = FindCVarChecked(TEXT("r.ReflectionMethod"));
	if (!CVar)
	{
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	const int32 NewVal = bEnabled ? 1 : 0;
	CVar->Set(NewVal, ECVF_SetByCode);

	UE_LOG(LogRevvyRender, Log,
		TEXT("SetLumenReflectionsEnabled(%d): r.ReflectionMethod %d -> %d"),
		bEnabled ? 1 : 0, OldVal, NewVal);
	return true;
}

bool URenderingSettingsService::SetGlobalIlluminationMethod(const FString& MethodStr)
{
	IConsoleVariable* CVar = FindCVarChecked(TEXT("r.DynamicGlobalIlluminationMethod"));
	if (!CVar)
	{
		return false;
	}

	int32 NewVal = INDEX_NONE;
	const FString Method = MethodStr.TrimStartAndEnd();
	if (Method.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		NewVal = 0;
	}
	else if (Method.Equals(TEXT("Lumen"), ESearchCase::IgnoreCase))
	{
		NewVal = 1;
	}
	else if (Method.Equals(TEXT("SSGI"), ESearchCase::IgnoreCase))
	{
		NewVal = 2;
	}
	else if (Method.Equals(TEXT("Plugin"), ESearchCase::IgnoreCase))
	{
		NewVal = 3;
	}
	else
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("SetGlobalIlluminationMethod: unknown method '%s' "
				"(expected None|Lumen|SSGI|Plugin)."), *MethodStr);
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	CVar->Set(NewVal, ECVF_SetByCode);
	UE_LOG(LogRevvyRender, Log,
		TEXT("SetGlobalIlluminationMethod('%s'): %d -> %d"),
		*MethodStr, OldVal, NewVal);
	return true;
}

// =================================================================
// Nanite
// =================================================================

bool URenderingSettingsService::SetNaniteEnabled(bool bEnabled)
{
	IConsoleVariable* CVar = FindCVarChecked(TEXT("r.Nanite"));
	if (!CVar)
	{
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	const int32 NewVal = bEnabled ? 1 : 0;
	CVar->Set(NewVal, ECVF_SetByCode);

	UE_LOG(LogRevvyRender, Log,
		TEXT("SetNaniteEnabled(%d): r.Nanite %d -> %d"),
		bEnabled ? 1 : 0, OldVal, NewVal);
	return true;
}

// =================================================================
// Anti-Aliasing
// =================================================================

bool URenderingSettingsService::SetAntiAliasingMethod(const FString& MethodStr)
{
	IConsoleVariable* CVar = FindCVarChecked(TEXT("r.AntiAliasingMethod"));
	if (!CVar)
	{
		return false;
	}

	int32 NewVal = INDEX_NONE;
	const FString Method = MethodStr.TrimStartAndEnd();
	if (Method.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		NewVal = 0;
	}
	else if (Method.Equals(TEXT("FXAA"), ESearchCase::IgnoreCase))
	{
		NewVal = 1;
	}
	else if (Method.Equals(TEXT("TAA"), ESearchCase::IgnoreCase))
	{
		NewVal = 2;
	}
	else if (Method.Equals(TEXT("MSAA"), ESearchCase::IgnoreCase))
	{
		NewVal = 3;
	}
	else if (Method.Equals(TEXT("TSR"), ESearchCase::IgnoreCase))
	{
		NewVal = 4;
	}
	else
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("SetAntiAliasingMethod: unknown method '%s' "
				"(expected None|FXAA|TAA|MSAA|TSR)."), *MethodStr);
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	CVar->Set(NewVal, ECVF_SetByCode);
	UE_LOG(LogRevvyRender, Log,
		TEXT("SetAntiAliasingMethod('%s'): r.AntiAliasingMethod %d -> %d"),
		*MethodStr, OldVal, NewVal);
	return true;
}

// =================================================================
// Scalability
// =================================================================

bool URenderingSettingsService::SetShadowQuality(int32 Quality)
{
	if (Quality < 0 || Quality > 4)
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("SetShadowQuality: Quality %d out of range [0,4]."), Quality);
		return false;
	}

	IConsoleVariable* CVar = FindCVarChecked(TEXT("sg.ShadowQuality"));
	if (!CVar)
	{
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	CVar->Set(Quality, ECVF_SetByCode);
	UE_LOG(LogRevvyRender, Log,
		TEXT("SetShadowQuality(%d): sg.ShadowQuality %d -> %d"),
		Quality, OldVal, Quality);
	return true;
}

bool URenderingSettingsService::SetPostProcessQuality(int32 Quality)
{
	if (Quality < 0 || Quality > 4)
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("SetPostProcessQuality: Quality %d out of range [0,4]."), Quality);
		return false;
	}

	IConsoleVariable* CVar = FindCVarChecked(TEXT("sg.PostProcessQuality"));
	if (!CVar)
	{
		return false;
	}

	const int32 OldVal = CVar->GetInt();
	CVar->Set(Quality, ECVF_SetByCode);
	UE_LOG(LogRevvyRender, Log,
		TEXT("SetPostProcessQuality(%d): sg.PostProcessQuality %d -> %d"),
		Quality, OldVal, Quality);
	return true;
}

bool URenderingSettingsService::SetViewDistanceScale(float Scale)
{
	if (!(Scale > 0.0f))
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("SetViewDistanceScale: Scale %.3f must be > 0."), Scale);
		return false;
	}

	IConsoleVariable* CVar = FindCVarChecked(TEXT("r.ViewDistanceScale"));
	if (!CVar)
	{
		return false;
	}

	const float OldVal = CVar->GetFloat();
	CVar->Set(Scale, ECVF_SetByCode);
	UE_LOG(LogRevvyRender, Log,
		TEXT("SetViewDistanceScale(%.3f): r.ViewDistanceScale %.3f -> %.3f"),
		Scale, OldVal, Scale);
	return true;
}

// =================================================================
// Query / Reset
// =================================================================

FString URenderingSettingsService::GetCurrentRenderingSettings()
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();

	Json->SetNumberField(TEXT("gi_method"),
		SafeGetInt(TEXT("r.DynamicGlobalIlluminationMethod")));
	Json->SetNumberField(TEXT("reflection_method"),
		SafeGetInt(TEXT("r.ReflectionMethod")));
	Json->SetNumberField(TEXT("nanite"),
		SafeGetInt(TEXT("r.Nanite")));
	Json->SetNumberField(TEXT("aa_method"),
		SafeGetInt(TEXT("r.AntiAliasingMethod")));
	Json->SetNumberField(TEXT("shadow_quality"),
		SafeGetInt(TEXT("sg.ShadowQuality")));
	Json->SetNumberField(TEXT("post_process_quality"),
		SafeGetInt(TEXT("sg.PostProcessQuality")));
	Json->SetNumberField(TEXT("view_distance_scale"),
		SafeGetFloat(TEXT("r.ViewDistanceScale")));

	return RenderingSettingsJsonObjectToString(Json);
}

bool URenderingSettingsService::RestoreScalabilityDefaults(const FString& PresetName)
{
	int32 Level = INDEX_NONE;
	const FString Preset = PresetName.TrimStartAndEnd();
	if (Preset.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
	{
		Level = 0;
	}
	else if (Preset.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
	{
		Level = 1;
	}
	else if (Preset.Equals(TEXT("High"), ESearchCase::IgnoreCase))
	{
		Level = 2;
	}
	else if (Preset.Equals(TEXT("Epic"), ESearchCase::IgnoreCase))
	{
		Level = 3;
	}
	else if (Preset.Equals(TEXT("Cinematic"), ESearchCase::IgnoreCase))
	{
		Level = 4;
	}
	else
	{
		UE_LOG(LogRevvyRender, Warning,
			TEXT("RestoreScalabilityDefaults: unknown preset '%s' "
				"(expected Low|Medium|High|Epic|Cinematic)."), *PresetName);
		return false;
	}

	// Preferred path: Scalability::FQualityLevels + SetQualityLevels + SaveState.
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(Level);
	Scalability::SetQualityLevels(Levels);
	Scalability::SaveState(GGameIni);

	UE_LOG(LogRevvyRender, Log,
		TEXT("RestoreScalabilityDefaults('%s'): applied quality level %d."),
		*PresetName, Level);
	return true;
}
