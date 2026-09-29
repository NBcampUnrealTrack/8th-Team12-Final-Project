// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "NpcAnimInstance.generated.h"

class ABaseNpcCharacter;
/**
 * 
 */
UCLASS()
class FEBRICULA_API UNpcAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
	
protected:
	virtual void NativeInitializeAnimation() override; 
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "AnimInstance")
	TObjectPtr<ABaseNpcCharacter> Character;
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "AnimInstance")
	TWeakObjectPtr<const AActor> LookAtTarget;
	
	
};
