// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MapActorBase.h"
#include "HISMActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

UCLASS()
class FEBRICULA_API AHISMActor : public AMapActorBase
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AHISMActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UHierarchicalInstancedStaticMeshComponent* HierarchyStaticMeshComponent;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 HorizontalCount;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 VerticalCount;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HorizontalGap;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float VerticalGap;
};
