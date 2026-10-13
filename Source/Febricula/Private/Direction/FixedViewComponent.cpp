// Fill out your copyright notice in the Description page of Project Settings.


#include "Direction/FixedViewComponent.h"

#include "Engine/TriggerBox.h"
#include "Kismet/GameplayStatics.h"


// Sets default values for this component's properties
UFixedViewComponent::UFixedViewComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

bool UFixedViewComponent::HasTriggerBox()
{
	if (ConnectedTriggerBox)
	{
		return true;
	}
	return false;
	
}

// [skydodo] 2026-10-13 버그 수정: 태그가 없는 컴포넌트면 ComponentTags[0]에서 크래시
void UFixedViewComponent::HandleOverlapBegin(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!ComponentTags.IsEmpty())
	{
		OnOverlapBegin.Broadcast(ComponentTags[0], true);
	}
}

void UFixedViewComponent::HandleOverlapEnd(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!ComponentTags.IsEmpty())
	{
		OnOverlapBegin.Broadcast(ComponentTags[0], false);
	}
}


// Called when the game starts
void UFixedViewComponent::BeginPlay()
{
	Super::BeginPlay();
	ConnectedTriggerBox->OnActorBeginOverlap.AddDynamic(this, &UFixedViewComponent::HandleOverlapBegin);
	ConnectedTriggerBox->OnActorEndOverlap.AddDynamic(this, &UFixedViewComponent::HandleOverlapEnd);
	
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
	{
		if (!ConnectedTriggerBox) return;
		ConnectedTriggerBox->UpdateOverlaps(false); // 이벤트 없이 오버랩 목록만 갱신
		APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
		if (Pawn && ConnectedTriggerBox->IsOverlappingActor(Pawn))
		{
			HandleOverlapBegin(ConnectedTriggerBox, Pawn);
		}
	});

	UE_LOG(LogTemp, Warning, TEXT("Fixed View Component: 트리거 박스가 연결되지 않음"));
	
}


