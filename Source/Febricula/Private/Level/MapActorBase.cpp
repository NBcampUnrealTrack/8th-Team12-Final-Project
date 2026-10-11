// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/MapActorBase.h"


// Sets default values
AMapActorBase::AMapActorBase()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAllowTickBeforeBeginPlay = false;
}

bool AMapActorBase::CanInteract_Implementation(AActor* Interactor) const
{
	return Super::CanInteract_Implementation(Interactor);
}

void AMapActorBase::Interact_Implementation(AActor* Interactor)
{
	Super::Interact_Implementation(Interactor);
}

FText AMapActorBase::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return Super::GetInteractionPrompt_Implementation(Interactor);
}

// Called when the game starts or when spawned
void AMapActorBase::BeginPlay()
{
	Super::BeginPlay();
	
}
