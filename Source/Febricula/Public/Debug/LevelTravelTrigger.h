// Fill out your copyright notice in the Description page of Project Settings.
//
// 플레이어가 들어오면 다른 레벨로 넘어가는 테스트용 트리거.
//
// 레벨에 놓고 TargetLevel만 지정하면 된다. 위에 목적지 이름이 떠 있어서 어디로 가는 문인지 보인다.
// GameInstance 서브시스템(시간/호감도/진행)은 레벨을 넘어가도 유지되므로,
// "A에서 호감도를 올리고 B로 가면 NPC가 B에 있다" 같은 흐름을 그대로 확인할 수 있다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelTravelTrigger.generated.h"

class UBoxComponent;
class UTextRenderComponent;

UCLASS()
class FEBRICULA_API ALevelTravelTrigger : public AActor
{
	GENERATED_BODY()

public:
	ALevelTravelTrigger();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 넘어갈 레벨 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LevelTravel")
	TSoftObjectPtr<UWorld> TargetLevel;

	UPROPERTY(VisibleAnywhere, Category = "LevelTravel")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, Category = "LevelTravel")
	TObjectPtr<UTextRenderComponent> Label;

	/** 레벨이 막 열린 직후 바로 되돌아가지 않도록 잠깐 무시한다 */
	float IgnoreUntil = 0.f;
};
