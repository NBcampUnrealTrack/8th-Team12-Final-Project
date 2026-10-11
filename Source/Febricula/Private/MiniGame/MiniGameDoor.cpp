// [skydodo] 2026-10-10 작성
// 요약: 미니게임 문 구현
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/MiniGameDoor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"

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
	return !TargetLevel.IsNull() && !bTraveling;
}

void AMiniGameDoor::Interact_Implementation(AActor* Interactor)
{
	if (TargetLevel.IsNull() || bTraveling)
	{
		return;
	}
	bTraveling = true;
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, TargetLevel);
}

FText AMiniGameDoor::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return Prompt;
}
