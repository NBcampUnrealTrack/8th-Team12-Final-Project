// Fill out your copyright notice in the Description page of Project Settings.
//
// 조사 가능한 사물. NPC와 똑같은 HUD를 써서 "시체인 것 같다." 같은 나레이션을 띄운다.
// 화자 이름을 비워두면 HUD가 이름칸을 감추고 나레이션처럼 보여준다.
//
// 두 가지 방식을 지원한다:
//   1) InspectLines에 문장을 직접 적기 (간단한 사물)
//   2) DialogueNodeId를 지정해 대화 테이블의 노드를 재생 (선택지가 필요한 사물)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "InteractableProp.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class FEBRICULA_API AInteractableProp : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	AInteractableProp();

	//~ Begin IInteractable
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	//~ End IInteractable

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop")
	TObjectPtr<USceneComponent> SceneComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 상호작용 감지용. 메시가 없어도 잡히도록 별도 콜리전을 둔다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Prop")
	TObjectPtr<UBoxComponent> InteractionVolume;

	/** "E - 조사하기" 처럼 표시할 프롬프트 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FText InteractionPrompt;

	/** 화면에 표시할 화자. 비워두면 나레이션 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FText SpeakerName;

	/** 순서대로 출력할 조사 문구. 예: "시체인 것 같다." */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (MultiLine = true))
	TArray<FText> InspectLines;

	/**
	 * 비워두지 않으면 InspectLines 대신 대화 테이블의 이 노드를 재생한다.
	 * 선택지가 필요한 사물(잠긴 문, 청소도구함 등)에 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FName DialogueNodeId;

	/** 한 번만 조사 가능하게 할지 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	bool bSingleUse = false;

	UPROPERTY(Transient)
	bool bUsed = false;
};
