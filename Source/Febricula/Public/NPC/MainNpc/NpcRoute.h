// [skydodo] 2026-08-18 작성
// 요약: 레벨에 배치하는 NPC 이동 경로 (순찰/안내 공용)
// Fill out your copyright notice in the Description page of Project Settings.
//
// 레벨에 배치해서 쓰는 NPC 이동 경로.
// 순찰(Patrol)과 안내(Guide) 양쪽이 같은 경로 액터를 공유한다.
//   - Patrol : 지점에 도착하면 바로 다음 지점으로
//   - Guide  : 지점에 도착하면 플레이어를 기다렸다가 다음 지점으로
// 경로 데이터 자체는 둘이 다를 이유가 없어서 "PatrolRoute"가 아니라 중립적인 "Route"로 둔다.
//
// 사용법: 레벨에 TargetPoint(엔진 기본 제공) 같은 액터를 순서대로 배치한 뒤,
// 이 액터의 RoutePoints 배열에 순서대로 드래그해서 넣으면 된다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NpcRoute.generated.h"

UCLASS()
class FEBRICULA_API ANpcRoute : public AActor
{
	GENERATED_BODY()

public:
	ANpcRoute();

	UFUNCTION(BlueprintPure, Category = "NPC|Route")
	int32 GetNumPoints() const { return RoutePoints.Num(); }

	/** Index가 배열 범위를 벗어나면 이 액터 자신의 위치를 반환 (안전한 폴백) */
	UFUNCTION(BlueprintPure, Category = "NPC|Route")
	FVector GetPointLocation(int32 Index) const;

	/** 항상 마지막 지점 다음엔 0번으로 순환한다 (왕복이 필요해지면 나중에 확장) */
	UFUNCTION(BlueprintPure, Category = "NPC|Route")
	int32 GetNextIndex(int32 CurrentIndex) const;

protected:
	/** 순서대로 방문할 지점들. 레벨에 배치한 액터(TargetPoint 등)를 순서대로 드래그해서 채운다 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPC|Route")
	TArray<TObjectPtr<AActor>> RoutePoints;
};
