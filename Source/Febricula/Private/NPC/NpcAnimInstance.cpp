// Fill out your copyright notice in the Description page of Project Settings.


#include "NPC/NpcAnimInstance.h"

#include "NPC/MainNpc/MainNpcCharacter.h"

void UNpcAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (APawn* Pawn = TryGetPawnOwner())
	{
		Character = Cast<ABaseNpcCharacter>(Pawn);
	}
	
	
}

void UNpcAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (Character)
	{
		if (const AActor* Target = Character->GetLookAtActor())
		{
			
		}
	}
}
