// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMSystemEditor : ModuleRules
{
	public SMSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Public")
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
				"Projects",
				"UnrealEd",
				"InputCore",
				"SlateCore",
				"Slate",
				"EditorStyle",
				"EditorWidgets",
				"ToolMenus",
				"ToolWidgets",
				"AssetTools",
				"GameplayTags",

				"WorkspaceMenuStructure",
				"DetailCustomizations",
				"PropertyEditor",

				"BlueprintGraph",
				"Kismet",
				"KismetCompiler",
				"KismetWidgets",

				"GraphEditor",
				"ContentBrowser",

				"ApplicationCore",
				"AppFramework",
				"MainFrame",
				
				"EnhancedInput",
				"InputBlueprintNodes"
			}
		);
	}
}