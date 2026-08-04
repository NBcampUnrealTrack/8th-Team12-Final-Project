// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/MovableMapActor.h"

#include "TimerManager.h"
#include "Engine/World.h"


// Sets default values
AMovableMapActor::AMovableMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AMovableMapActor::StartMove()
{
	switch (NowState)
	{
	case EMoveState::NONE:
		NowState = EMoveState::OPENING;
		break;
	case EMoveState::CLOSING:
		NowState = EMoveState::OPENING;
		break;
	case EMoveState::OPENING:
		NowState = EMoveState::CLOSING;
		break;
	}
	GetWorldTimerManager().ClearTimer(MoveTimer);
	if (NowState == EMoveState::OPENING)
	{
		GetWorldTimerManager().SetTimer(MoveTimer, this, &AMovableMapActor::OpenActor, .1f, true);
	}
	else if (NowState == EMoveState::CLOSING)
	{
		GetWorldTimerManager().SetTimer(MoveTimer, this, &AMovableMapActor::CloseActor, .1f, true);
	}
	bNowMove = true;

}

// Called when the game starts or when spawned
void AMovableMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (FComponentReference Ref : HingeComponent)
	{
		if (USceneComponent* Hinge = Cast<USceneComponent>(Ref.GetComponent(this)))
		{
			Hinges.Add(Hinge);
		}
	}
}

// Called every frame

