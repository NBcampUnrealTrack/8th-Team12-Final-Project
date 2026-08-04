// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMAssetTools : ModuleRules
{
	public SMAssetTools(ReadOnlyTargetRules Target) : base(Target)
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
				"SMSystem",
				"SMSystemEditor"
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
				"EditorStyle",
				"EditorWidgets",
				"AssetTools",
				"GraphEditor",
				"BlueprintGraph",
				"WorkspaceMenuStructure",
				"DesktopPlatform",
				"Json",
				"JsonUtilities",
				"ContentBrowserData"
			}
		);
	}
}