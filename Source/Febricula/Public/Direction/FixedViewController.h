// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FixedViewController.generated.h"

class UFixedViewComponent;

UCLASS()
class FEBRICULA_API AFixedViewController : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AFixedViewController();
	FName AddFixedCamera(UFixedViewComponent* CameraActor);

protected:
	UFUNCTION()
	void OnOverlapBegin(FName ComponentTag, bool bState);
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FixedViewController")
	TMap<FName, TObjectPtr<UFixedViewComponent>> FixedCameraActors;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FixedViewController")
	TObjectPtr<UFixedViewComponent> NowFixedCamera;
	
	UPROPERTY()
	USceneComponent* SceneComponent;
};

