// [skydodo] 2026-10-10 작성
// 요약: 햄버거집 알바 미니게임 - 떨어지는 재료를 트레이로 받아 주문 완성
// Fill out your copyright notice in the Description page of Project Settings.
//
// 원본: 기획 HTML 시안 play.html (burger-complete-v2). 규칙과 화면 배치를 그대로 옮겼다.
//   - 60초 근무. ← → / A D 이동, Space 제출·정리, P / Esc 일시정지
//   - 주문판의 재료를 순서 상관없이 받는다 (빵은 빵 세트 1개)
//   - 다른 재료·상한 패티를 받으면 콤보 초기화 + 트레이 적재량 증가 (적재량만큼 느려짐)
//   - 완성하면 Space로 제출, 미완성이면 Space는 불필요한 재료만 정리
//
// 화면은 위젯 트리 없이 NativePaint에서 직접 그린다. 가운데 플레이 영역은 HTML 캔버스와 같은 840×1240 좌표계,
// 주문판·상태판은 가로 화면(2200×1240)의 좌우 패널로 옮겼다.
// 좌표·크기를 HTML과 같은 숫자로 두어 시안을 고치면 숫자만 옮기면 된다.
// 보상은 HTML의 성과급 대신 MiniGame Setting 값(돈 고정 + 손재주)을 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "MiniGame/MiniGameWidget.h"
#include "Styling/SlateBrush.h"
#include "BurgerMiniGameWidget.generated.h"

class USoundBase;
class UTexture2D;
struct FBurgerPainter;

UCLASS()
class FEBRICULA_API UBurgerMiniGameWidget : public UMiniGameWidget
{
	GENERATED_BODY()

public:
	UBurgerMiniGameWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;

	/**
	 * 그림. 키는 코드에서 부르는 이름이다 (HTML 시안의 이름 그대로).
	 * 캐릭터: idle, blink, success, failure, walk-left-0~3, walk-right-0~3
	 * 재료: bun-set, patty, cheese, lettuce, tomato, spoiled_patty, finished_burger
	 * 화면: kitchen, order-panel, status-panel, button-normal, button-hover, button-pressed
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Burger|Art")
	TMap<FName, TSoftObjectPtr<UTexture2D>> Art;

	/**
	 * 효과음. 키는 코드에서 부르는 이름이다.
	 * catch(먹었을 때) · complete(주문 재료 다 모음) · wrong(잘못된 재료) · spoiled(상한 패티)
	 * submit(Space - 정리·미완성) · submit_ok(제출 성공) · rush(10초 남음) · shift_end(근무 끝) · click(버튼)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Burger|Sound")
	TMap<FName, TSoftObjectPtr<USoundBase>> Sfx;

	UPROPERTY(EditDefaultsOnly, Category = "Burger|Sound", meta = (ClampMin = "0", ClampMax = "2"))
	float SfxVolume = 1.f;

	/** 근무 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Burger|Rule", meta = (ClampMin = "10"))
	float Duration = 60.f;

private:
	enum class EMode : uint8
	{
		Title,
		Practice,   // 연습 판 - 정해진 재료를 하나씩 떨어뜨리며 직접 해 보게 한다 (처음엔 자동)
		Tutorial,   // 설명 카드 - 타이틀의 "설명 보기"로 여는 요약본
		Play,
		Pause,
		Result,
	};

	enum class EButtonAction : uint8
	{
		Start,          // 출근하기 (처음이면 연습 판부터)
		StartPractice,  // 연습하기
		SkipPractice,   // 연습 건너뛰고 바로 출근
		ShowCards,      // 설명 보기
		NextPage,       // 카드 다음
		PrevPage,       // 카드 이전
		CloseCards,     // 카드 닫기 → 타이틀
		SkipShift,      // 대충 일하고 퇴근 (한 번 플레이한 뒤부터)
		Resume,
		Exit,
	};

	/** 오버레이 버튼 하나. 좌표는 플레이 영역(840×1240) 기준, 가운데 */
	struct FOverlayButton
	{
		float X = 0.f;
		float Y = 0.f;
		float W = 0.f;
		float H = 0.f;
		FString Label;
		EButtonAction Action = EButtonAction::Start;
		/** Enter/Space가 누르는 버튼. 아니면 작은 보조 버튼으로 그린다 */
		bool bPrimary = false;
	};

	struct FFallingItem
	{
		int32 Id = 0;
		float X = 0.f;
		float Y = 0.f;
		float Speed = 0.f;
	};

	struct FPopText
	{
		FVector2f Pos;
		FString Text;
		FLinearColor Color;
		float Life = 1.f;
	};

