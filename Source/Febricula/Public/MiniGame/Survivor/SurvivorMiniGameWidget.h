// [skydodo] 2026-10-14 작성
// 요약: 편의점 야간 알바 미니게임 - 진상 손님 사이에서 90초 버티기 (뱀파이어 서바이버 풍)
// Fill out your copyright notice in the Description page of Project Settings.
//
// 원본: 기획 HTML 시안 survivor.html. 규칙과 수치를 그대로 옮기고 그림·소리만 입혔다.
//   - WASD / 방향키로 이동. "죄송합니다"는 가까운 손님에게 자동으로 날아간다
//   - 손님 인내심(HP)을 다 깎으면 퇴점 → 초록 영수증(경험치)을 떨어뜨린다
//   - 영수증을 모아 레벨이 오르면 서비스 스킬 3개 중 하나를 고른다 (고르는 동안 시간 정지)
//   - 손님과 부딪히거나 쿠폰에 맞으면 멘탈이 깎인다. 멘탈 0이면 퇴근, 90초 버티면 칼퇴
//
// 화면: 버거·꽃집은 세로 플레이 영역 + 좌우 패널이었지만 이 게임은 넓을수록 재미있어서
// 가로 화면(2200×1240) 전체가 매장 바닥이다. 시간·멘탈 등은 위쪽 띠, 익힌 스킬은 아래쪽 띠에 둔다.
// 보상은 MiniGame Setting 값(돈 고정 + 말솜씨). 점수 = 퇴점시킨 손님 수.

#pragma once

#include "CoreMinimal.h"
#include "MiniGame/MiniGameWidget.h"
#include "Styling/SlateBrush.h"
#include "SurvivorMiniGameWidget.generated.h"

class USoundBase;
class UTexture2D;
struct FSurvivorPainter;

UCLASS()
class FEBRICULA_API USurvivorMiniGameWidget : public UMiniGameWidget
{
	GENERATED_BODY()

public:
	USurvivorMiniGameWidget(const FObjectInitializer& ObjectInitializer);

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
	 * 그림. 키는 코드에서 부르는 이름이다.
	 * 캐릭터 시트(가로로 프레임을 이어 붙인 320px 칸): hero_walk_front, hero_walk_back, hero_idle_front, hero_idle_back,
	 *   hero_hurt, hero_zen, hero_failure, hero_success, <손님>_walk_front / _walk_back (+ _flash = 피격용 흰 실루엣),
	 *   coupon_throw (+ _flash). 손님: tipsy(취객) · rusher(화난 손님) · coupon(쿠폰 진상) · group(단체 손님)
	 * 소품: xp_receipt, xp_blue, manual, coupon0~2, scrap0~3, apology(말풍선), floor, shelf, exit
	 * 스킬 아이콘: skill_<아이콘 이름>
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Survivor|Art")
	TMap<FName, TSoftObjectPtr<UTexture2D>> Art;

