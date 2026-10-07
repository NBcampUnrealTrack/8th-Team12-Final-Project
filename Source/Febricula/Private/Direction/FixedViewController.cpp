// Fill out your copyright notice in the Description page of Project Settings.


#include "Direction/FixedViewController.h"


// Sets default values
AFixedViewController::AFixedViewController()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AFixedViewController::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AFixedViewController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

