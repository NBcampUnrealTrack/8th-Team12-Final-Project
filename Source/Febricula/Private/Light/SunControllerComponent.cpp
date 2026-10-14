#include "Light/SunControllerComponent.h"

#include "Light/SkyLightController.h"
#include "Components/SkyLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "GameMode/TimeSubsystem.h" // 실제 경로에 맞출 것

USunControllerComponent::USunControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USunControllerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!SkyLight)
	{
		SkyLight = Cast<ASkyLight>(UGameplayStatics::GetActorOfClass(this, ASkyLight::StaticClass()));
		UE_LOG(LogTemp, Warning, TEXT("[Sun] SkyLight 미지정 — 자동으로 찾음: %s"), *GetNameSafe(SkyLight));
	}

	if (UTimeSubsystem* Time = GetTimeSubsystem())
	{
		Time->OnScheduleChanged.AddDynamic(this, &USunControllerComponent::HandleScheduleChanged);
	}

	// 레벨 시작 시에는 보간 없이 바로 맞춘다
	ApplySchedule(/*bInstant=*/true);
}

void USunControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTimeSubsystem* Time = GetTimeSubsystem())
	{
		Time->OnScheduleChanged.RemoveDynamic(this, &USunControllerComponent::HandleScheduleChanged);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Super::EndPlay(EndPlayReason);
}

void USunControllerComponent::HandleScheduleChanged(FGameplayTag ChangedScheduleName)
{
	ApplySchedule(/*bInstant=*/false);
}

void USunControllerComponent::SetSunRot()
{
	ApplySchedule(/*bInstant=*/false);
}

void USunControllerComponent::ApplySchedule(bool bInstant)
{
	UTimeSubsystem* Time = GetTimeSubsystem();
	AActor* Owner = GetOwner();
	if (!Time || !Owner)
	{
		return;
	}

	const FString Point = Time->GetNowSchedulePoint();
	if (Point.IsEmpty())
	{
		return;
	}

	// 1) 해 회전
	if (Point.Equals(TEXT("MorningTime")))
	{
		Owner->SetActorRotation(SunRotationBlock.MorningTimeRot);
	}
	else if (Point.Equals(TEXT("AfternoonTime")))
	{
		Owner->SetActorRotation(SunRotationBlock.AfternoonTimeRot);
	}
	else
	{
		Owner->SetActorRotation(SunRotationBlock.DinnerTimeRot);
	}

	// 진행 중이던 이전 전환은 취소
	FTimerManager& TM = GetWorld()->GetTimerManager();
	TM.ClearTimer(RecaptureTimer);
	TM.ClearTimer(RampStartTimer);
	TM.ClearTimer(RampTimer);

	USkyLightComponent* Sky = GetSkyComp();
	USkyLightController* Ctrl = GetSkyController();
	float Target = 0.f;
	if (!Sky || !Ctrl || !Ctrl->GetTargetIntensity(Target))
	{
		return;
	}

	RampTo = Target;
	bRampInstant = bInstant;

	// 강도가 내려가는 방향(밤 → 아침)은 캡처 **전에** 바로 내린다.
	// 높은 강도가 남은 채로 밝은 하늘을 찍으면 그 순간 번쩍인다.
	if (Target <= Sky->Intensity)
	{
		Sky->SetIntensity(Target);
	}

	// 2) 다음 프레임에 캡처 — 같은 프레임이면 이전 해 방향의 하늘이 찍힌다
	RecaptureTimer = TM.SetTimerForNextTick(this, &USunControllerComponent::DoRecapture);

	UE_LOG(LogTemp, Log, TEXT("[Sun] 시간대 %s — 하늘 강도 %.1f -> %.1f"), *Point, Sky->Intensity, Target);
}

void USunControllerComponent::DoRecapture()
{
	USkyLightComponent* Sky = GetSkyComp();
	if (!Sky)
	{
		return;
	}
	Sky->RecaptureSky();

	// 3) 강도가 올라가는 방향(오후 → 밤)은 캡처가 반영된 뒤에 올린다
	if (Sky->Intensity < RampTo)
	{
		GetWorld()->GetTimerManager().SetTimer(RampStartTimer, this,
			&USunControllerComponent::StartRamp, FMath::Max(RampDelay, 0.01f), false);
	}
}

void USunControllerComponent::StartRamp()
{
	USkyLightComponent* Sky = GetSkyComp();
	if (!Sky)
	{
		return;
	}

	if (bRampInstant || RampTime <= 0.f)
	{
		Sky->SetIntensity(RampTo);
		return;
	}

	RampFrom = Sky->Intensity;
	RampElapsed = 0.f;
	GetWorld()->GetTimerManager().SetTimer(RampTimer, this,
		&USunControllerComponent::TickRamp, RampStep, true);
}

void USunControllerComponent::TickRamp()
{
	USkyLightComponent* Sky = GetSkyComp();
	if (!Sky)
	{
		GetWorld()->GetTimerManager().ClearTimer(RampTimer);
		return;
	}

	RampElapsed += RampStep;
	const float Alpha = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(RampElapsed / RampTime, 0.f, 1.f));
	Sky->SetIntensity(FMath::Lerp(RampFrom, RampTo, Alpha));

	if (Alpha >= 1.f)
	{
		GetWorld()->GetTimerManager().ClearTimer(RampTimer);
	}
}

UTimeSubsystem* USunControllerComponent::GetTimeSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UTimeSubsystem>() : nullptr;
}

USkyLightComponent* USunControllerComponent::GetSkyComp() const
{
	return SkyLight ? SkyLight->GetLightComponent() : nullptr;
}

USkyLightController* USunControllerComponent::GetSkyController() const
{
	return SkyLight ? SkyLight->FindComponentByClass<USkyLightController>() : nullptr;
}