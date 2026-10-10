#pragma once
#include "SpawnpointType.generated.h"

UENUM(BlueprintType)
enum class ESpawnPointType : uint8
{
	OneByOne UMETA(DisplayName = "이벤트 하나와 대응. 기본값"),
	OneByMulti UMETA(DisplayName = "이벤트 여러개와 대응"),
	MultiByRandom UMETA(DisplayName = "여러개의의 이벤트와 랜덤 포인트 대응")
};

