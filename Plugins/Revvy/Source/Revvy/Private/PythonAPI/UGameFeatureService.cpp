// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGameFeatureService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGF, Log, All);

// GameFeatures
#include "GameFeaturesSubsystem.h"
#include "GameFeatureData.h"
#include "GameFeatureAction.h"
#include "GameFeatureTypes.h"

// Plugin manager fallback for URL resolution
#include "Interfaces/IPluginManager.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// =================================================================
// Helpers
// =================================================================

namespace RevvyGameFeatureHelpers
{
	static UGameFeaturesSubsystem* GetSubsystem()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		return GEngine->GetEngineSubsystem<UGameFeaturesSubsystem>();
	}

	static FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	static FString SerializeJsonArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}

	static FString MakeErrorJson(const FString& Error)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Error);
		return SerializeJson(Obj);
	}

	/**
	 * Resolve a plugin URL from a bare plugin name.
	 *   1) UGameFeaturesSubsystem::GetPluginURLByName — canonical path when a
	 *      state machine already exists for the plugin.
	 *   2) Fallback: IPluginManager::FindPlugin(PluginName) + GetPluginURL_FileProtocol
	 *      for plugins that are installed on disk but have not yet been
	 *      picked up by the subsystem (e.g. never Registered).
	 * Returns true on success, populates OutURL. Logs a Warning otherwise.
	 */
	static bool ResolvePluginURL(const FString& PluginName, FString& OutURL)
	{
		if (PluginName.IsEmpty())
		{
			UE_LOG(LogRevvyGF, Warning, TEXT("ResolvePluginURL: empty plugin name."));
			return false;
		}

		UGameFeaturesSubsystem* Subsystem = GetSubsystem();
		if (Subsystem && Subsystem->GetPluginURLByName(FStringView(PluginName), OutURL) && !OutURL.IsEmpty())
		{
			return true;
		}

		// Fallback via IPluginManager.
		if (IPluginManager* PluginManager = &IPluginManager::Get())
		{
			TSharedPtr<IPlugin> Plugin = PluginManager->FindPlugin(PluginName);
			if (Plugin.IsValid())
			{
				const FString DescriptorPath = Plugin->GetDescriptorFileName();
				if (!DescriptorPath.IsEmpty())
				{
					OutURL = UGameFeaturesSubsystem::GetPluginURL_FileProtocol(DescriptorPath);
					if (!OutURL.IsEmpty())
					{
						return true;
					}
				}
			}
		}

		UE_LOG(LogRevvyGF, Warning,
			TEXT("ResolvePluginURL: could not resolve plugin URL for '%s'."),
			*PluginName);
		OutURL.Reset();
		return false;
	}

	/**
	 * Wrap ChangeGameFeatureTargetState with an empty completion delegate.
	 * Returns true when the request was accepted (subsystem + URL valid).
	 */
	static bool RequestTargetState(const FString& PluginName, EGameFeatureTargetState TargetState, const TCHAR* Context)
	{
		UGameFeaturesSubsystem* Subsystem = GetSubsystem();
		if (!Subsystem)
		{
			UE_LOG(LogRevvyGF, Warning, TEXT("%s: UGameFeaturesSubsystem unavailable."), Context);
			return false;
		}

		FString URL;
		if (!ResolvePluginURL(PluginName, URL))
		{
			UE_LOG(LogRevvyGF, Warning,
				TEXT("%s: could not resolve URL for plugin '%s'."),
				Context, *PluginName);
			return false;
		}

		Subsystem->ChangeGameFeatureTargetState(URL, TargetState, FGameFeaturePluginChangeStateComplete());
		UE_LOG(LogRevvyGF, Log,
			TEXT("%s: requested TargetState=%s for '%s' (URL=%s)."),
			Context, *LexToString(TargetState), *PluginName, *URL);
		return true;
	}
}

// =================================================================
// Query
// =================================================================

FString UGameFeatureService::ListGameFeaturePlugins()
{
	UGameFeaturesSubsystem* Subsystem = RevvyGameFeatureHelpers::GetSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogRevvyGF, Warning, TEXT("ListGameFeaturePlugins: UGameFeaturesSubsystem unavailable."));
		return TEXT("[]");
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	Subsystem->ForEachGameFeature(
		[&Items](FGameFeatureInfo&& Info)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Info.Name);
			Obj->SetStringField(TEXT("url"), Info.URL);
			Obj->SetStringField(TEXT("state"), UE::GameFeatures::ToString(Info.CurrentState));
			Obj->SetBoolField(TEXT("built_in"), Info.bLoadedAsBuiltIn);
			Items.Add(MakeShared<FJsonValueObject>(Obj));
		});

	return RevvyGameFeatureHelpers::SerializeJsonArray(Items);
}

