// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovableMapActor.h"
#include "MoveState.h"
#include "Dialogue/DialogueDataTypes.h"
#include "Interaction/InteractableProp.h"
#include "HingedActor.generated.h"

UCLASS()
class FEBRICULA_API AHingedActor : public AMovableMapActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHingedActor();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void OpenActor(AActor* Interactor, bool bInstant) override;
	virtual void CloseActor(AActor* Interactor, bool bInstant) override;
	
	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	// bool bCanChangeLevel;
	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor", meta = (AllowedClasses="/Script/Engine.World"))
	// TSoftObjectPtr<UWorld> ConnectedLevel;
		
	
};

