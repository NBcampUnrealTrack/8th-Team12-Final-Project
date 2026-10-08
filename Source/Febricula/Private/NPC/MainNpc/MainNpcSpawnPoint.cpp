// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/MainNpcSpawnPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

AMainNpcSpawnPoint::AMainNpcSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	// 기본 캐릭터 캡슐 크기. 액터 위치가 발이므로 반 높이만큼 올려서 보여준다.
	PreviewCapsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PreviewCapsule"));
	if (PreviewCapsule)
	{
		PreviewCapsule->SetupAttachment(Root);
		PreviewCapsule->InitCapsuleSize(34.f, 88.f);
		PreviewCapsule->SetRelativeLocation(FVector(0.f, 0.f, 88.f));
		PreviewCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewCapsule->ShapeColor = FColor(255, 170, 0);
	}

	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow)
	{
		Arrow->SetupAttachment(Root);
		Arrow->SetRelativeLocation(FVector(0.f, 0.f, 88.f));
		Arrow->ArrowColor = FColor(255, 170, 0);
	}
#endif
}
