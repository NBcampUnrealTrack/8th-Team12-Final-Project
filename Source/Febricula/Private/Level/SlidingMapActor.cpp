// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/SlidingMapActor.h"

#include "Kismet/GameplayStatics.h"


// Sets default values
ASlidingMapActor::ASlidingMapActor()
{
}

// Called when the game starts or when spawned
void ASlidingMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge: Hinges)
	{
		MinLocation.Add(Hinge->GetRelativeLocation());
		
	}	
}

