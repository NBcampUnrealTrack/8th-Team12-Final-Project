// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/PlayerStart.h"
#include "FebriculaPlayerStart.generated.h"

UCLASS()
class FEBRICULA_API AFebriculaPlayerStart : public APlayerStart
{
	GENERATED_BODY()

public:
	AFebriculaPlayerStart(const FObjectInitializer& ObjectInitializer);
	// Sets default values for this actor's properties
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "APlayerStart | Tag")
	FGameplayTag SpawnPointTag;
};
