// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/SlidingActor.h"

#include "Components/ArrowComponent.h"


// Sets default values
ASlidingActor::ASlidingActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

// Called when the game starts or when spawned
void ASlidingActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge : Hinges)
	{
		MinMovement.Add(FTransform(Hinge->GetRelativeLocation()));
	}
}

void ASlidingActor::StartMove(AActor* Interactor)
{
	Super::StartMove(Interactor);
	// bNowMove = true;
	// GetWorldTimerManager().ClearTimer(MoveTimer);
	// if (NowState == EMoveState::OPENING)
	// {
	// 	CurrentMove = 0.f;
	// 	FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ASlidingActor::OpenActor, Interactor);
	//
	// 	GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
	// 	NowState = EMoveState::CLOSING;
	// }
	// else if (NowState == EMoveState::CLOSING)
	// {
	// 	if (CurrentMove == 0)
	// 	{
	// 		CurrentMove = MaxMovement;
	// 	}
	// 	FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &ASlidingActor::CloseActor, Interactor);
	//
	// 	GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
	// 	NowState = EMoveState::OPENING;
	// }
	// bNowMove = true;

}

void ASlidingActor::OpenActor(AActor* Interactor)
{
	//Super::OpenActor(Interactor);
	const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();	

	if (MaxMovement > CurrentMove)
	{ 
		UE_LOG(LogTemp, Warning, TEXT("Current Move: %f"), CurrentMove);
		CurrentMove = FMath::Min(CurrentMove + MoveSpeed, MaxMovement);
		for (int i = 0; i < Hinges.Num(); i++)
		{
			Hinges[i]->SetRelativeLocation(MinMovement[i].GetLocation() + Axis * CurrentMove);
			//Hinges[i]->AddRelativeRotation(FRotator(0, MoveSpeed, 0));
		}
	}
	else
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);
		//Super::Interact_Implementation(Interactor);
	}
}

void ASlidingActor::CloseActor(AActor* Interactor)
{
	//Super::CloseActor(Interactor);
	bool bEnd = true;
	const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();	
	CurrentMove = FMath::Max(CurrentMove - MoveSpeed, 0);
	for (int i = 0; i < MinMovement.Num(); i++)
	{
		if (CurrentMove >= 0)
		{
			Hinges[i]->SetRelativeLocation(MinMovement[i].GetLocation() + Axis * CurrentMove);
			bEnd = false;
		}
	}
	if (bEnd)
	{
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);

	}
}


// Called every frame

