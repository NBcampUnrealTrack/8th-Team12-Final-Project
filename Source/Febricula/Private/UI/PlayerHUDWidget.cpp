// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/PlayerHUDWidget.h"

#include "Dialogue/DialogueSubsystem.h"
#include "Engine/World.h"

void UPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UDialogueSubsystem* Dialogue = GetDialogueSubsystem())
	{
		Dialogue->OnDialogueLine.AddDynamic(this, &UPlayerHUDWidget::HandleDialogueLine);
		Dialogue->OnDialogueChoices.AddDynamic(this, &UPlayerHUDWidget::HandleDialogueChoices);
		Dialogue->OnDialogueFinished.AddDynamic(this, &UPlayerHUDWidget::HandleDialogueFinished);
	}

	BP_HideDialogue();
	BP_HideChoices();
	BP_HideInteractionPrompt();
}

void UPlayerHUDWidget::NativeDestruct()
{
	if (UDialogueSubsystem* Dialogue = GetDialogueSubsystem())
	{
		Dialogue->OnDialogueLine.RemoveDynamic(this, &UPlayerHUDWidget::HandleDialogueLine);
		Dialogue->OnDialogueChoices.RemoveDynamic(this, &UPlayerHUDWidget::HandleDialogueChoices);
		Dialogue->OnDialogueFinished.RemoveDynamic(this, &UPlayerHUDWidget::HandleDialogueFinished);
	}

	Super::NativeDestruct();
}

// ---------------------------------------------------------------------------
// 상호작용 프롬프트
// ---------------------------------------------------------------------------

void UPlayerHUDWidget::ShowInteractionPrompt(const FText& Prompt)
{
	CachedPrompt = Prompt;
	bPromptVisible = true;

	// 대화 중이면 프롬프트를 띄우지 않는다 (대화창과 겹침 방지).
	if (IsDialogueActive())
	{
		return;
	}

	BP_ShowInteractionPrompt(Prompt);
}

void UPlayerHUDWidget::HideInteractionPrompt()
{
	bPromptVisible = false;
	CachedPrompt = FText::GetEmpty();
	BP_HideInteractionPrompt();
}

// ---------------------------------------------------------------------------
// 대화 델리게이트 수신
// ---------------------------------------------------------------------------

void UPlayerHUDWidget::HandleDialogueLine(const FText& Speaker, const FText& Body)
{
	// 대화가 시작되면 프롬프트는 감춘다.
	BP_HideInteractionPrompt();

	// 새 대사가 나오면 이전 선택지는 항상 지운다.
	BP_HideChoices();
	BP_ShowDialogueLine(Speaker, Body);
}

void UPlayerHUDWidget::HandleDialogueChoices(const TArray<FDialogueChoiceView>& Choices)
{
	BP_ShowChoices(Choices);
}

void UPlayerHUDWidget::HandleDialogueFinished()
{
	BP_HideChoices();
	BP_HideDialogue();

	// 대화가 끝났는데 아직 상호작용 범위 안이면 프롬프트를 되살린다.
	if (bPromptVisible)
	{
		BP_ShowInteractionPrompt(CachedPrompt);
	}
}

// ---------------------------------------------------------------------------
// UMG에서 들어오는 입력
// ---------------------------------------------------------------------------

void UPlayerHUDWidget::RequestAdvance()
{
	if (UDialogueSubsystem* Dialogue = GetDialogueSubsystem())
	{
		Dialogue->Advance();
	}
}

void UPlayerHUDWidget::RequestSelectChoice(int32 ChoiceIndex)
{
	if (UDialogueSubsystem* Dialogue = GetDialogueSubsystem())
	{
		Dialogue->SelectChoice(ChoiceIndex);
	}
}

bool UPlayerHUDWidget::IsDialogueActive() const
{
	const UDialogueSubsystem* Dialogue = GetDialogueSubsystem();
	return Dialogue && Dialogue->IsDialogueActive();
}

UDialogueSubsystem* UPlayerHUDWidget::GetDialogueSubsystem() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UDialogueSubsystem>();
	}
	return nullptr;
}
