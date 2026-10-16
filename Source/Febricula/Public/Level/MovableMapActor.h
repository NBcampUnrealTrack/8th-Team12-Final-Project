// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MapActorBase.h"
#include "MoveState.h"
#include "Interaction/InteractionBranch.h"
#include "MovableMapActor.generated.h"

class UArrowComponent;

UCLASS()
class FEBRICULA_API AMovableMapActor : public AInteractableProp
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMovableMapActor();
	virtual void StartMove(AActor* Interactor, bool bInstant = false);
	virtual void StartMoveWrapper(AActor* Interactor, EMoveState MoveState = EMoveState::OPENING, bool bInstant = false);
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	virtual void OpenActor(AActor* Interactor, bool bInstant = false);
	virtual void CloseActor(AActor* Interactor, bool bInstant = false);

	UFUNCTION()
	void FinishDialogue();
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	virtual void InitializeMovement(){};
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor", meta=(UseComponentPicker, AllowedClasses="/Script/Engine.SceneComponent"))
	TArray<FComponentReference> HingeComponent;
	UPROPERTY(Editanywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	float MaxMovement;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<FTransform> MinMovement;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	float MoveSpeed;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor")
	TObjectPtr<UArrowComponent> AxisComponent;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Activity")
	TArray<FInteractionBranch> Branches;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Type")
	bool bLogic = true;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	TArray<USceneComponent*> Hinges;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	bool bNowMove;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	EMoveState NowState;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	float CurrentMove;
	
	UPROPERTY(VisibleAnywhere , BlueprintReadOnly, Category = "Openable Map Actor | Debug")
	bool bInteracted;
	

	FTimerHandle MoveTimer;
};
