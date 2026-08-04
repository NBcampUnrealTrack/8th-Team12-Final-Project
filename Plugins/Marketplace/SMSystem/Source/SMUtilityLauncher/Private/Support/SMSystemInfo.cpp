// Copyright Recursoft LLC. All Rights Reserved.

#include "SMSystemInfo.h"

FString FSMSystemInfo::GetBuildType() const
{
	return bIsMarketplace ? TEXT("Marketplace") : TEXT("Other");
}

FString FSMSystemInfo::ToString() const
{
	return FString::Printf(
		TEXT("Platform: %s\nEngine: %s\nPlugin: %s\nBuild: %s"),
		*OSVersion.ToString(), *EngineVersion, *PluginVersion, *GetBuildType());
}