	struct FParticle
	{
		FVector2f Pos;
		FVector2f Vel;
		FLinearColor Color;
		float Life = 0.7f;
	};

	// ---- 규칙 (HTML의 reset / update / spawn / catchItem / action) ----
	void ResetRound();
	void StartRound();
	void TogglePause();
	void EndRound();
	void SkipShift();
	void StartPractice();
	void SetPracticeStep(int32 Step);
	/** 연습 판: 단계마다 정해진 재료를 떨어뜨리고, 조건을 채우면 다음 단계로 */
	void UpdatePractice(float Dt);
	void ChangeCardPage(int32 Delta);
	void DoButton(EButtonAction Action);
	void PressPrimaryButton();
	void Update(float Dt);
	void UpdateEffects(float Dt);
	void Spawn();
	void CatchItem(int32 Id);
	void Action();
	TArray<int32> Missing() const;
	void AddPop(float InX, float InY, const FString& Text, const FLinearColor& Color);
	void PlaySfx(FName Key) const;
	/** 지금 적재량에서의 이동 속도 비율 (1 = 원래 속도) */
	float GetSpeedRatio() const;
	void AddJunk(int32 Id, int32 Count);
	void ClearJunk();
	void Burst(float InX, float InY, const FLinearColor& Color);

	// ---- 화면 ----
	FName CurrentPose() const;
	TArray<FOverlayButton> GetOverlayButtons() const;
	/** 화면 좌표(2200×1240)에 있는 버튼 번호. 없으면 INDEX_NONE */
	int32 FindButtonAt(const FVector2f& ScreenPos) const;
	/** 840×1240 캔버스를 화면 높이에 맞춰 가운데 놓는 배율과 위치 */
	static void GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset);
	static FVector2f ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos);
	const FSlateBrush* FindBrush(FName Name) const;

	void PaintSidePanels(FBurgerPainter& P) const;
	void PaintGame(FBurgerPainter& P) const;
	void PaintOverlay(FBurgerPainter& P) const;
	void PaintTutorialPage(FBurgerPainter& P) const;
	void PaintPractice(FBurgerPainter& P) const;
	void PaintButtons(FBurgerPainter& P) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundBase>> LoadedSfx;

	TMap<FName, FSlateBrush> ArtBrushes;
	FSlateBrush SolidBrush;
	FSlateBrush SlotDoneBrush;
	FSlateBrush SlotWaitBrush;
	FSlateBrush BarBackBrush;
	FSlateBrush BarFillBrush;
	FSlateBrush OverlayBrush;
	FSlateBrush ReceiptBrush;
	FSlateBrush FrameBrush;
	FSlateBrush PanelBrush;
	FSlateBrush SecondaryBrush;
	FSlateBrush SecondaryHoverBrush;
	FSlateBrush HighlightBrush;

	// ---- 상태 ----
	EMode Mode = EMode::Title;
	float T = 0.f;
	float PlayerX = 420.f;
	TArray<FFallingItem> Items;
	TArray<FPopText> Pops;
	TArray<FParticle> Particles;
	int32 RecipeIndex = 0;
	TArray<int32> Got;
	int32 Junk = 0;
	int32 Orders = 0;
	int32 Errors = 0;
	int32 Combo = 0;
	int32 Best = 0;
	float SpawnTimer = 0.f;
	float Serving = 0.f;
	float Bounce = 0.f;
	int32 Serial = 0;
	FName Reaction;
	float ReactionUntil = 0.f;

	bool bLeftHeld = false;
	bool bRightHeld = false;
	int32 HoveredButton = INDEX_NONE;
	int32 PressedButton = INDEX_NONE;

	int32 TutorialPage = 0;
	int32 PracticeStep = 0;
	/** 현재 단계에 들어온 뒤 지난 시간 (마지막 단계에서 잠깐 쉬었다가 출근) */
	float PracticeStepTime = 0.f;
	/** 상한 패티를 받은 뒤 못 움직이는 남은 시간 */
	float Stun = 0.f;
	int32 SpoiledCount = 0;
	/** 트레이에 쌓인 불필요한 재료 (그림으로 쌓아 보여 준다). Junk == JunkIds.Num() */
	TArray<int32> JunkIds;
	/** 걷기 애니메이션 진행 - 느려지면 발걸음도 느려진다 */
	float WalkPhase = 0.f;
	/** 연습 판 "움직여 보세요" 단계에서 움직인 거리 */
	float PracticeMoved = 0.f;
	bool bRushAnnounced = false;
	/** 열릴 때 읽어 둔 플레이 횟수 (0이면 처음) */
	int32 PlayCount = 0;
	bool bSkipped = false;

	/** 결과 화면에 보여줄 지급된 보상 */
	FMiniGameReward FinalReward;
};
