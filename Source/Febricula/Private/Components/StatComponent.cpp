// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StatComponent.h"


// Sets default values for this component's properties
UStatComponent::UStatComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

bool UStatComponent::SetStat(FName StatName, int32 NewValue)
{
	if (int32* Found = Stats.Find(StatName))
	{
		*Found = NewValue;
		return true;
	}
	return false;
}

int32 UStatComponent::GetStat(FName StatName)
{
	return Stats.FindRef(StatName);	
}


