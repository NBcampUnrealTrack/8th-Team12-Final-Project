// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovableMapActor.h"
#include "MoveState.h"
#include "Dialogue/DialogueDataTypes.h"
#include "DoorMapActor.generated.h"

UCLASS()
class FEBRICULA_API ADoorMapActor : public AMovableMapActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ADoorMapActor();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	

	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	// bool bCanChangeLevel;
	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor", meta = (AllowedClasses="/Script/Engine.World"))
	// TSoftObjectPtr<UWorld> ConnectedLevel;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<float> MinRotation;
	
};

