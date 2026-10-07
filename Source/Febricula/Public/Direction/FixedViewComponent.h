// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FixedViewComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnOverlapBegin, FName, ComponentTag, bool, State);

class ATriggerBox;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FEBRICULA_API UFixedViewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UFixedViewComponent();
	
	bool HasTriggerBox();
	
	float GetYaw() { return CameraYaw; };
	
	FOnOverlapBegin OnOverlapBegin;

protected:
	UFUNCTION()
	void HandleOverlapBegin(AActor* OverlappedActor, AActor* OtherActor);
	UFUNCTION()
	void HandleOverlapEnd(AActor* OverlappedActor, AActor* OtherActor);
	// Called when the game starts
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedViewComonent")
	float CameraYaw;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedViewComonent | Collision")
	TObjectPtr<ATriggerBox> ConnectedTriggerBox;

};
