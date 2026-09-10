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
	return;
}

void AMovableMapActor::OpenActor()
{
	return;
}

void AMovableMapActor::CloseActor()
{
	return;
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

