// Fill out your copyright notice in the Description page of Project Settings.
//
// 레벨에 배치해서 쓰는 순찰 경로 컨테이너.
// 사용법: 레벨에 TargetPoint(엔진 기본 제공) 같은 액터를 순서대로 배치한 뒤,
// 이 액터의 PatrolPoints 배열에 순서대로 드래그해서 넣으면 된다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NpcPatrolRoute.generated.h"

UCLASS()
class FEBRICULA_API ANpcPatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	ANpcPatrolRoute();

	UFUNCTION(BlueprintPure, Category = "NPC|Patrol")
	int32 GetNumPoints() const { return PatrolPoints.Num(); }

	/** Index가 배열 범위를 벗어나면 이 액터 자신의 위치를 반환 (안전한 폴백) */
	UFUNCTION(BlueprintPure, Category = "NPC|Patrol")
	FVector GetPointLocation(int32 Index) const;

	/** 항상 마지막 지점 다음엔 0번으로 순환한다 (왕복이 필요해지면 나중에 확장) */
	UFUNCTION(BlueprintPure, Category = "NPC|Patrol")
	int32 GetNextIndex(int32 CurrentIndex) const;

protected:
	/** 순서대로 방문할 지점들. 레벨에 배치한 액터(TargetPoint 등)를 순서대로 드래그해서 채운다 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPC|Patrol")
	TArray<TObjectPtr<AActor>> PatrolPoints;
};