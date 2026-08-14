// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MapActorBase.h"
#include "MovableMapActor.h"
#include "MoveState.h"
#include "DoorMapActor.generated.h"

UCLASS()
class FEBRICULA_API ADoorMapActor : public AMovableMapActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ADoorMapActor();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	void OpenActor() override;
	void CloseActor() override;
	
	virtual void DoInteract_Implementation(AActor* Interactor) override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	

	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	// bool bCanChangeLevel;
	// UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor", meta = (AllowedClasses="/Script/Engine.World"))
	// TSoftObjectPtr<UWorld> ConnectedLevel;
	EMoveState NowState;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<float> MinRotation;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Change")
	TSoftObjectPtr<UWorld> TargetLevel;

};

