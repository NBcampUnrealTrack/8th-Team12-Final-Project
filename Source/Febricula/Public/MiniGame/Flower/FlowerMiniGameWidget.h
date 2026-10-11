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

namespace FlowerPhysics
{
	class FWorld;
	struct FBody;
}

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

	struct FPopText
	{
		FVector2f Pos;
		FString Text;
		FLinearColor Color;
		float Life = 1.f;
	};

	/** 조각 모양: 네모(흙 · 종이), 꽃잎(길쭉하게 돌며 흩날림), 동그라미(반짝이 · 물방울) */
	enum class EParticleShape : uint8
	{
		Square,
		Petal,
		Dot,
	};

	struct FParticle
	{
		FVector2f Pos;
		FVector2f Vel;
		FLinearColor Color;
		float Life = 0.7f;
		float MaxLife = 0.7f;
		float Size = 7.f;
		float Gravity = 180.f;
		float Angle = 0.f;
		float Spin = 0.f;
		/** 좌우로 살랑이는 세기 (잎 · 꽃잎) */
		float Sway = 0.f;
		EParticleShape Shape = EParticleShape::Square;
	};

	/** 퍼져 나가는 점선 고리 (착지 · 합성 충격파) */
	struct FRing
	{
		FVector2f Center;
		float Radius = 0.f;
		float Speed = 0.f;
		float Life = 0.5f;
		float MaxLife = 0.5f;
		float DotSize = 8.f;
		FLinearColor Color;
		/** 점 고리 대신 꽉 찬 원이 번쩍 퍼진다 (합성 순간) */
		bool bFlash = false;
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
	/** 다음에 나올 꽃 (본 근무는 무작위 씨앗~장미, 연습은 단계마다 정해진 꽃) */
	int32 RollNext() const;
	/** 피워 낸 주문 꽃을 포장한다 (떨어뜨린 꽃은 안 받는다) */
	void Fulfill();
	void NextOrder();
	/** 물리 한 스텝 뒤 착지 · 합성 이벤트를 받아 점수 · 효과로 바꾼다 */
	void ProcessEvents();
	/** 연출 타이머 (부풀기 · 찌그러짐 · 흔들림) */
	void UpdateBodyFx(float Dt);

	// ---- 단계별 효과 ----
	/** 처음 바닥이나 다른 꽃에 닿았을 때: 단계별 바운스 + 주변 밀쳐내기 + 조각 */
	void OnLanded(FlowerPhysics::FBody& Body, float Impact);
	/** 합성으로 새 꽃이 피어날 때: 번쩍임 · 고리 · 조각 · 주변 밀어내기. 해바라기부터 화면 흔들림 */
	void OnBorn(const FlowerPhysics::FBody& Body);
	/** 떨어지는 동안 단계별 꼬리 (흙 알갱이 · 잎 · 꽃잎) */
	void EmitTrail(FlowerPhysics::FBody& Body, float Dt);
	/** 단계별 조각 한 무리. Upward > 0이면 위로 솟구치는 분수 모양 */
	void Spray(int32 Tier, float InX, float InY, int32 Count, float Speed, float Upward);
	void AddRing(float InX, float InY, float StartRadius, float Speed, float Life, float DotSize, const FLinearColor& Color, bool bFlash = false);
	void AddShake(float Amount);
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
	/**
	 * 꽃 하나. (InX, InY)는 무게중심, 그림은 무게중심에서 떨어진 그림 가운데에 Angle만큼 돌려 그린다.
	 * Scale은 피어날 때 부풀기, Squash > 0이면 납작(착지), < 0이면 길쭉(빠르게 떨어지는 중).
	 */
	void PaintFlower(FFlowerPainter& P, float InX, float InY, float Angle, int32 Tier, float Scale = 1.f, float Alpha = 1.f, float Squash = 0.f) const;
	void PaintEffects(FFlowerPainter& P) const;
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
	/** 작업대 물리 (꽃 모양 강체). 헤더에 물리 정의를 끌어오지 않으려고 포인터로 둔다 */
	TSharedPtr<FlowerPhysics::FWorld> World;
	TArray<FPopText> Pops;
	TArray<FParticle> Particles;
	TArray<FRing> Rings;
	/** 화면 흔들림 세기 (px). 점점 줄어든다 */
	float Shake = 0.f;
	/** 이번 프레임 흔들림 오프셋 (Tick에서 정하고 Paint에서 쓴다) */
	FVector2f ShakeOffset = FVector2f::ZeroVector;
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
