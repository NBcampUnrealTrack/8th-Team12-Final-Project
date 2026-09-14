// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPC/NpcTypes.h"
#include "CharacterAnimationComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FEBRICULA_API UCharacterAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UCharacterAnimationComponent();
	
	UFUNCTION(BlueprintCallable, Category = "Animation Component")
	bool PlayAnim(FName AnimId, bool bLoop = false);
	
	UFUNCTION(BlueprintCallable, Category = "Animation Component")
	void StopAnim(float BlendOutTime = 0.25f);
	
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	// Called when the game starts
	/**
	 * 대화 데이터에서 이름으로 호출할 애니메이션 목록.
	 * 키는 데이터에서 쓸 짧은 이름(예: "Sit", "Think", "Write"), 값은 AnimSequence 애셋.
	 * AnimMontage를 넣어도 동작한다.
	 *
	 * 이렇게 이름으로 한 단계 감싸두면, 나중에 애니메이션 애셋이 교체돼도
	 * 대화 JSON은 그대로 두고 이 맵만 바꾸면 된다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Animation")
	TMap<FName, TObjectPtr<UAnimSequenceBase>> AnimLibrary;

	/** 슬롯 애니메이션을 재생할 슬롯 이름. 애님 블루프린트의 Slot 노드 이름과 일치해야 한다 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Animation")
	FName AnimSlotName = TEXT("DefaultSlot");

	/** 슬롯 애니메이션 블렌드 인/아웃 시간 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Animation")
	float AnimBlendTime = 0.25f;

	/**
	 * 애님 블루프린트를 거치지 않고 시퀀스를 직접 재생한다 (단일 노드 모드).
	 *
	 * 애님 BP가 아직 없는 NPC에서도 애니메이션 테스트를 할 수 있게 만든 폴백이다.
	 * 단점: 재생하는 동안 로코모션이 사라지고 그 시퀀스만 나온다.
	 * 애님 BP가 붙어 있지 않으면 이 값과 상관없이 자동으로 이 방식이 쓰인다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NPC|Animation")
	bool bPreferSingleNodeAnim = false;
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bLogSlotWeight = false;
};