	/**
	 * 효과음. 키는 코드에서 부르는 이름이다.
	 * hit(사과 명중) · exit(퇴점) · pickup(영수증) · levelup · click(카드·버튼) · hurt(멘탈 깎임)
	 * · rush(마지막 10초) · shift_end(칼퇴) · fail(멘탈 퇴근) · heal(회복) · start(출근)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Survivor|Sound")
	TMap<FName, TSoftObjectPtr<USoundBase>> Sfx;

	UPROPERTY(EditDefaultsOnly, Category = "Survivor|Sound", meta = (ClampMin = "0", ClampMax = "2"))
	float SfxVolume = 1.f;

	/** 근무 시간 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Survivor|Rule", meta = (ClampMin = "10"))
	float Duration = 90.f;

private:
	enum class EMode : uint8
	{
		Title,
		Practice,   // 연습 판 - 움직이기 · 자동 응대 · 영수증 · 스킬 고르기를 직접 해 본다 (처음엔 자동)
		Tutorial,   // 설명 카드 - 타이틀의 "설명 보기"로 여는 요약본
		Play,
		LevelUp,    // 스킬 3택 (시간 정지). 연습 중에도 뜬다
		Pause,
		Ending,     // 90초 버팀 → 조끼 벗고 출구로 걸어 나가는 연출
		Failing,    // 멘탈 0 → 주저앉는 연출
		Result,
	};

	enum class EButtonAction : uint8
	{
		Start,          // 출근하기 (처음이면 연습 판부터)
		StartPractice,
		SkipPractice,
		ShowCards,
		NextPage,       // 카드 다음 / 연습 설명 단계의 "알겠어요"
		PrevPage,
		CloseCards,
		SkipShift,      // 대충 일하고 퇴근 (한 번 플레이한 뒤부터)
		Resume,
		Exit,
		Choose0,        // 레벨업 카드 1~3
		Choose1,
		Choose2,
	};

	enum class EEnemyType : uint8
	{
		Drunk,   // 취객: 비틀비틀 접근
		Angry,   // 화난 손님: 멈칫한 뒤 돌진
		Coupon,  // 쿠폰 진상: 거리를 두고 쿠폰 투척
		Group,   // 단체 손님: 셋이 함께, 작고 약함
		Boss,    // 블랙 컨슈머: 근무 중간에 한 번. 크고 단단하며 "사장 나와!" 고함(범위 멘탈 피해) + 단체 손님 호출
	};

	/** 서비스 스킬 (HTML upgrades 순서 + 2026-10-14 추가 공격 4종) */
	enum class ESkill : uint8
	{
		Multi, Fast, Manual, Boss, Zen, Speed, Magnet, Heal,
		Scanner, Mop, Chain, Bag,
		Num
	};

	struct FOverlayButton
	{
		float X = 0.f;
		float Y = 0.f;
		float W = 0.f;
		float H = 0.f;
		FString Label;
		EButtonAction Action = EButtonAction::Start;
		bool bPrimary = false;
		/** 버튼 대신 레벨업 카드로 그린다 */
		bool bCard = false;
	};

	struct FEnemy
	{
		/** 연쇄 사과 · 부메랑이 같은 손님을 두 번 치지 않게 구분하는 번호 */
		int32 Id = 0;
		FVector2f Pos;
		EEnemyType Type = EEnemyType::Drunk;
		float R = 26.f;
		int32 Hp = 1;
		int32 MaxHp = 1;
		FVector2f Knock = FVector2f::ZeroVector;
		float Flash = 0.f;
		float Age = 0.f;
		/** 다음 행동(돌진 · 투척)까지 남은 시간 */
		float Action = 1.f;
		float Dash = 0.f;
		FVector2f DashDir = FVector2f::ZeroVector;
		/** 걷기 진행도 (1 = 4프레임 한 바퀴) */
		float Walk = 0.f;
		bool bFlip = false;
		bool bBack = false;
		/** 쿠폰 투척 모션 진행 시간 (보스는 고함 예고 시간). 음수면 하는 중 아님 */
		float ThrowT = -1.f;
		bool bThrown = false;
		bool bWarned = false;
	};

