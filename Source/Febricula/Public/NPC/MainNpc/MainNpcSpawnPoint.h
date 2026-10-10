// [skydodo] 2026-10-08 작성
// 요약: 메인 NPC가 서 있을 자리 (스케줄 테이블이 SpawnPointId로 가리킨다)
// Fill out your copyright notice in the Description page of Project Settings.
//
// 메인 NPC가 서 있을 자리. 레벨에 배치하고 SpawnPointId만 붙여 두면 된다.
//
// 스케줄 테이블(FMainNpcSpawnRow)은 좌표 대신 이 ID를 가리킨다.
// 레벨에서 자리를 옮겨도 테이블은 손댈 필요가 없다.
// 액터의 위치는 NPC의 발 위치다 (바닥에 붙여 놓으면 된다). 화살표 방향을 바라보고 선다.

#pragma once

#include "CoreMinimal.h"
#include "SpawnPointType.h"
#include "GameFramework/Actor.h"
#include "MainNpcSpawnPoint.generated.h"

class UArrowComponent;
class UCapsuleComponent;

UCLASS()
class FEBRICULA_API AMainNpcSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AMainNpcSpawnPoint();

	UFUNCTION(BlueprintPure, Category = "MainNpc|SpawnPoint")
	FName GetSpawnPointId() const { return SpawnPointId; }
	
	UFUNCTION(BlueprintPure, Category = "MainNpc|SpawnPoint")
	ESpawnPointType GetSpawnPointType() const { return SpawnPointType; }

protected:
	/** 스케줄 테이블의 SpawnPointId와 맞출 이름 (예: Library_Desk). 한 레벨 안에서 겹치면 안 된다 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "MainNpc|SpawnPoint")
	FName SpawnPointId;

	/** 이지민 수정(2026, 10. 10)
	 * 여러 후보 중 아무데나 랜덤하게 스폰하게 만들기 위해 추가
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MainNpc|SpawnPoint")
	ESpawnPointType SpawnPointType;

#if WITH_EDITORONLY_DATA
	/** 에디터에서 NPC가 설 자리와 크기를 보여주는 용도. 게임에서는 없다 */
	UPROPERTY(VisibleAnywhere, Category = "MainNpc|SpawnPoint")
	TObjectPtr<UCapsuleComponent> PreviewCapsule;

	UPROPERTY(VisibleAnywhere, Category = "MainNpc|SpawnPoint")
	TObjectPtr<UArrowComponent> Arrow;
#endif
};
