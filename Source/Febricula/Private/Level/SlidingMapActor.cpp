// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/SlidingMapActor.h"

#include "Kismet/GameplayStatics.h"


// Sets default values
ASlidingMapActor::ASlidingMapActor()
{
}

// Called when the game starts or when spawned
void ASlidingMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (USceneComponent* Hinge: Hinges)
	{
		MinLocation.Add(Hinge->GetRelativeLocation());
		
	}	
}

void ASlidingMapActor::OpenActor()
{
	if (!bLocked)
	{
		UE_LOG(LogTemp, Warning, TEXT("%f : %f"), CurrentMove, MaxMovement);

		for (int i = 0; i < Hinges.Num(); i++)
		{
			switch (CurrentAxis)
			{
			case EMoveAxis::XAxis:
				Hinges[i]->AddRelativeLocation(FVector(MoveSpeed, 0, 0));
				break;
			case EMoveAxis::YAxis:
				Hinges[i]->AddRelativeLocation(FVector(0, MoveSpeed, 0));
				break;
			case EMoveAxis::ZAxis:
				Hinges[i]->AddRelativeLocation(FVector(0, 0, MoveSpeed));
				break;
			}
			CurrentMove += MoveSpeed;
			if (MaxMovement <= CurrentMove)
			{
				GetWorld()->GetTimerManager().ClearTimer(MoveTimer);
				CurrentMove = MaxMovement;
				switch (CurrentAxis)
				{
				case EMoveAxis::XAxis:
					Hinges[i]->SetRelativeLocation(FVector(MaxMovement, MinLocation[i].Y, MinLocation[i].Z));
					break;
				case EMoveAxis::YAxis:
					Hinges[i]->SetRelativeLocation(FVector(MinLocation[i].X, MaxMovement, MinLocation[i].Z));
					break;
				case EMoveAxis::ZAxis:
					Hinges[i]->SetRelativeLocation(FVector(MinLocation[i].X, MinLocation[i].Y, MaxMovement));
					break;
				}
				NowState = EMoveState::CLOSING;
				bNowMove = false;
				
			}
		}
	}
}

void ASlidingMapActor::CloseActor()
{
	if (!bLocked)
	{
		for (int i = 0; i < Hinges.Num(); i++)
		{
			switch (CurrentAxis)
			{
			case EMoveAxis::XAxis:
				Hinges[i]->AddRelativeLocation(FVector(-MoveSpeed, 0, 0));
				break;
			case EMoveAxis::YAxis:
				Hinges[i]->AddRelativeLocation(FVector(0, -MoveSpeed, 0));
				break;
			case EMoveAxis::ZAxis:
				Hinges[i]->AddRelativeLocation(FVector(0, 0, -MoveSpeed));
				break;
			}
			CurrentMove -= MoveSpeed;
			if (CurrentMove <= 0)
			{
				GetWorldTimerManager().ClearTimer(MoveTimer);
				Hinges[i]->SetRelativeLocation(MinLocation[i]);
				CurrentMove = 0;
				NowState = EMoveState::OPENING;
				bNowMove = false;
			}
		}
	}
}
void ASlidingMapActor::DoInteract_Implementation(AActor* Interactor)
{
	if (!Execute_CanInteract(this, Interactor)) { return;} 
	if (TargetLevel.IsNull()) { return; }
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, TargetLevel);
}

bool ASlidingMapActor::CanInteract_Implementation(AActor* Interactor) const
{
	return bCanInteract;
}

