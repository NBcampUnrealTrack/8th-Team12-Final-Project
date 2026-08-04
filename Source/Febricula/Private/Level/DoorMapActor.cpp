// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/DoorMapActor.h"

#include "TimerManager.h"
#include "Components/TimelineComponent.h"


// Sets default values
ADoorMapActor::ADoorMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	UTimelineComponent* OpenTimeline = CreateDefaultSubobject<UTimelineComponent>(FName("Open"));
}

// void ADoorMapActor::StartMove()
// {
// 	bNowMove = true;
// 	if (bNowMove)
// 	{
// 		switch (NowState)
// 		{
// 		case EMoveState::CLOSING:
// 			NowState = EMoveState::OPENING;
// 			break;
// 		case EMoveState::OPENING:
// 			NowState = EMoveState::CLOSING;
// 		}
// 	}
// 	else
// 	{
// 		NowState = EMoveState::OPENING;
// 	}
// 	GetWorldTimerManager().ClearTimer(MoveTimer);
// 	if (NowState == EMoveState::OPENING)
// 	{
// 		GetWorldTimerManager().SetTimer(MoveTimer, this, &ADoorMapActor::OpenActor, .1f, true);
// 	}
// 	else if (NowState == EMoveState::CLOSING)
// 	{
// 		GetWorldTimerManager().SetTimer(MoveTimer, this, &ADoorMapActor::CloseActor, .1f, true);
// 	}
// 	bNowMove = true;
// }

void ADoorMapActor::OpenActor()
{
	if (!bLocked)
	{
		for (int i = 0; i < Hinges.Num(); i++)
		{
			Hinges[i]->AddRelativeRotation(FRotator(0, MoveSpeed, 0));// AddLocalRotation(FRotator(0.0f, 1.0f, 0.0f));
			CurrentMove += MoveSpeed;
			if (CurrentMove >= MaxMovement)
			{
				GetWorldTimerManager().ClearTimer(MoveTimer);
				CurrentMove = MaxMovement;
				Hinges[i]->SetRelativeRotation(FRotator(Hinges[i]->GetRelativeRotation().Pitch, MinRotation[i] + MaxMovement, Hinges[i]->GetRelativeRotation().Roll));
				NowState = EMoveState::CLOSING;
				bNowMove = false;
			}
		}
	}
}

void ADoorMapActor::CloseActor()
{
	if (!bLocked)
	{
		for (int i = 0; i < Hinges.Num(); i++)
		{
			Hinges[i]->AddRelativeRotation(FRotator(0, -MoveSpeed, 0));// AddLocalRotation(FRotator(0.0f, 1.0f, 0.0f));
			CurrentMove -= MoveSpeed;
			if (CurrentMove <= 0)
			{
				GetWorldTimerManager().ClearTimer(MoveTimer);
				Hinges[i]->SetRelativeRotation(FRotator(Hinges[i]->GetRelativeRotation().Pitch, MinRotation[i], Hinges[i]->GetRelativeRotation().Roll));
				CurrentMove = 0;
				NowState = EMoveState::OPENING;
				bNowMove = false;
			}
		}
	}
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