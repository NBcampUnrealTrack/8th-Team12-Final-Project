// [skydodo] 2026-10-12 작성
// 요약: 꽃집 알바 미니게임 - 같은 꽃끼리 합쳐 주문 꽃을 만들면 자동 포장
// Fill out your copyright notice in the Description page of Project Settings.
//
// 원본: 기획 HTML 시안 flower.html / play.html (flower-complete-v1). 규칙과 물리 수치를 그대로 옮겼다.
//   - 90초 근무. 마우스로 위치 고르고 클릭, 또는 ← → / A D 이동 + Space로 꽃을 떨어뜨린다
//   - 같은 단계 꽃 둘이 닿으면 다음 단계로 합쳐진다 (씨앗 → 새싹 → … → 꽃다발)
//   - 주문판의 꽃이 작업대에 생기면 자동 포장돼서 자리가 빈다. 3건 뒤부터 두 꽃 주문
//   - 위험선 위로 2초 넘게 쌓이면 근무 끝
//
// 화면 틀은 햄버거 미니게임(UBurgerMiniGameWidget)과 같다.
// 가운데 플레이 영역은 HTML 캔버스와 같은 840×1240 좌표계, 머리글·주문판·꽃 단계표는 가로 화면(2200×1240)의 좌우 패널로 옮겼다.
// 보상은 HTML의 성과급 대신 MiniGame Setting 값(돈 고정 + 체력)을 쓴다. 점수 = 완료한 주문 수.

#pragma once

#include "CoreMinimal.h"
#include "MiniGame/MiniGameWidget.h"
#include "Styling/SlateBrush.h"
#include "FlowerMiniGameWidget.generated.h"

class USoundBase;
class UTexture2D;
struct FFlowerPainter;

UCLASS()
class FEBRICULA_API UFlowerMiniGameWidget : public UMiniGameWidget
{
	GENERATED_BODY()

public:
	UFlowerMiniGameWidget(const FObjectInitializer& ObjectInitializer);

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
	 * 꽃: seed, sprout, daisy, tulip, rose, sunflower, hydrangea, bouquet
	 * 화면: shop, order-panel, status-panel, button-normal, button-hover, button-pressed
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Flower|Art")
	TMap<FName, TSoftObjectPtr<UTexture2D>> Art;

	/**
	 * 효과음. 키는 코드에서 부르는 이름이다.
	 * drop(놓기) · merge(합체) · order(주문 완료) · warning(10초 남음) · finish(근무 종료) · overflow(넘침 실패) · click(버튼)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Flower|Sound")
	TMap<FName, TSoftObjectPtr<USoundBase>> Sfx;

	UPROPERTY(EditDefaultsOnly, Category = "Flower|Sound", meta = (ClampMin = "0", ClampMax = "2"))
	float SfxVolume = 1.f;

	/** 근무 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Flower|Rule", meta = (ClampMin = "10"))
	float Duration = 90.f;

private:
	enum class EMode : uint8
	{
		Title,
		Practice,   // 연습 판 - 단계마다 정해진 꽃을 놓아 보며 직접 익힌다 (처음엔 자동)
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
		NextPage,       // 카드 다음 / 연습 설명 단계의 "알겠어요"
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

	/** 작업대 위의 꽃 하나 (HTML의 ball) */
	struct FFlowerBall
	{
		int32 Id = 0;
		int32 Tier = 0;
		float X = 0.f;
		float Y = 0.f;
		float VX = 0.f;
		float VY = 0.f;
		float R = 0.f;
		/** 생긴 뒤 지난 시간 - 합성 유예 · 위험선 유예 판정 */
		float Age = 0.f;
		/** 생길 때 살짝 부풀었다 돌아오는 연출 */
		float Pop = 0.f;
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

	// ---- 규칙 (HTML의 reset / action / merge / fulfill / physics / danger) ----
	void ResetRound();
	void StartRound();
	void TogglePause();
	void EndRound(bool bOverflow);
	void SkipShift();
	void StartPractice();
	void SetPracticeStep(int32 Step);
	/** 연습 판: 단계마다 놓을 꽃을 정해 주고, 조건을 채우면 다음 단계로 */
	void UpdatePractice(float Dt);
	void ChangeCardPage(int32 Delta);
	void DoButton(EButtonAction Action);
	void PressPrimaryButton();
	void Update(float Dt);
	void UpdateEffects(float Dt);
	/** 지금 꽃을 커서 위치에 떨어뜨린다 */
	void Drop();
	/** 다음에 나올 꽃 (본 근무는 무작위 씨앗~데이지, 연습은 단계마다 정해진 꽃) */
	int32 RollNext() const;
	FFlowerBall& AddBall(int32 Tier, float InX, float InY, float InVY, float InPop);
	void Merge(int32 IndexA, int32 IndexB);
	void Fulfill();
	void NextOrder();
	void Physics(float Dt);
	static void Bounds(FFlowerBall& Ball);
	void UpdateDanger(float Dt);
	void AddPop(float InX, float InY, const FString& Text, const FLinearColor& Color);
	void PlaySfx(FName Key);
	void Burst(float InX, float InY, const FLinearColor& Color);

