// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ActivitySetting.generated.h"

class UTestActivityAsset;
/**
 * 
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Activity Setting"))
class FEBRICULA_API UActivitySetting : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Event Default")
	TSoftObjectPtr<UTestActivityAsset> FixedEventAsset;
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category="Event Default")
	TMap<FName, TSoftObjectPtr<UWorld>> Levels;

};
