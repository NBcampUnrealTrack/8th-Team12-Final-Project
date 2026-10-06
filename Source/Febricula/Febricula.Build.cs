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
			"NavigationSystem",
			// 행인 이동 계산을 Mass 프로세서에서 병렬로 돌린다 (SubNpcMassProcessors).
			"MassEntity",
			"MassCore",
			// 멀리 있는 행인을 인스턴스드 메시 + 텍스처로 구운 애니메이션으로 그린다 (SubNpcMassProcessors).
			"AnimToTexture"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Febricula",
			// BurgerHouse — 손님 NPC 루프 (Import/BURGERHOUSE.md)
			"Febricula/BurgerHouse"
		});

	}
}
