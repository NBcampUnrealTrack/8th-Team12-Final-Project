// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InteractableInterface.h"
#include "MapActorBase.h"
#include "InteractableActor.generated.h"

UCLASS()
class FEBRICULA_API AInteractableActor : public AMapActorBase, public IInteractableInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AInteractableActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void DoInteract_Implementation(AActor* Interactor) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = MapActorBase)
	bool bCanInteract;

};
