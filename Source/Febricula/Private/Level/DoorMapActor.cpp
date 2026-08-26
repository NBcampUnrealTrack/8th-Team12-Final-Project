// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/DoorMapActor.h"

#include "TimerManager.h"


// Sets default values
ADoorMapActor::ADoorMapActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
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

// Called when the game starts or when spawned
void ADoorMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge : Hinges)
	{
		MinRotation.Add(Hinge->GetRelativeRotation().Yaw);
	}
}