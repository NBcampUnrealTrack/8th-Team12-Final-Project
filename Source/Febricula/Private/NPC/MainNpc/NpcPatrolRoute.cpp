#include "NPC/MainNpc/NpcPatrolRoute.h"

ANpcPatrolRoute::ANpcPatrolRoute()
{
	PrimaryActorTick.bCanEverTick = false;

	// 이 액터 자체는 순찰 지점 목록을 들고 있을 뿐, 씬에 렌더링되는 게 없어도 된다.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

FVector ANpcPatrolRoute::GetPointLocation(int32 Index) const
{
	if (PatrolPoints.IsValidIndex(Index) && PatrolPoints[Index])
	{
		return PatrolPoints[Index]->GetActorLocation();
	}
	return GetActorLocation();
}

int32 ANpcPatrolRoute::GetNextIndex(int32 CurrentIndex) const
{
	const int32 Num = PatrolPoints.Num();
	if (Num <= 0)
	{
		return 0;
	}
	return (CurrentIndex + 1) % Num;
}