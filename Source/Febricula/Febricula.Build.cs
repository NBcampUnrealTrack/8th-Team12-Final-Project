// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Febricula : ModuleRules
{
	public Febricula(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"DeveloperSettings",
			"GameplayTags", "CinematicCamera"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Febricula",
			"Febricula/Variant_Platforming",
			"Febricula/Variant_Platforming/Animation",
			"Febricula/Variant_Combat",
			"Febricula/Variant_Combat/AI",
			"Febricula/Variant_Combat/Animation",
			"Febricula/Variant_Combat/Gameplay",
			"Febricula/Variant_Combat/Interfaces",
			"Febricula/Variant_Combat/UI",
			"Febricula/Variant_SideScrolling",
			"Febricula/Variant_SideScrolling/AI",
			"Febricula/Variant_SideScrolling/Gameplay",
			"Febricula/Variant_SideScrolling/Interfaces",
			"Febricula/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
