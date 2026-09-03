// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Dialogue/DialogueDataTypes.h"
#include "GameFramework/WorldSettings.h"
#include "LevelAutoEventSetting.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API ALevelAutoEventSetting : public AWorldSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UDataTable> AutoEventRow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UDataTable> AutoDialogueRow;
};
