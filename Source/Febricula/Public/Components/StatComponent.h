// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StatComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FEBRICULA_API UStatComponent : public UActorComponent
{
protected:
private:
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UStatComponent();
	
	bool SetStat(FName StatName, int32 NewValue);
	int32 GetStat(FName StatName);

protected:
	// Called when the game starts
	TMap<FName, int32> Stats;
	
	
public:
};
