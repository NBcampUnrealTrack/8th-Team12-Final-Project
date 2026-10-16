// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableProp.h"
#include "MapActorBase.generated.h"

UCLASS()
class FEBRICULA_API AMapActorBase : public AInteractableProp
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMapActorBase();
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MapActorBase)
	bool bLocked;
};
