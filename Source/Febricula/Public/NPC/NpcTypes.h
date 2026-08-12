// Fill out your copyright notice in the Description page of Project Settings.
//
// 프로젝트 내 모든 NPC(메인/서브)가 공유하는 공용 열거형 모음.
// Main/Sub 구분은 클래스 상속(ABaseNpcCharacter -> AMainNpcCharacter / ASubNpcCharacter)으로 하므로
// 여기엔 별도의 "카테고리" enum을 두지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "NpcTypes.generated.h"

UENUM(BlueprintType)
enum class ENpcMovementState : uint8
{
	Idle			UMETA(DisplayName = "Idle"),
	Walk			UMETA(DisplayName = "Walk"),
	Follow			UMETA(DisplayName = "Follow (Main Only)"),
	Talk			UMETA(DisplayName = "Talk (Main Only)"),
	Interacting		UMETA(DisplayName = "Interacting"),
};

UENUM(BlueprintType)
enum class ENpcRelationshipLevel : uint8
{
	Wary		UMETA(DisplayName = "경계"),
	Neutral		UMETA(DisplayName = "평범"),
	Favor		UMETA(DisplayName = "호의"),
	Trust		UMETA(DisplayName = "신뢰"),
	Intimate	UMETA(DisplayName = "친밀"),
};

UENUM(BlueprintType)
enum class ENpcSpawnMode : uint8
{
	FixedInLevel		UMETA(DisplayName = "Level Fixed Spawn"),
	RandomEncounter		UMETA(DisplayName = "Random Encounter"),
};
