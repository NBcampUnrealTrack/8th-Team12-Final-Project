// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

struct FSMSystemInfo
{
	FText OSVersion;
	FString EngineVersion;
	FString PluginVersion;

	bool bIsMarketplace = false;
	bool bIsAuthenticated = false;

	FString GetBuildType() const;
	FString ToString() const;
};
