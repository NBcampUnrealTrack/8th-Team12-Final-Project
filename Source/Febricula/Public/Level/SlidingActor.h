// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovableMapActor.h"
#include "SlidingActor.generated.h"

UCLASS()
class FEBRICULA_API ASlidingActor : public AMovableMapActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASlidingActor();	


protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	virtual void OpenActor(AActor* Interactor, bool bInstant) override;
	virtual void CloseActor(AActor* Interactor, bool bInstant) override;
public:
};
