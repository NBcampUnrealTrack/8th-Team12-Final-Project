// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MovableMapActor.h"
#include "GameFramework/Actor.h"
#include "SlidingMapActor.generated.h"

UCLASS()
class FEBRICULA_API ASlidingMapActor : public AMovableMapActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASlidingMapActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void OpenActor() override;
	virtual void CloseActor() override;
	
	virtual void DoInteract_Implementation(AActor* Interactor) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sliding Door")
	EMoveAxis CurrentAxis;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<FVector> MinLocation;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Change")
	TSoftObjectPtr<UWorld> TargetLevel;
};
