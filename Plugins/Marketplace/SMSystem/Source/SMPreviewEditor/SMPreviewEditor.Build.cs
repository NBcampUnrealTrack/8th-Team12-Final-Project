// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMPreviewEditor : ModuleRules
{
	public SMPreviewEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine("SMSystemEditor", "Private")
			}
		);

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
				"InputCore",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"EditorStyle",
				"ToolWidgets",
				"ToolMenus",

				"AdvancedPreviewScene",
				"SceneOutliner",
				"Kismet",
				"UMG",
				
				"SMSystemEditor"
			}
		);
	}
}