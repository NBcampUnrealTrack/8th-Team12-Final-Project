// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MapActorBase.h"
#include "MoveState.h"
#include "Interaction/InteractionBranch.h"
#include "MovableMapActor.generated.h"

UCLASS()
class FEBRICULA_API AMovableMapActor : public AInteractableProp
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMovableMapActor();

protected:
	
	virtual void StartMove();
	virtual void OpenActor();
	virtual void CloseActor();

	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor", meta=(UseComponentPicker, AllowedClasses="/Script/Engine.SceneComponent"))
	TArray<FComponentReference> HingeComponent;
	UPROPERTY(Editanywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	float MaxMovement;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	float MoveSpeed;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Activity")
	TArray<FInteractionBranch> Branches;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<USceneComponent*> Hinges;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	bool bNowMove;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	EMoveState NowState;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	float CurrentMove;
	
	FTimerHandle MoveTimer;
};
