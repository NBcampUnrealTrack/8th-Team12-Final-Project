// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NPC/BaseNpcCharacter.h"
#include "NPCSetting.generated.h"

/**
 * 
 */
UCLASS(Config="Game")
class FEBRICULA_API UNPCSetting : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "NPC Character Map")
	TMap<FName, TSoftClassPtr<ABaseNpcCharacter>> NPCCharacterMap;
};
