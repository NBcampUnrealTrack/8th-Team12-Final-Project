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
			"GameplayTags", "CinematicCamera",
			// BurgerHouse — 손님이 앉을 자리로 순간이동할 때 내비메시에 투영한다
			// (ACustomerCharacter::TeleportToPoint). 안 하면 벽 안에서 시작해
			// 이후 MoveTo 가 전부 실패한다.
			"NavigationSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Febricula",
			// BurgerHouse — 손님 NPC 루프 (Import/BURGERHOUSE.md)
			"Febricula/BurgerHouse"
		});

	}
}