	/** Bounces = 1+1 연쇄 사과로 더 튕길 수 있는 횟수, LastHitId = 방금 맞힌 손님 (다시 맞지 않게) */
	struct FShot { FVector2f Pos; FVector2f Vel; float Life = 1.5f; int32 Bounces = 0; int32 LastHitId = INDEX_NONE; };
	/** 바코드 스캐너 레이저: 그리기용 (피해는 쏘는 순간 한 번에 준다) */
	struct FBeam { FVector2f Start; FVector2f Dir; float Length = 0.f; float Width = 0.f; float Life = 0.f; };
	/** 물걸레 자국: 밟은 손님은 미끄러져 느려지고 인내심이 조금씩 깎인다 */
	struct FPuddle { FVector2f Pos; float Radius = 0.f; float Life = 0.f; float MaxLife = 1.f; };
	/** 비닐봉지 부메랑: 날아갔다 돌아오며 길 위의 손님을 모두 친다. HitIds = 이번 방향에서 이미 맞힌 손님 */
	struct FBag { FVector2f Pos; FVector2f Vel; FVector2f Dir; float Age = 0.f; bool bReturning = false; TArray<int32> HitIds; };
	struct FCouponShot { FVector2f Pos; FVector2f Vel; float Life = 4.f; int32 Variant = 0; };
	struct FGem { FVector2f Pos; int32 Value = 1; float Seed = 0.f; };
	struct FPopText { FVector2f Pos; FString Text; FLinearColor Color; float Life = 1.1f; };
	struct FCallRing { FVector2f Pos; float Radius = 0.f; float Life = 1.f; };
	/** 퇴점하는 손님: 옆으로 걸어 나가며 사라진다 */
	struct FDeparture { FVector2f Pos; EEnemyType Type = EEnemyType::Drunk; float R = 26.f; float Walk = 0.f; float Dir = 1.f; float Life = 0.65f; };
	/** 피격 종이 조각 */
	struct FScrap { FVector2f Pos; FVector2f Vel; float Rot = 0.f; float Spin = 0.f; int32 Variant = 0; float Life = 0.55f; };

	// ---- 규칙 (HTML의 reset / spawn / wave / hit / stress / level / update) ----
	void ResetRound();
	void StartRound();
	void TogglePause();
	void SkipShift();
	void StartPractice();
	void SetPracticeStep(int32 Step);
	void UpdatePractice(float Dt);
	void ChangeCardPage(int32 Delta);
	void DoButton(EButtonAction Action);
	void PressPrimaryButton();
	void Update(float Dt);
	void UpdateEffects(float Dt);
	void UpdateEnding(float Dt);
	void SpawnEnemy(EEnemyType Type, const FVector2f* Near = nullptr);
	void SpawnWave();
	void HitEnemy(FEnemy& Enemy, int32 Damage, const FVector2f& Push);
	void Stress(float Amount);
	void OpenLevelUp();
	void ChooseSkill(int32 Index);
	void ApplySkill(ESkill Skill);
	/** 멘탈 0 / 근무 시간 버팀 → 연출 뒤 정산 */
	void BeginFailing();
	void BeginEnding();
	void FinishShift(bool bSurvived);
	void AddPop(const FVector2f& Pos, const FString& Text, const FLinearColor& Color);
	void AddScraps(const FVector2f& Pos, int32 Count);
	void PlaySfx(FName Key);
	/** 추가 공격 4종 (스캐너 · 물걸레 · 연쇄 사과 · 부메랑) */
	void UpdateExtraAttacks(float Dt);
	/** Shot이 손님을 맞힌 뒤: 연쇄 사과가 남았으면 다음 손님에게 튕기고 true */
	bool BounceShot(FShot& Shot, const FEnemy& Victim);
	int32 SkillLevel(ESkill Skill) const { return Taken[static_cast<int32>(Skill)]; }

