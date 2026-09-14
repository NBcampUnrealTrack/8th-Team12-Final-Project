// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/DoorMapActor.h"

#include "TimerManager.h"


// Sets default values
ADoorMapActor::ADoorMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

void ADoorMapActor::StartMove()
{
 	bNowMove = true;
 	GetWorldTimerManager().ClearTimer(MoveTimer);
 	if (NowState == EMoveState::OPENING)
 	{
 		CurrentMove = 0.f;
 		GetWorldTimerManager().SetTimer(MoveTimer, this, &ADoorMapActor::OpenActor, .1f, true);
 		NowState = EMoveState::CLOSING;
 	}
 	else if (NowState == EMoveState::CLOSING)
 	{
 		if (CurrentMove == 0)
 		{
 			CurrentMove = MaxMovement;
 		}
 		GetWorldTimerManager().SetTimer(MoveTimer, this, &ADoorMapActor::CloseActor, .1f, true);
 		NowState = EMoveState::OPENING;
 	}
 	bNowMove = true;
 }

void ADoorMapActor::Interact_Implementation(AActor* Interactor)
{
	StartMove();
	//Super::Interact_Implementation(Interactor);
}

// Called when the game starts or when spawned
void ADoorMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge : Hinges)
	{
		MinRotation.Add(Hinge->GetRelativeRotation().Yaw);
	}
}

void ADoorMapActor::OpenActor()
{
	Super::OpenActor();
	if (MaxMovement > CurrentMove)
	{
		CurrentMove += MoveSpeed;
		for (USceneComponent* Hinge : Hinges)
		{
			Hinge->AddRelativeRotation(FRotator(0, MoveSpeed, 0));
		}
	}
	else
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);

	}
}

void ADoorMapActor::CloseActor()
{
	Super::CloseActor();
	bool bEnd = true;
	CurrentMove -= MoveSpeed;
	for (int i = 0; i < MinRotation.Num(); i++)
	{
		if (CurrentMove >= 0)
		{
			Hinges[i]->AddRelativeRotation(FRotator(0, -MoveSpeed, 0));
			bEnd = false;
		}
	}
	if (bEnd)
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);

	}
}
