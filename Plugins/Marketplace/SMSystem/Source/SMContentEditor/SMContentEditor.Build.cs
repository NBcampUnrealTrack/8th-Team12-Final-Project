// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMContentEditor : ModuleRules
{
	public SMContentEditor(ReadOnlyTargetRules Target) : base(Target)
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
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"CoreUObject",
				"Projects",
				"InputCore",
				"AppFramework",
				"MainFrame",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"EditorStyle",
				"EditorWidgets",
				"ToolWidgets",
				"AssetTools",
				"ContentBrowserData",
				"PakFileUtilities",
				"EnhancedInput",
				"GameplayTags",
				"GameplayTagsEditor",
				
				"SMSystemEditor",
				"SMUtilityLauncher"
			}
		);
	}
}