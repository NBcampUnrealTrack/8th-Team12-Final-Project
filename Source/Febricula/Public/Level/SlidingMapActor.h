// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovableMapActor.h"
#include "GameFramework/Actor.h"
#include "SlidingMapActor.generated.h"

UCLASS()
class FEBRICULA_API ASlidingMapActor : public AMovableMapActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASlidingMapActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sliding Door")
	EMoveAxis CurrentAxis;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<FVector> MinLocation;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Change")
	TSoftObjectPtr<UWorld> TargetLevel;
};
