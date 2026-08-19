// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NPC/BaseNpcCharacter.h"
#include "SubNpcCharacter.generated.h"

/**
 * 스토리/호감도 없이 이벤트 발생을 돕는 서브 NPC (교장, 학생부장, 담당 선생님 등).
 * 부모(ABaseNpcCharacter)의 Idle/Walk/Interact만으로 충분해서 지금은 추가 로직이 없다.
 * IsMainNpc()는 부모의 기본값(false)을 그대로 쓴다.
 *
 * 서브 NPC 전용 기능(예: 정해진 대사만 나오는 캔드 인터랙션, 고정 스폰 포인트 룩업 등)이
 * 생기면 여기에 추가한다. 지금 당장 비어 있는 게 정상이다 - AMainNpcCharacter처럼
 * 억지로 기능을 채우면 다시 "베이스에 힘이 들어간" 문제가 재발한다.
 */
UCLASS()
class FEBRICULA_API ASubNpcCharacter : public ABaseNpcCharacter
{
	GENERATED_BODY()
};
