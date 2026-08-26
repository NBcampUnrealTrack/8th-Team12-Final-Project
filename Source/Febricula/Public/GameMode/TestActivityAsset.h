// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActivityDefinition.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "TestActivityAsset.generated.h"

/**
 * 
 */

UCLASS()
class FEBRICULA_API UTestActivityAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Febricula | TestEvent")	
	TMap<FGameplayTag, FActivityDefinition> Activities; // 키는 액티비티 태그
};