	// ---- 화면 ----
	TArray<FOverlayButton> GetOverlayButtons() const;
	int32 FindButtonAt(const FVector2f& ScreenPos) const;
	static void GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset);
	static FVector2f ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos);
	const FSlateBrush* FindBrush(FName Name) const;
	const FSlateBrush* SkillIcon(ESkill Skill) const;
	/** 스킬 아이콘. 그림이 아직 없는 스킬은 색 동그라미 + 글자로 대신 그린다 */
	void PaintSkillIcon(FSurvivorPainter& P, ESkill Skill, float X, float Y, float Size) const;

	void PaintStore(FSurvivorPainter& P) const;
	void PaintActors(FSurvivorPainter& P) const;
	void PaintEnemy(FSurvivorPainter& P, const FEnemy& Enemy) const;
	void PaintHero(FSurvivorPainter& P) const;
	void PaintProjectiles(FSurvivorPainter& P) const;
	void PaintHud(FSurvivorPainter& P) const;
	void PaintOverlay(FSurvivorPainter& P) const;
	void PaintLevelUp(FSurvivorPainter& P) const;
	void PaintTutorialPage(FSurvivorPainter& P) const;
	void PaintPractice(FSurvivorPainter& P) const;
	void PaintButtons(FSurvivorPainter& P) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> LoadedTextures;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<USoundBase>> LoadedSfx;

	TMap<FName, FSlateBrush> ArtBrushes;
	FSlateBrush SolidBrush;
	FSlateBrush CircleBrush;
	FSlateBrush PanelBrush;
	FSlateBrush DarkPanelBrush;
	FSlateBrush OverlayBrush;
	FSlateBrush CardBrush;
	FSlateBrush CardHoverBrush;
	FSlateBrush ButtonBrush;
	FSlateBrush ButtonHoverBrush;
	FSlateBrush SecondaryBrush;
	FSlateBrush SecondaryHoverBrush;
	FSlateBrush BarBackBrush;
	FSlateBrush ReceiptBrush;

	// ---- 상태 ----
	EMode Mode = EMode::Title;
	/** 레벨업 · 일시정지에서 돌아갈 모드 (연습 / 본 근무) */
	EMode ResumeMode = EMode::Play;
	float T = 0.f;

	FVector2f HeroPos;
	float HeroR = 25.f;
	float HeroHurt = 0.f;
	float HeroWalk = 0.f;
	bool bHeroBack = false;
	bool bHeroFlip = false;
	bool bHeroMoving = false;

	// 스킬 수치 (HTML의 g)
	float Mental = 100.f;
	float MoveSpeed = 390.f;
	int32 ShotCount = 1;
	float ShotCooldown = 0.5f;
	float FireTimer = 0.f;
	int32 Damage = 2;
	int32 ManualCount = 0;
	float ManualAngle = 0.f;
	float ManualTimer = 0.f;
	int32 BossLevel = 0;
	float BossTimer = 0.f;
	int32 ZenLevel = 0;
	float Magnet = 150.f;
	int32 Taken[static_cast<int32>(ESkill::Num)] = {};

	// 추가 공격 상태
	/** 마지막으로 걸은 방향 (바코드 스캐너가 쏘는 쪽) */
	FVector2f FacingDir = FVector2f(1.f, 0.f);
	float ScannerTimer = 0.f;
	float BagTimer = 0.f;
	float PuddleTickTimer = 0.f;
	/** 마지막 물걸레 자국에서 걸은 거리 */
	float MopDistance = 0.f;
	int32 NextEnemyId = 0;

	int32 Level = 1;
	int32 Xp = 0;
	int32 XpNeed = 5;
	int32 PendingLevels = 0;
	TArray<ESkill> Choices;

	int32 Exits = 0;
	float SpawnTimer = 0.f;
	bool bRush = false;
	bool bBossSpawned = false;

	TArray<FEnemy> Enemies;
	TArray<FShot> Shots;
	TArray<FCouponShot> Coupons;
	TArray<FGem> Gems;
	TArray<FPopText> Pops;
	TArray<FCallRing> Rings;
	TArray<FDeparture> Departures;
	TArray<FScrap> Scraps;
	TArray<FBeam> Beams;
	TArray<FPuddle> Puddles;
	TArray<FBag> Bags;

	/** 칼퇴 연출 */
	float EndingT = 0.f;
	FVector2f EndingStart;
	/** 멘탈 퇴근 연출 */
	float FailT = 0.f;
	bool bSurvived = false;

	bool bUpHeld = false;
	bool bDownHeld = false;
	bool bLeftHeld = false;
	bool bRightHeld = false;
	int32 HoveredButton = INDEX_NONE;
	int32 PressedButton = INDEX_NONE;

	int32 TutorialPage = 0;
	int32 PracticeStep = 0;
	float PracticeStepTime = 0.f;
	/** 연습 단계 조건 (움직인 거리 · 퇴점 수 기준) */
	float PracticeMoved = 0.f;
	int32 PracticeExitBase = 0;
	int32 PlayCount = 0;
	bool bSkipped = false;
	TMap<FName, double> LastSfxTime;

	FMiniGameReward FinalReward;
};
