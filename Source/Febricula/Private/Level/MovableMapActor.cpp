// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/Level/MovableMapActor.h"

#include "TimerManager.h"
#include "Components/ArrowComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AMovableMapActor::AMovableMapActor()
{
	RootComponent = SceneComponent;
	AxisComponent = CreateDefaultSubobject<UArrowComponent>("AxisComponent");
	AxisComponent->SetupAttachment(RootComponent);
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
}

void AMovableMapActor::StartMove(AActor* Interactor, bool bInstant)
{
	UE_LOG(LogTemp, Warning, TEXT("AMovableMapActor::StartMove() %d"), bInstant);
	bNowMove = true;
	GetWorldTimerManager().ClearTimer(MoveTimer);
	if (NowState == EMoveState::OPENING)
	{
		CurrentMove = 0.f;
		FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &AMovableMapActor::OpenActor, Interactor, bInstant);

		GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
		NowState = EMoveState::CLOSING;
	}
	else if (NowState == EMoveState::CLOSING)
	{
		if (CurrentMove == 0)
		{
			CurrentMove = MaxMovement;
		}
		FTimerDelegate Del = FTimerDelegate::CreateUObject(this, &AMovableMapActor::CloseActor, Interactor, bInstant);

		GetWorldTimerManager().SetTimer(MoveTimer, Del, .1f, true);
		NowState = EMoveState::OPENING;
	}
	bNowMove = true;

}

void AMovableMapActor::OpenActor(AActor* Interactor, bool bInstant)
{
	bInteracted = true;
}

void AMovableMapActor::CloseActor(AActor* Interactor, bool bInstant)
{
	bInteracted = false;
}

void AMovableMapActor::FinishDialogue()
{
	if (bUsed)
	{
		StartMove(UGameplayStatics::GetPlayerCharacter(this, 0), false);
	}
}

void AMovableMapActor::StartMoveWrapper(AActor* Interactor, EMoveState MoveState, bool bInstant)
{
	NowState = MoveState;
	StartMove(Interactor, bInstant);
}

// Called every frame


void AMovableMapActor::Interact_Implementation(AActor* Interactor)
{
	Super::Interact_Implementation(Interactor);
	//StartMove(Interactor, false);
	//Super::Interact_Implementation(Interactor);
}

// Called when the game starts or when spawned
void AMovableMapActor::BeginPlay()
{
	Super::BeginPlay();
	for (FComponentReference Ref : HingeComponent)
	{
		if (USceneComponent* Hinge = Cast<USceneComponent>(Ref.GetComponent(this)))
		{
			Hinges.Add(Hinge);
		}
	}
	if (UDialogueSubsystem* DialogueSubsystem = GetWorld()->GetSubsystem<UDialogueSubsystem>())
	{
		DialogueSubsystem->OnDialogueFinished.AddDynamic(this, &AMovableMapActor::FinishDialogue);
	}
}
// Called every frame

