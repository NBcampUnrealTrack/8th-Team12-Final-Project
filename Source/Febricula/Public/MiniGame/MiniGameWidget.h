// [skydodo] 2026-10-10 작성
// 요약: 미니게임 위젯 베이스 - 정산·종료를 서브시스템에 넘기는 통로
// Fill out your copyright notice in the Description page of Project Settings.
//
// 게임마다 이 클래스를 상속해 화면과 규칙만 만든다.
// 끝나면 FinishGame()으로 보상을 받고(결과 화면에 표시), 확인 버튼에서 ExitGame()을 부른다.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MiniGame/MiniGameTypes.h"
#include "MiniGameWidget.generated.h"

class UMiniGameSubsystem;

UCLASS(Abstract)
class FEBRICULA_API UMiniGameWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 보상 지급 (한 번만). 결과 화면에 띄울 값을 돌려준다 */
	FMiniGameReward FinishGame(const FMiniGameResult& Result);

	/** 지금 점수로 받을 보상 미리보기 (지급 X) */
	FMiniGameReward PreviewReward(int32 Score) const;

	/** 끝까지 플레이한 횟수. 0이면 처음 (설명 카드), 1 이상이면 건너뛰기 허용 */
	int32 GetPlayCount() const;

	/** 결과 확인 후 미니게임을 닫고 원래 레벨로 */
	void ExitGame();

protected:
	virtual void NativeConstruct() override;

	UMiniGameSubsystem* GetMiniGameSubsystem() const;
};
