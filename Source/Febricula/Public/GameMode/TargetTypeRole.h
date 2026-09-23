#pragma once
#include "CoreMinimal.h"
#include "TargetTypeRole.generated.h"

UENUM(BlueprintType)
enum class ETargetTypeRole : uint8
{
	Sound,	
	Teleport,
	Spawn,
	Camera,
	MoveGoal
};