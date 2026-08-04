// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMUtilityLauncher : ModuleRules
{
	public SMUtilityLauncher(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private"),
				Path.Combine("SMSystemEditor", "Private")
			});

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"CoreUObject",
				"ApplicationCore",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"EditorStyle",
				"ToolWidgets",
				
				"SMSystem",
				"SMSystemEditor"
			}
		);
	}
}