// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapActorBase.generated.h"

UCLASS()
class FEBRICULA_API AMapActorBase : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMapActorBase();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MapActorBase)
	bool bLocked;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MapActorBase)
	bool bCanInteract;
};
