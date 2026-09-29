// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PlayerStat.generated.h"

UENUM(BlueprintType)
enum class EPlayerStat : uint8
{
	Health,
	Mental,
	Int,
	Luck,
	Courage,
	HandWork,
	Max UMETA(Hidden)
};