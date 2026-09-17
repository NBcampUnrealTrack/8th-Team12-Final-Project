// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CrowdSequence.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "NpcCrowdManager.generated.h"

class ANpcCrowdPath;

UCLASS()
class FEBRICULA_API ANpcCrowdManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ANpcCrowdManager();
	void Play(FName Tag);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd Manager")
	TMap<FName, FCrowdSequence> CrowdSequence;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd Manager")
	TArray<TObjectPtr<ASubNpcCharacter>> SpawnedCharacters;

public:
};
