// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Dialogue/DialogueDataTypes.h"
#include "GameFramework/WorldSettings.h"
#include "LevelSetting.generated.h"

/**
 * 
 */
UCLASS()
class FEBRICULA_API ALevelSetting : public AWorldSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Setting | Event")
	TObjectPtr<UDataTable> AutoEventRow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Setting | Event")
	TObjectPtr<UDataTable> AutoDialogueRow;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Setting | Activity")
	TSoftClassPtr<UUserWidget> ActivityWidget;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Level Setting | FixedCamera")
	bool bUseFixedCamera;
	
	
	
};
