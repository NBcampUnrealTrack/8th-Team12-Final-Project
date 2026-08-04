// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/MapActorBase.h"


// Sets default values
AMapActorBase::AMapActorBase()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAllowTickBeforeBeginPlay = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(FName("Root"));
	
}

// Called when the game starts or when spawned
void AMapActorBase::BeginPlay()
{
	Super::BeginPlay();
	
}
