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

void ASlidingActor::OpenActor(AActor* Interactor, bool bInstant)
{
	Super::OpenActor(Interactor);
	const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();	
	if (!bInstant)
	{
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
			ExecuteActionsWrapper();
			//Super::Interact_Implementation(Interactor);
		}
	}
	else
	{
		CurrentMove = MaxMovement;
		for (int i = 0; i< Hinges.Num(); i++)
		{
			Hinges[i]->SetRelativeLocation(MinMovement[i].GetLocation() + Axis * MaxMovement);
		}
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);
		ExecuteActionsWrapper();
	}
}

// [skydodo] 2026-10-13 버그 수정: 'CurrentMove >= 0'은 항상 참이라 닫힘이 끝나지 않고 타이머가 영원히 돌았다(ExecuteActionsWrapper도 호출 안 됨)
void ASlidingActor::CloseActor(AActor* Interactor, bool bInstant)
{
	Super::CloseActor(Interactor);
	bool bEnd = true;
	const FVector Axis = AxisComponent->GetRelativeRotation().Vector().GetSafeNormal();	

	if (!bInstant)
	{
		// 0까지 다 닫힌 다음 틱에 종료한다 (>= 0이면 항상 참이라 타이머가 끝나지 않았다)
		if (CurrentMove > 0)
		{
			CurrentMove = FMath::Max(CurrentMove - MoveSpeed, 0);
			for (int i = 0; i < MinMovement.Num(); i++)
			{
				Hinges[i]->SetRelativeLocation(MinMovement[i].GetLocation() + Axis * CurrentMove);
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
	else
	{
		CurrentMove = 0.f;
		for (int i = 0; i < MinMovement.Num(); i++)
		{
			Hinges[i]->SetRelativeLocation(MinMovement[i].GetLocation());
		}
		bNowMove = false;
		GetWorldTimerManager().ClearTimer(MoveTimer);
		ExecuteActionsWrapper();
	}
}


// Called every frame

