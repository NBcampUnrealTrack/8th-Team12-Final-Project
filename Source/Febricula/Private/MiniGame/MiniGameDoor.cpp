// [skydodo] 2026-10-10 작성
// 요약: 미니게임 문 구현
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/MiniGameDoor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MiniGame/MiniGameSubsystem.h"
#include "Progress/GameProgressSubsystem.h"

AMiniGameDoor::AMiniGameDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(60.f, 60.f, 100.f));
	// InteractionComponent는 모든 오브젝트 타입으로 오버랩 쿼리를 한다. 쿼리만 켜 두면 잡힌다.
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetHiddenInGame(false);
	Box->ShapeColor = FColor(255, 170, 90);
	SetRootComponent(Box);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	LabelText->SetupAttachment(Box);
	LabelText->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	LabelText->SetHorizontalAlignment(EHTA_Center);
	LabelText->SetWorldSize(32.f);
	LabelText->SetTextRenderColor(FColor(255, 170, 90));

	Prompt = NSLOCTEXT("MiniGame", "DoorPrompt", "미니게임 하러 가기");
}

void AMiniGameDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	FText Shown = Label.IsEmpty() ? Prompt : Label;
	if (TargetLevel.IsNull())
	{
		Shown = FText::FromString(Shown.ToString() + TEXT(" (TargetLevel 없음)"));
	}
	LabelText->SetText(Shown);
}

bool AMiniGameDoor::CanInteract_Implementation(AActor* Interactor) const
{
	if (UGameProgressSubsystem* GameProgressSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UGameProgressSubsystem>())
	{
		if (GameProgressSubsystem->EvaluateConditions(InteractConditions))
		{
			return !TargetLevel.IsNull() && !bTraveling;		
		}
	}
	return false;
}

void AMiniGameDoor::Interact_Implementation(AActor* Interactor)
{
	if (TargetLevel.IsNull() || bTraveling)
	{
		return;
	}
	bTraveling = true;

	// 돌아오면 PlayerStart가 아니라 지금 서 있는 자리로 (UMiniGameSubsystem이 되돌린다)
	const APawn* Pawn = Cast<APawn>(Interactor);
	if (!Pawn)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		Pawn = PC ? PC->GetPawn() : nullptr;
	}
	UGameInstance* GI = GetGameInstance();
	UMiniGameTravelSubsystem* Travel = GI ? GI->GetSubsystem<UMiniGameTravelSubsystem>() : nullptr;
	if (Pawn && Travel && GetWorld())
	{
		Travel->SetReturnPoint(*GetWorld(), Pawn->GetActorTransform());
		//Travel->TryStart();
	}
	if (UMiniGameSubsystem* GameSubsystem = GetWorld()->GetSubsystem<UMiniGameSubsystem>())
	{
		GameSubsystem->TryStart();
	}
	//UGameplayStatics::OpenLevelBySoftObjectPtr(this, TargetLevel);
}

FText AMiniGameDoor::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return Prompt;
}