FString UGameFeatureService::GetGameFeatureState(const FString& PluginName)
{
	UGameFeaturesSubsystem* Subsystem = RevvyGameFeatureHelpers::GetSubsystem();
	if (!Subsystem)
	{
		return RevvyGameFeatureHelpers::MakeErrorJson(TEXT("UGameFeaturesSubsystem unavailable"));
	}

	FString URL;
	if (!RevvyGameFeatureHelpers::ResolvePluginURL(PluginName, URL))
	{
		return RevvyGameFeatureHelpers::MakeErrorJson(
			FString::Printf(TEXT("Could not resolve URL for plugin '%s'"), *PluginName));
	}

	const EGameFeaturePluginState State = Subsystem->GetPluginState(URL);

	float InstallPercent = 0.f;
	Subsystem->GetGameFeaturePluginInstallPercent(URL, InstallPercent);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("name"), PluginName);
	Obj->SetStringField(TEXT("url"), URL);
	Obj->SetStringField(TEXT("state"), UE::GameFeatures::ToString(State));
	Obj->SetNumberField(TEXT("progress"), InstallPercent);
	return RevvyGameFeatureHelpers::SerializeJson(Obj);
}

FString UGameFeatureService::ListGameFeatureActions(const FString& PluginName)
{
	UGameFeaturesSubsystem* Subsystem = RevvyGameFeatureHelpers::GetSubsystem();
	if (!Subsystem)
	{
		return RevvyGameFeatureHelpers::MakeErrorJson(TEXT("UGameFeaturesSubsystem unavailable"));
	}

	FString URL;
	if (!RevvyGameFeatureHelpers::ResolvePluginURL(PluginName, URL))
	{
		return RevvyGameFeatureHelpers::MakeErrorJson(
			FString::Printf(TEXT("Could not resolve URL for plugin '%s'"), *PluginName));
	}

	const UGameFeatureData* Data = Subsystem->GetGameFeatureDataForRegisteredPluginByURL(URL, /*bCheckForRegistering=*/true);
	if (!Data)
	{
		return RevvyGameFeatureHelpers::MakeErrorJson(
			FString::Printf(TEXT("Plugin '%s' has no resolved UGameFeatureData (must be at least Registered)"), *PluginName));
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const UGameFeatureAction* Action : Data->GetActions())
	{
		if (!Action)
		{
			continue;
		}

		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("action_class"), Action->GetClass()->GetName());
		Obj->SetStringField(TEXT("class_path"), Action->GetClass()->GetPathName());
		Items.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return RevvyGameFeatureHelpers::SerializeJsonArray(Items);
}

// =================================================================
// State Transitions
// =================================================================

bool UGameFeatureService::LoadGameFeatureToRegistered(const FString& PluginName)
{
	return RevvyGameFeatureHelpers::RequestTargetState(
		PluginName, EGameFeatureTargetState::Registered, TEXT("LoadGameFeatureToRegistered"));
}

bool UGameFeatureService::LoadGameFeatureToLoaded(const FString& PluginName)
{
	return RevvyGameFeatureHelpers::RequestTargetState(
		PluginName, EGameFeatureTargetState::Loaded, TEXT("LoadGameFeatureToLoaded"));
}

bool UGameFeatureService::LoadGameFeatureToActive(const FString& PluginName)
{
	return RevvyGameFeatureHelpers::RequestTargetState(
		PluginName, EGameFeatureTargetState::Active, TEXT("LoadGameFeatureToActive"));
}

bool UGameFeatureService::DeactivateGameFeature(const FString& PluginName)
{
	UGameFeaturesSubsystem* Subsystem = RevvyGameFeatureHelpers::GetSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogRevvyGF, Warning, TEXT("DeactivateGameFeature: UGameFeaturesSubsystem unavailable."));
		return false;
	}

	FString URL;
	if (!RevvyGameFeatureHelpers::ResolvePluginURL(PluginName, URL))
	{
		UE_LOG(LogRevvyGF, Warning,
			TEXT("DeactivateGameFeature: could not resolve URL for plugin '%s'."),
			*PluginName);
		return false;
	}

	Subsystem->DeactivateGameFeaturePlugin(URL, FGameFeaturePluginDeactivateComplete());
	UE_LOG(LogRevvyGF, Log,
		TEXT("DeactivateGameFeature: deactivation requested for '%s' (URL=%s)."),
		*PluginName, *URL);
	return true;
}

bool UGameFeatureService::UnloadGameFeature(const FString& PluginName)
{
	UGameFeaturesSubsystem* Subsystem = RevvyGameFeatureHelpers::GetSubsystem();
	if (!Subsystem)
	{
		UE_LOG(LogRevvyGF, Warning, TEXT("UnloadGameFeature: UGameFeaturesSubsystem unavailable."));
		return false;
	}

	FString URL;
	if (!RevvyGameFeatureHelpers::ResolvePluginURL(PluginName, URL))
	{
		UE_LOG(LogRevvyGF, Warning,
			TEXT("UnloadGameFeature: could not resolve URL for plugin '%s'."),
			*PluginName);
		return false;
	}

	Subsystem->UnloadGameFeaturePlugin(URL, FGameFeaturePluginUnloadComplete(), /*bKeepRegistered=*/false);
	UE_LOG(LogRevvyGF, Log,
		TEXT("UnloadGameFeature: unload requested for '%s' (URL=%s)."),
		*PluginName, *URL);
	return true;
}
