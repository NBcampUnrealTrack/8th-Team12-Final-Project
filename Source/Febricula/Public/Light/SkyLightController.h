#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SkyLightController.generated.h"

class USkyLightComponent;

/** 시간대별 스카이라이트 강도 */
USTRUCT(BlueprintType)
struct FSkyIntensityBlock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	float MorningTime = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	float AfternoonTime = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lighting")
	float DinnerTime = 1.f;
};

/**
 * 스카이라이트 액터에 붙는다. 시간대별 목표 강도를 알고 있을 뿐,
 * ⚠ 스스로 OnScheduleChanged 에 바인드하지 않는다 — 순서는 USunControllerComponent 가 정한다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FEBRICULA_API USkyLightController : public UActorComponent
{
	GENERATED_BODY()

public:
	USkyLightController();

	/** 지금 시간대의 목표 강도를 계산만 한다. 시간대를 못 읽으면 false */
	bool GetTargetIntensity(float& OutIntensity) const;

	/** 목표 강도를 즉시 적용한다 (보간 없음) */
	UFUNCTION(BlueprintCallable, Category = "Lighting")
	bool SetIntensity();

	USkyLightComponent* GetSkyLightComponent() const;

protected:
	UPROPERTY(EditAnywhere, Category = "Lighting")
	FSkyIntensityBlock IntensityBlock;
};