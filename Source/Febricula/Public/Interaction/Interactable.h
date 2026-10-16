// [skydodo] 2026-08-10 작성 (최종 수정 2026-08-17)
// 요약: 상호작용 가능한 모든 액터가 구현하는 인터페이스
// Fill out your copyright notice in the Description page of Project Settings.
//
// NPC뿐 아니라 상호작용 가능한 모든 액터(사물함, 옥상 문, 서가 등)가 함께 쓸 수 있도록
// 캐릭터 클래스가 아닌 별도 인터페이스로 분리했다.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 상호작용 가능한 오브젝트가 구현하는 인터페이스.
 * 이벤트 트리의 "전제 조건 / 발생 조건" 판정은 이 인터페이스 바깥(퀘스트·이벤트 매니저)에서
 * 처리하고, 여기서는 "상호작용이 시작됐다"는 사실만 넘겨준다.
 */
class FEBRICULA_API IInteractable
{
	GENERATED_BODY()

public:
	/** 지금 상호작용을 걸 수 있는 상태인지 (대화 중, 컷씬 중이면 false 등) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	bool CanInteract(AActor* Interactor) const;

	/** 실제 상호작용 진입점. 구현부에서 이벤트 트리 진입 여부를 판단해 위임한다 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void Interact(AActor* Interactor);

	/** "F. 상호작용" 같은 UI 프롬프트에 표시할 텍스트 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FText GetInteractionPrompt(AActor* Interactor) const;
};
