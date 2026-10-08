// [skydodo] 2026-08-18 작성
// 요약: NPC 이동 경로 구현 - 지점 위치, 다음 인덱스 계산
// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/NpcRoute.h"

ANpcRoute::ANpcRoute()
{
	PrimaryActorTick.bCanEverTick = false;

	// 이 액터 자체는 경로 지점 목록을 들고 있을 뿐, 씬에 렌더링되는 게 없어도 된다.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FVector ANpcRoute::GetPointLocation(int32 Index) const
{
	if (RoutePoints.IsValidIndex(Index) && RoutePoints[Index])
	{
		return RoutePoints[Index]->GetActorLocation();
	}
	return GetActorLocation();
}

FRotator ANpcRoute::GetPointRotation(int32 Index) const
{
	if (RoutePoints.IsValidIndex(Index) && RoutePoints[Index])
	{
		FRotator Temp = RoutePoints[Index]->GetActorRotation();
		return Temp;
	}
	return GetActorRotation();
}

int32 ANpcRoute::GetNextIndex(int32 CurrentIndex) const
{
	const int32 Num = RoutePoints.Num();
	if (Num <= 0)
	{
		return 0;
	}
	return (CurrentIndex + 1) % Num;
}
