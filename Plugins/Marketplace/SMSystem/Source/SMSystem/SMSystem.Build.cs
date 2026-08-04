// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMSystem : ModuleRules
{
	public SMSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDefinitions.Add("LOGICDRIVER_IS_MARKETPLACE_BUILD=1");

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Public"),
				Path.Combine(ModuleDirectory, "Public", "Properties"),
				Path.Combine(ModuleDirectory, "Public", "Nodes"),
				Path.Combine(ModuleDirectory, "Public", "Nodes", "States"),
				Path.Combine(ModuleDirectory, "Public", "Nodes", "Transitions"),
				Path.Combine(ModuleDirectory, "Public", "Nodes", "Rules")
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags"
			}
		);

		// Editor specific modules for slate specific configuration of editor widgets.
		// Configuration values are stored on run-time struct for overall simplicity.
		if (Target.Type == TargetType.Editor)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"SlateCore",
					"PluginWarden"
				}
			);

			PublicIncludePathModuleNames.AddRange(
				new string[]
				{
					"SMPreviewEditor"
				}
			);
		}
	}
}