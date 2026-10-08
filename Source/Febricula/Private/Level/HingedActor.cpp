// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/HingedActor.h"

#include "TimerManager.h"
#include "Components/ArrowComponent.h"


// Sets default values
AHingedActor::AHingedActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

// Called when the game starts or when spawned
void AHingedActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge : Hinges)
	{
		MinMovement.Add(FTransform(Hinge->GetRelativeRotation()));
	}
}

void AHingedActor::OpenActor(AActor* Interactor, bool bInstant)
{
	Super::OpenActor(Interactor);
	const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();	
	if (MaxMovement > CurrentMove)
	{ 
		if (!bInstant)
		{
			CurrentMove = FMath::Min(CurrentMove + MoveSpeed, MaxMovement);
		}
		else
		{
			CurrentMove = MaxMovement;
		}
		const FQuat Delta(Axis, FMath::DegreesToRadians(CurrentMove));
		for (int i = 0; i < Hinges.Num(); i++)
		{
			Hinges[i]->SetRelativeRotation(MinMovement[i].GetRotation() * Delta);
			//Hinges[i]->AddRelativeRotation(FRotator(0, MoveSpeed, 0));
		}
	}
	else
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);
		ExecuteActionsWrapper();

		
		
		//Super::Interact_Implementation(Interactor);
	}
}

void AHingedActor::CloseActor(AActor* Interactor, bool bInstant)
{
	Super::CloseActor(Interactor);
	bool bEnd = true;
	if (CurrentMove >= 0)
	{
		const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();
		if (!bInstant)
		{
			CurrentMove = FMath::Max(CurrentMove - MoveSpeed, 0);
		}
		else
		{
			CurrentMove = 0;
		}
		for (int i = 0; i < MinMovement.Num(); i++)
		{
			const FQuat Delta(Axis, FMath::DegreesToRadians(CurrentMove));

			Hinges[i]->SetRelativeRotation(MinMovement[i].GetRotation() * Delta);
			bEnd = false;
		}
	}
	
	if (bEnd)
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);
		ExecuteActionsWrapper();

	}
}
