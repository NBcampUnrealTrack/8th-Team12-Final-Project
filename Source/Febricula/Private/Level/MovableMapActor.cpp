// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/MovableMapActor.h"

#include "TimerManager.h"
#include "Engine/World.h"


// Sets default values
AMovableMapActor::AMovableMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

void AMovableMapActor::StartMove()
{
	if (NowState == EMoveState::NONE)
	{
		NowState = EMoveState::OPENING;
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

