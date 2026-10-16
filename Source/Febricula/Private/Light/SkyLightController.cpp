#include "Light/SkyLightController.h"

#include "Components/SkyLightComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameMode/TimeSubsystem.h" // 실제 경로에 맞출 것

USkyLightController::USkyLightController()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool USkyLightController::GetTargetIntensity(float& OutIntensity) const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UTimeSubsystem* Time = GI ? GI->GetSubsystem<UTimeSubsystem>() : nullptr;
	if (!Time)
	{
		return false;
	}

	const FString Point = Time->GetNowSchedulePoint();
	if (Point.IsEmpty())
	{
		return false;
	}

	if (Point.Equals(TEXT("MorningTime")))
	{
		OutIntensity = IntensityBlock.MorningTime;
	}
	else if (Point.Equals(TEXT("AfternoonTime")))
	{
		OutIntensity = IntensityBlock.AfternoonTime;
	}
	else
	{
		OutIntensity = IntensityBlock.DinnerTime;
	}
	return true;
}

bool USkyLightController::SetIntensity()
{
	USkyLightComponent* Sky = GetSkyLightComponent();
	float Target = 0.f;
	if (!Sky || !GetTargetIntensity(Target))
	{
		return false;
	}
	Sky->SetIntensity(Target);
	return true;
}

USkyLightComponent* USkyLightController::GetSkyLightComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<USkyLightComponent>() : nullptr;
}