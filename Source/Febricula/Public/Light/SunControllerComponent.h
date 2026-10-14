#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SunControllerComponent.generated.h"

class ASkyLight;
class USkyLightComponent;
class USkyLightController;
class UTimeSubsystem;

/** 시간대별 해 회전 (이미 다른 헤더에 정의돼 있다면 이 정의는 지운다) */
USTRUCT(BlueprintType)
struct FSunRotationBlock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	FRotator MorningTimeRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	FRotator AfternoonTimeRot = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun")
	FRotator DinnerTimeRot = FRotator::ZeroRotator;
};

/**
 * 디렉셔널 라이트에 붙는다. 시간대가 바뀌면 해와 스카이라이트를 **정해진 순서로** 바꾼다.
 *
 *   프레임 0     : 해 회전 (강도가 내려가는 방향이면 스카이라이트도 즉시 내림)
 *   다음 프레임  : RecaptureSky — 하늘이 새 해 방향으로 갱신된 뒤에 찍는다
 *   + RampDelay  : 강도가 올라가는 방향이면 RampTime 동안 보간해서 올림
 *
 * ⚠ OnScheduleChanged 의 리스너는 이것 하나여야 한다. 스카이라이트 쪽에 따로 바인드하면
 *   델리게이트 순서에 따라 번쩍임이 다시 생긴다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class FEBRICULA_API USunControllerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USunControllerComponent();

	/** 지금 시간대로 해와 스카이라이트를 맞춘다 */
	UFUNCTION(BlueprintCallable, Category = "Lighting")
	void SetSunRot();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UFUNCTION()
	void HandleDialogueFinished();

	UFUNCTION()
	void HandleScheduleChanged(FGameplayTag ChangedScheduleName);

	UPROPERTY(EditAnywhere, Category = "Sun Rotation Block")
	FSunRotationBlock SunRotationBlock;

	/** 같은 레벨의 스카이라이트. 비워 두면 레벨에서 하나 찾는다 */
	UPROPERTY(EditInstanceOnly, Category = "Lighting")
	TObjectPtr<ASkyLight> SkyLight;

	/** 캡처 요청 뒤 강도를 올리기 시작할 때까지의 대기 */
	UPROPERTY(EditAnywhere, Category = "Lighting", meta = (ClampMin = "0"))
	float RampDelay = 0.1f;

	/** 강도를 올리는 데 걸리는 시간. 0 이면 즉시 */
	UPROPERTY(EditAnywhere, Category = "Lighting", meta = (ClampMin = "0"))
	float RampTime = 1.5f;

private:
	void ApplySchedule(bool bInstant);
	void DoRecapture();
	void StartRamp();
	void TickRamp();

	UTimeSubsystem* GetTimeSubsystem() const;
	USkyLightComponent* GetSkyComp() const;
	USkyLightController* GetSkyController() const;

	FTimerHandle RecaptureTimer;
	FTimerHandle RampStartTimer;
	FTimerHandle RampTimer;

	float RampFrom = 1.f;
	float RampTo = 1.f;
	float RampElapsed = 0.f;
	bool bRampInstant = false;

	static constexpr float RampStep = 1.f / 30.f;
	bool bPendingApply = false;
};