// Fill out your copyright notice in the Description page of Project Settings.
//
// 플레이어가 들고 있는 단일 HUD 위젯.
// 상호작용 프롬프트 / 대화창 / 선택지를 "하나의 위젯 안"에 두는 이유:
// 서로 다른 위젯으로 나누면 Z-order를 따로 관리해야 하고, 선택지가 대화창 뒤로 깔려서
// 클릭이 안 먹는 사고가 나기 쉽다. 한 위젯 안의 Overlay로 쌓으면 순서가 곧 레이어라 그런 문제가 없다.
//
// UMG 위젯 블루프린트를 이 클래스를 부모로 만들고, 아래 BP_ 이벤트들을 구현하면 된다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Dialogue/DialogueDataTypes.h"
#include "PlayerHUDWidget.generated.h"

class UDialogueSubsystem;

UCLASS(Abstract)
class FEBRICULA_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ---- 상호작용 프롬프트 (InteractionComponent가 호출) ----

	/** "E - 대화하기" 같은 프롬프트를 띄운다 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Prompt")
	void ShowInteractionPrompt(const FText& Prompt);

	UFUNCTION(BlueprintCallable, Category = "HUD|Prompt")
	void HideInteractionPrompt();

	// ---- UMG에서 구현할 시각 표현 ----

	/** 프롬프트 텍스트 표시. 위젯 BP에서 텍스트 블록에 꽂고 보이기 처리 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Prompt")
	void BP_ShowInteractionPrompt(const FText& Prompt);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Prompt")
	void BP_HideInteractionPrompt();

	/** 대사 한 줄 표시. Speaker가 비어 있으면 나레이션이므로 이름칸을 숨기면 된다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Dialogue")
	void BP_ShowDialogueLine(const FText& Speaker, const FText& Body);

	/** 선택지 목록 표시. bEnabled=false인 항목은 회색 + 클릭 불가로 처리할 것 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Dialogue")
	void BP_ShowChoices(const TArray<FDialogueChoiceView>& Choices);

	/** 선택지 목록 숨기기 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Dialogue")
	void BP_HideChoices();

	/** 대화창 전체 숨기기 */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD|Dialogue")
	void BP_HideDialogue();

	// ---- UMG 버튼에서 호출할 입력 ----

	/** 대화창(또는 화면) 클릭 시 다음 대사로 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Dialogue")
	void RequestAdvance();

	/** 선택지 버튼 클릭 시 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Dialogue")
	void RequestSelectChoice(int32 ChoiceIndex, bool bYesNo = false);

	UFUNCTION(BlueprintPure, Category = "HUD|Dialogue")
	bool IsDialogueActive() const;

protected:
	// 대화 서브시스템 델리게이트 수신부
	UFUNCTION()
	void HandleDialogueLine(const FText& Speaker, const FText& Body);

	UFUNCTION()
	void HandleDialogueChoices(const TArray<FDialogueChoiceView>& Choices, bool bYesNo = false);

	UFUNCTION()
	void HandleDialogueFinished();

	/** 이동 대기 구간: 대화는 계속되지만 창만 잠시 닫는다 */
	UFUNCTION()
	void HandleDialogueBoxHidden();

	UDialogueSubsystem* GetDialogueSubsystem() const;

	/** 대화 중에는 상호작용 프롬프트를 가린다 (겹쳐 보이면 지저분하므로) */
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText CachedPrompt;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bPromptVisible = false;
};
