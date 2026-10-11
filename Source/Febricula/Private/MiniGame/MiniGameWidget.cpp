// [skydodo] 2026-10-10 작성
// 요약: 미니게임 위젯 베이스 구현
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/MiniGameWidget.h"

#include "MiniGame/MiniGameSubsystem.h"

void UMiniGameWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 키보드는 위젯이 직접 받는다. UserWidget 기본값은 마우스를 통과시키므로 Visible로 바꾼다.
	SetIsFocusable(true);
	SetVisibility(ESlateVisibility::Visible);
	SetKeyboardFocus();
}

UMiniGameSubsystem* UMiniGameWidget::GetMiniGameSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMiniGameSubsystem>() : nullptr;
}

FMiniGameReward UMiniGameWidget::FinishGame(const FMiniGameResult& Result)
{
	UMiniGameSubsystem* Subsystem = GetMiniGameSubsystem();
	return Subsystem ? Subsystem->GrantReward(Result) : FMiniGameReward();
}

FMiniGameReward UMiniGameWidget::PreviewReward(int32 Score) const
{
	const UMiniGameSubsystem* Subsystem = GetMiniGameSubsystem();
	return Subsystem ? Subsystem->PreviewReward(Score) : FMiniGameReward();
}

int32 UMiniGameWidget::GetPlayCount() const
{
	const UMiniGameSubsystem* Subsystem = GetMiniGameSubsystem();
	return Subsystem ? Subsystem->GetPlayCount() : 0;
}

void UMiniGameWidget::ExitGame()
{
	if (UMiniGameSubsystem* Subsystem = GetMiniGameSubsystem())
	{
		Subsystem->EndMiniGame();
	}
	else
	{
		RemoveFromParent();
	}
}
