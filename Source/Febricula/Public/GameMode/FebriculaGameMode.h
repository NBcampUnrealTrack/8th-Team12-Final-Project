// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dialogue/DialogueDataTypes.h"
#include "GameFramework/GameModeBase.h"
#include "FebriculaGameMode.generated.h"

class ABaseNpcCharacter;
class ALevelAutoEventSetting;
/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AFebriculaGameMode : public AGameModeBase
{
private:
	GENERATED_BODY()

public:
	
	/** Constructor */
	AFebriculaGameMode();
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	
	virtual void BeginPlay() override;
	
	TSoftClassPtr<ABaseNpcCharacter> GetNpcViaName(FName NowName);
	
	// UFUNCTION(BlueprintCallable)
	// [[nodiscard]] int32 Year() const
	// {
	// 	return year;
	// }
	//
	// UFUNCTION(BlueprintCallable)
	// void SetYear(int32 Year)
	// {
	// 	year = Year;
	// }

	
protected:
	void PlayEvent();
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UDataTable> AutoEventTable;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UDataTable> AutoDialogueTable;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TMap<FName, TSoftClassPtr<ABaseNpcCharacter>> NPCCharacterMap;
	
	FTimerHandle AutoEventTimer;
};



