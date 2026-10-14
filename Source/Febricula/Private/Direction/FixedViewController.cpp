// Fill out your copyright notice in the Description page of Project Settings.


#include "Direction/FixedViewController.h"

#include "EngineUtils.h"
#include "FebriculaPlayerController.h"
#include "Camera/CameraActor.h"
#include "Direction/FixedViewComponent.h"
#include "GameMode/LevelSetting.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
// [skydodo] 2026-10-13 버그 수정: SceneComponent를 만들지 않고 RootComponent에 nullptr을 넣고 있었다
AFixedViewController::AFixedViewController()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneComponent;
}

FName AFixedViewController::AddFixedCamera(UFixedViewComponent* FixedViewComponent)
{
	if (!FixedViewComponent)
	{
 		UE_LOG(LogTemp, Warning, TEXT("Fixed View Controller: 카메라가 존재하지 않음"));
 		return NAME_None;
	}
	TArray<FName> CameraTag = FixedViewComponent->ComponentTags;
	if (CameraTag.IsEmpty())
	{
 		UE_LOG(LogTemp, Warning, TEXT("Fixed View Controller: 카메라에 태그가 없음\n기본 태그 추가"))
		FixedViewComponent->ComponentTags.Add(TEXT("Fixed Camera"));
		CameraTag = FixedViewComponent->ComponentTags;
	}
	if (!FixedViewComponent->HasTriggerBox())
	{
		UE_LOG(LogTemp, Warning, TEXT("Fixed View Controller: 카메라에 연결된 콜리전이 없음"))
		return NAME_None;
	}
	if (FixedCameraActors.Contains(CameraTag[0]))
	{
 		UE_LOG(LogTemp, Warning, TEXT("Fixed View Controller: 이미 등록된 카메라"))
 		return NAME_None;
	}
	return CameraTag[0];
}

// [skydodo] 2026-10-13 버그 수정: Find()->Get()과 NowFixedCamera->ComponentTags[0]을 검사 없이 역참조해 크래시 가능
void AFixedViewController::OnOverlapBegin(FName ComponentTag, bool bState)
{
	if (bState)
	{
		if (UFixedViewComponent* FixedViewComponent = FixedCameraActors.FindRef(ComponentTag))
		{
			UE_LOG(LogTemp, Warning, TEXT("Now Trigger: %s"), *FixedViewComponent->GetOwner()->GetName());
			NowFixedCamera = FixedViewComponent;
			if (AFebriculaPlayerController* Controller = Cast<AFebriculaPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
			{
				Controller->SetViewTarget(NowFixedCamera->GetOwner());
			}
		}
	}
	else
	{
		if (ComponentTag != NAME_None && NowFixedCamera && !NowFixedCamera->ComponentTags.IsEmpty())
		{
			if (ComponentTag == NowFixedCamera->ComponentTags[0])
			{
				NowFixedCamera = FixedCameraActors.FindRef(FName("First"));
			}
		}
	}
}

// Called when the game starts or when spawned
// [skydodo] 2026-10-13 버그 수정: 카메라 하나 등록에 실패하면 return으로 나머지 카메라까지 등록되지 않았다 -> continue
void AFixedViewController::BeginPlay()
{
	Super::BeginPlay();
	if (ALevelSetting* LevelSetting = Cast<ALevelSetting>(GetWorldSettings()))
	{
		if (!LevelSetting->bUseFixedCamera)
		{
			UE_LOG(LogTemp, Warning, TEXT("Fixed Camera Controller: 고정 카메라를 사용하지 않는 레벨에 배치된 컨트롤러"));
			return;
		}
		TArray<ACameraActor*> FoundedCameras;
		for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
		{
			if (UFixedViewComponent* Comp = It->FindComponentByClass<UFixedViewComponent>())
			{
				FName CameraTag = AddFixedCamera(Comp);
				if (CameraTag == NAME_None)
				{
					// 카메라 하나가 잘못돼도 나머지는 등록한다
					UE_LOG(LogTemp, Warning, TEXT("Fixed View Controller: %s 카메라 추가 실패"), *It->GetName());
					continue;
				}
				UE_LOG(LogTemp, Log, TEXT("Fixed View Controller: %s 추가됨"), *CameraTag.ToString());
				FixedCameraActors.Add(CameraTag, Comp);
				Comp->OnOverlapBegin.AddDynamic(this, &AFixedViewController::OnOverlapBegin);
			}
		}
	}
}

