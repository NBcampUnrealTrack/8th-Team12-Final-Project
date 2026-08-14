// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class FEBRICULA_API IInteractableInterface
{
	GENERATED_BODY()
	
protected:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = Interactable)
	void DoInteract(AActor* Instigator);
	virtual void DoInteract_Implementation(AActor* Instigator) {};
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = Interactable)
	FText GetInteractionText(AActor* Instigator) const;
	virtual FText GetInteractionText_Implementation(AActor* Instigator) const { return FText::GetEmpty(); };
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = Interactable)
	bool CanInteract(AActor* Instigator) const;
	virtual bool CanInteract_Implementation(AActor* Instigator) const { return true; };
	
	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
};
