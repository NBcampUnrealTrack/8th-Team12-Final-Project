// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/HISMActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"


// Sets default values
AHISMActor::AHISMActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	HierarchyStaticMeshComponent = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(FName("HierarchyStaticMeshComponent"));
	HierarchyStaticMeshComponent->SetupAttachment(RootComponent);
}

// Called when the game starts or when spawned
void AHISMActor::BeginPlay()
{
	Super::BeginPlay();
	
}