	// ---- 화면 ----
	TArray<FOverlayButton> GetOverlayButtons() const;
	/** 화면 좌표(2200×1240)에 있는 버튼 번호. 없으면 INDEX_NONE */
	int32 FindButtonAt(const FVector2f& ScreenPos) const;
	/** 가로 화면을 위젯 크기에 맞춰 가운데 놓는 배율과 위치 */
	static void GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset);
	static FVector2f ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos);
	/** 마우스 위치로 꽃 놓을 위치를 옮긴다. 플레이 영역 안이면 true */
	bool AimAt(const FVector2f& ScreenPos);
	const FSlateBrush* FindBrush(FName Name) const;
	const FSlateBrush* FlowerBrush(int32 Tier) const;

	void PaintSidePanels(FFlowerPainter& P) const;
	void PaintGame(FFlowerPainter& P) const;
	/** HTML의 ball(): 받침 원 + 꽃 그림 */
	void PaintBall(FFlowerPainter& P, float InX, float InY, int32 Tier, float Bounce, float Alpha = 1.f) const;
	void PaintOverlay(FFlowerPainter& P) const;
	void PaintTutorialPage(FFlowerPainter& P) const;
	void PaintPractice(FFlowerPainter& P) const;
	void PaintButtons(FFlowerPainter& P) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundBase>> LoadedSfx;

	TMap<FName, FSlateBrush> ArtBrushes;
	FSlateBrush SolidBrush;
	FSlateBrush CircleBrush;
	FSlateBrush RingBrush;
	FSlateBrush WorkAreaBrush;
	FSlateBrush SlotDoneBrush;
	FSlateBrush SlotWaitBrush;
	FSlateBrush BarBackBrush;
	FSlateBrush BarFillBrush;
	FSlateBrush DangerFillBrush;
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
	float Cursor = 420.f;
	TArray<FFlowerBall> Balls;
	TArray<FPopText> Pops;
	TArray<FParticle> Particles;
	int32 NextTier = 0;
	int32 QueueTier = 0;
	float Cooldown = 0.f;
	int32 Orders = 0;
	/** 합성 점수 (기록용 - 보상과 무관) */
	int32 MergeScore = 0;
	/** 지금까지 만든 가장 높은 단계 */
	int32 BestTier = 0;
	/** 위험선 위에 쌓여 있던 시간 (2초면 끝) */
	float Over = 0.f;
	int32 Serial = 0;
	/** 남은 주문 꽃 단계 / 이번 주문에서 이미 포장한 꽃 단계 */
	TArray<int32> Want;
	TArray<int32> Fulfilled;
	int32 Chain = 0;
	float ChainTimer = 0.f;
	int32 BestChain = 0;
	int32 Bouquets = 0;
	int32 Drops = 0;
	int32 Merges = 0;

	bool bLeftHeld = false;
	bool bRightHeld = false;
	int32 HoveredButton = INDEX_NONE;
	int32 PressedButton = INDEX_NONE;

	int32 TutorialPage = 0;
	int32 PracticeStep = 0;
	/** 현재 단계에 들어온 뒤 지난 시간 */
	float PracticeStepTime = 0.f;
	/** 단계에 들어올 때의 합성 · 놓기 횟수 (단계 조건은 이 값과 비교) */
	int32 PracticeMergeBase = 0;
	int32 PracticeDropBase = 0;
	bool bWarningAnnounced = false;
	/** 넘쳐서 끝났는지 (결과 문구) */
	bool bOverflowed = false;
	/** 열릴 때 읽어 둔 플레이 횟수 (0이면 처음) */
	int32 PlayCount = 0;
	bool bSkipped = false;
	/** 같은 효과음 80ms 안 중복 재생 막기 (HTML 규칙) */
	TMap<FName, double> LastSfxTime;

	/** 결과 화면에 보여줄 지급된 보상 */
	FMiniGameReward FinalReward;
};
