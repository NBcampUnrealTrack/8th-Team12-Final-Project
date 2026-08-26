// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FebriculaGameMode.generated.h"

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
	
};



