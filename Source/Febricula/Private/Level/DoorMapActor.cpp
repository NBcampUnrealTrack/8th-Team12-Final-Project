// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/DoorMapActor.h"

#include "TimerManager.h"


// Sets default values
ADoorMapActor::ADoorMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

void ADoorMapActor::StartMove(AActor* Interactor)
{
 	bNowMove = true;
 	GetWorldTimerManager().ClearTimer(MoveTimer);
 	if (NowState == EMoveState::OPENING)
 	{
 		CurrentMove = 0.f;
 		FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ADoorMapActor::OpenActor, Interactor);

 		GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
 		NowState = EMoveState::CLOSING;
 	}
 	else if (NowState == EMoveState::CLOSING)
 	{
 		if (CurrentMove == 0)
 		{
 			CurrentMove = MaxMovement;
 		}
 		FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ADoorMapActor::CloseActor, Interactor);

 		GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
 		NowState = EMoveState::OPENING;
 	}
 	bNowMove = true;
 }

void ADoorMapActor::Interact_Implementation(AActor* Interactor)
{
	StartMove(Interactor);
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

void ADoorMapActor::OpenActor(AActor* Interactor)
{
	Super::OpenActor(Interactor);
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
		Super::Interact_Implementation(Interactor);
	}
}

void ADoorMapActor::CloseActor(AActor* Interactor)
{
	Super::CloseActor(Interactor);
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
