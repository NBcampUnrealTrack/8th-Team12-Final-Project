// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MoveState.generated.h"

/**
 * 
 */
UENUM(BlueprintType)
enum class EMoveState : uint8
{
	OPENING,
	CLOSING,
	NONE
};
