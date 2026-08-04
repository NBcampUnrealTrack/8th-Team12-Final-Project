// Copyright Recursoft LLC. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SMExtendedEditor : ModuleRules
{
	public SMExtendedEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Public"),
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private"),
				Path.Combine("SMSystemEditor", "Private"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"CoreUObject",
				"UnrealEd",
				"RenderCore",
				"InputCore",
				"SlateCore",
				"Slate",
				"EditorStyle",
				"EditorWidgets",
				"ToolMenus",

				"DetailCustomizations",
				"PropertyEditor",

				"BlueprintGraph",
				"Kismet",
				"KismetWidgets",
				"GraphEditor",

				"ApplicationCore",
				"UMG",
				
				"SMSystem",
				"SMExtendedRuntime",
				"SMSystemEditor"
			}
		);
	}
}