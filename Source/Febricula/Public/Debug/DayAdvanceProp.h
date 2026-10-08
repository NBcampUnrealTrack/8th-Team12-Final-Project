// [skydodo] 2026-10-08 작성
// 요약: 상호작용하면 다음 날 아침으로 넘기는 테스트용 오브젝트
// [테스트용] 테스트 맵 전용 오브젝트. 실제 게임 레벨에는 배치하지 말 것 (실제 게임의 시간 경과는 이벤트 액션 TimeAdvanceDay 등으로 한다)
//
// 레벨에 놓기만 하면 된다. E로 상호작용하면 시간대를 끝까지 넘겨 다음 날 아침이 되고,
// "N일차 아침" 대사를 띄운다. 일차/시간대가 바뀌면 메인 NPC 스포너가 알아서 다시 배치한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "DayAdvanceProp.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTimeSubsystem;

// [테스트용]
UCLASS()
class FEBRICULA_API ADayAdvanceProp : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	ADayAdvanceProp();

	//~ Begin IInteractable
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	//~ End IInteractable

	/**
	 * 다음 날 아침으로 넘긴다. 시간대를 끝까지 넘기는 실제 게임 흐름(UTimeSubsystem::ResetSchedule)과 같다.
	 * ResetSchedule은 시간표가 비어 있으면 끝나지 않으므로 횟수를 제한해서 직접 돌리고, 안 되면 날만 넘긴다.
	 */
	static void AdvanceToNextMorning(UTimeSubsystem* Time);

protected:
	UPROPERTY(VisibleAnywhere, Category = "DayAdvance")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 상호작용 컴포넌트의 주변 검사에 걸리도록 하는 영역 */
	UPROPERTY(VisibleAnywhere, Category = "DayAdvance")
	TObjectPtr<UBoxComponent> InteractionVolume;

	UPROPERTY(VisibleAnywhere, Category = "DayAdvance")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(EditAnywhere, Category = "DayAdvance")
	FText InteractionPrompt;
};
