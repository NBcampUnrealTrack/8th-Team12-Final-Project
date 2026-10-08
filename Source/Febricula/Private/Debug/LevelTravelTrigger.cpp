// Fill out your copyright notice in the Description page of Project Settings.

#include "Debug/LevelTravelTrigger.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

ALevelTravelTrigger::ALevelTravelTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(100.f, 100.f, 100.f));
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetHiddenInGame(false);
	Box->ShapeColor = FColor(0, 200, 255);
	SetRootComponent(Box);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Box);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(40.f);
	Label->SetTextRenderColor(FColor(0, 200, 255));
}

void ALevelTravelTrigger::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const FString Target = TargetLevel.IsNull() ? TEXT("(TargetLevel 없음)") : TargetLevel.GetAssetName();
	Label->SetText(FText::FromString(FString::Printf(TEXT("-> %s"), *Target)));
}

void ALevelTravelTrigger::BeginPlay()
{
	Super::BeginPlay();

	IgnoreUntil = GetWorld()->GetTimeSeconds() + 1.f;
	Box->OnComponentBeginOverlap.AddDynamic(this, &ALevelTravelTrigger::HandleBeginOverlap);
}

void ALevelTravelTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled() || TargetLevel.IsNull())
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() < IgnoreUntil)
	{
		return;
	}

	UGameplayStatics::OpenLevelBySoftObjectPtr(this, TargetLevel);
}
