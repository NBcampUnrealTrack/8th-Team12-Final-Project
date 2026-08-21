// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/InteractableActor.h"

#include "SNegativeActionButton.h"


// Sets default values
AInteractableActor::AInteractableActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AInteractableActor::BeginPlay()
{
	Super::BeginPlay();
	
}

void AInteractableActor::DoInteract_Implementation(AActor* Interactor)
{
	
}

bool AInteractableActor::CanInteract_Implementation(AActor* Interactor) const
{
	return bCanInteract;
}

// Called every frame