// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMSearch : ModuleRules
{
	public SMSearch(ReadOnlyTargetRules Target) : base(Target)
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
				"Core",
				"SMSystem"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"CoreUObject",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"Projects",
				"EditorStyle",
				"EditorWidgets",
				"KismetWidgets",
				"GraphEditor",
				"BlueprintGraph",
				"WorkspaceMenuStructure",
				"Kismet",

				"SMSystemEditor",
				"SMExtendedRuntime",
				"SMUtilityLauncher",
				"SMAssetTools"
			}
		);
	}
}