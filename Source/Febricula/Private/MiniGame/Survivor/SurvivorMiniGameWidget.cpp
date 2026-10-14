// [skydodo] 2026-10-14 작성
// 요약: 편의점 야간 알바 미니게임 구현 (survivor.html 이식)
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/Survivor/SurvivorMiniGameWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"

DEFINE_LOG_CATEGORY_STATIC(LogSurvivorMiniGame, Log, All);

namespace SurvivorGame
{
	/** 가로 화면 전체가 매장이다 */
	constexpr float ScreenW = 2200.f;
	constexpr float ScreenH = 1240.f;

	/** 매장 바닥 (손님 · 주인공이 다니는 곳). 위쪽 띠는 진열대 · 시간 · 멘탈, 아래쪽 띠는 출구 · 익힌 스킬 */
	constexpr float AreaX = 40.f;
	constexpr float AreaY = 200.f;
	constexpr float AreaW = 2120.f;
	constexpr float AreaH = 950.f;
	constexpr float AreaRight = AreaX + AreaW;
	constexpr float AreaBottom = AreaY + AreaH;
	constexpr float CenterX = ScreenW * 0.5f;

	// ---- 규칙 수치 (HTML 그대로. 바닥이 넓어진 만큼만 손님을 조금 더 자주 부른다) ----
	constexpr float WarningSeconds = 10.f;
	constexpr int32 MaxEnemies = 105;
	/**
	 * HTML은 844×976 바닥이었다. 가로로 2.5배 넓어져 손님이 늦게 도착하므로 등장 간격을 줄인다.
	 * 난이도 시뮬레이션(같은 규칙, 피하며 줍는 봇 40판씩): HTML 바닥 생존 42% · 퇴점 중앙 90명,
	 * 이 바닥 ×1.0 생존 75% · 중앙 90명, ×0.85 생존 60% · 중앙 95명, ×0.7 생존 45% · 중앙 106명 → 원본에 가까운 0.85
	 */
	constexpr float SpawnIntervalScale = 0.85f;
	constexpr float ShotSpeed = 900.f;
	constexpr float ManualRadius = 115.f;
	constexpr float BossCallInterval = 7.f;
	constexpr float HurtInvincible = 0.85f;
	constexpr float GemPullSpeed = 650.f;
	constexpr float CouponSpeed = 285.f;
	constexpr float CouponReleaseTime = 0.34f;   // 쿠폰 투척 2번 프레임 (attack-events.json)
	constexpr double SfxRepeatGuard = 0.07;

	// ---- 그림 ----
	/** 캐릭터 시트 칸 크기와 발 위치 (모든 캐릭터 같음) */
	constexpr float PivotY = 304.f / 320.f;
	constexpr float HeroSize = 150.f;
	/** 걷기: 한 걸음 주기(4프레임)에 Stride px. 프레임당 0.09~0.22초로 묶는다. 서 있으면 0번(= 대기 그림) */
	constexpr float Stride = 140.f;
	constexpr float MinCycle = 1.f / (4.f * 0.22f);
	constexpr float MaxCycle = 1.f / (4.f * 0.09f);

	/** 한 번만 재생하는 모션 (초/프레임, 에셋 animations.json 값) */
	constexpr float HurtFrames[] = { 0.143f, 0.143f };
	constexpr float FailureFrames[] = { 0.25f, 0.25f, 0.25f };
	constexpr float SuccessFrames[] = { 0.2f, 0.2f, 0.2f, 0.2f };
	constexpr float ThrowFrames[] = { 0.18f, 0.16f, 0.12f, 0.24f };

	template <int32 N>
	constexpr float Length(const float (&Frames)[N])
	{
		float Sum = 0.f;
		for (int32 i = 0; i < N; ++i) { Sum += Frames[i]; }
		return Sum;
	}

	template <int32 N>
	int32 FrameAt(const float (&Frames)[N], float Time)
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (Time < Frames[i]) { return i; }
			Time -= Frames[i];
		}
		return N - 1;
	}

	/** 칼퇴 연출: 0.55초 서 있다가 조끼 벗기 → 출구까지 1.4초 걷기 */
	constexpr float EndingWalkStart = 0.55f + Length(SuccessFrames);
	constexpr float EndingWalkTime = 1.4f;
	constexpr float EndingTotal = EndingWalkStart + 1.6f;
	constexpr float FailingTotal = Length(FailureFrames) + 0.9f;

	/**
	 * 연습 판 단계
	 *   0 움직여 보기 → 1 자동 응대로 손님 3명 퇴점 → 2 영수증 주워 레벨업 (스킬 고르기)
	 *   → 3 멘탈 · 손님 설명 (Space · Enter · 버튼) → 4 준비 끝 (잠깐 뒤 실제 근무 시작)
	 */
	constexpr int32 PracticeMoveStep = 0;
	constexpr int32 PracticeApologyStep = 1;
	constexpr int32 PracticeReceiptStep = 2;
	constexpr int32 PracticeExplainStep = 3;
	constexpr int32 PracticeSteps = 5;
	constexpr float PracticeMoveGoal = 500.f;
	constexpr int32 PracticeExitGoal = 3;
	constexpr float PracticeFinishDelay = 1.6f;
	/** 연습 손님은 느리게 */
	constexpr float PracticeEnemySpeed = 0.7f;

	struct FSkillDef
	{
		const TCHAR* Name;
		const TCHAR* Desc;
		int32 Max;
		const TCHAR* Icon;
	};
	/** ESkill 순서 (HTML upgrades) */
	const FSkillDef Skills[] =
	{
		{ TEXT("죄송합니다"), TEXT("동시 응대 대상 +1. 사과의 범위가 넓어집니다."), 4, TEXT("skill_apology") },
		{ TEXT("영혼 없는 사과"), TEXT("사과 간격 25% 단축. 입은 일하고 영혼은 쉽니다."), 3, TEXT("skill_soulless") },
		{ TEXT("매뉴얼"), TEXT("회전 매뉴얼 +1. 인내심을 깎고 손님을 밀어냅니다."), 4, TEXT("skill_manual") },
		{ TEXT("점장 호출"), TEXT("즉시 + 이후 7초마다 주변 응대. 재선택 시 범위·효과 증가."), 3, TEXT("skill_manager") },
		{ TEXT("해탈"), TEXT("받는 스트레스 20%p 감소. 멘탈 25 회복 (최대 60%)."), 3, TEXT("skill_zen") },
		{ TEXT("퇴근 연습"), TEXT("이동 속도 20% 증가. 발걸음만큼은 이미 퇴근."), 2, TEXT("skill_speed") },
		{ TEXT("영수증 챙겨드릴게요"), TEXT("경험치 수집 반경 60% 증가."), 2, TEXT("skill_receipt") },
		{ TEXT("폐기 음료 한 모금"), TEXT("멘탈 45 회복. 사과 효과 1 증가."), 99, TEXT("skill_drink") },
	};

	struct FEnemyDef
	{
		const TCHAR* Art;    // 그림 이름 앞부분
		const TCHAR* Name;
		int32 BaseHp;
		float Radius;
		float DrawSize;
	};
	/** EEnemyType 순서 */
	const FEnemyDef EnemyDefs[] =
	{
		{ TEXT("tipsy"), TEXT("취객"), 3, 26.f, 132.f },
		{ TEXT("rusher"), TEXT("화난 손님"), 5, 26.f, 136.f },
		{ TEXT("coupon"), TEXT("쿠폰 진상"), 4, 26.f, 132.f },
		{ TEXT("group"), TEXT("단체 손님"), 2, 21.f, 116.f },
	};

	FLinearColor Hex(const TCHAR* InHex)
	{
		return FLinearColor(FColor::FromHex(InHex));
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = FMath::Clamp(Alpha, 0.f, 1.f);
		return Color;
	}

	/** 에셋 외곽선과 같은 진한 갈색 */
	FLinearColor Ink() { return Hex(TEXT("3a2b24")); }
	FLinearColor Mint() { return Hex(TEXT("3fae6e")); }
	FLinearColor Accent() { return Hex(TEXT("e8952a")); }
	FLinearColor Bad() { return Hex(TEXT("e24a5a")); }
	FLinearColor Muted() { return Hex(TEXT("7d6b5f")); }
	FLinearColor Tag() { return Hex(TEXT("c98a3a")); }

	FString StatLabel(FName Stat)
	{
		if (Stat == TEXT("Speech")) { return TEXT("말솜씨"); }
		if (Stat == TEXT("Mental")) { return TEXT("멘탈"); }
		if (Stat == TEXT("Health")) { return TEXT("체력"); }
		if (Stat == TEXT("HandWork")) { return TEXT("손재주"); }
		return Stat.ToString();
	}

	/** 걷기 진행: 속도에 비례, 서 있으면 처음으로 */
	void StepWalk(float& Walk, float Speed, float Dt)
	{
		Speed = FMath::Abs(Speed);
		if (Speed < 5.f)
		{
			Walk = 0.f;
			return;
		}
		Walk += Dt * FMath::Clamp(Speed / Stride, MinCycle, MaxCycle);
	}

	int32 WalkFrame(float Walk)
	{
		return FMath::FloorToInt(Walk * 4.f) % 4;
	}

	/** 설명 한 줄을 ". " 기준으로 두 줄로 (카드 폭에 맞게) */
	void SplitDesc(const TCHAR* Desc, FString& OutA, FString& OutB)
	{
		const FString Full(Desc);
		if (!Full.Split(TEXT(". "), &OutA, &OutB))
		{
			OutA = Full;
			OutB.Reset();
		}
		else
		{
			OutA += TEXT(".");
		}
	}
}

/** 화면 좌표(2200×1240)로 그리는 도우미 (꽃집의 FFlowerPainter와 같다). 그린 순서가 곧 앞뒤가 된다 */
struct FSurvivorPainter
{
	enum class EAlign : uint8 { Left, Center, Right };

	FSlateWindowElementList& Out;
	FGeometry Canvas;
	int32 Layer;
	const FSlateBrush* Solid;

	FPaintGeometry Rect(float X, float Y, float W, float H) const
	{
		return Canvas.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y)));
	}

	void Box(float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(Out, ++Layer, Rect(X, Y, W, H), Solid, ESlateDrawEffect::None, Color);
	}

	void Brush(const FSlateBrush* InBrush, float X, float Y, float W, float H, const FLinearColor& Tint = FLinearColor::White)
	{
		if (InBrush)
		{
			// MakeBox는 브러시의 TintColor를 보지 않으므로 직접 곱한다
			const FLinearColor Final = Tint * InBrush->TintColor.GetSpecifiedColor();
			FSlateDrawElement::MakeBox(Out, ++Layer, Rect(X, Y, W, H), InBrush, ESlateDrawEffect::None, Final);
		}
	}

	/** 가운데 기준으로 Angle(라디안)만큼 돌려 그린다 */
	void RotatedBrush(const FSlateBrush* InBrush, float X, float Y, float W, float H, float Angle, const FLinearColor& Tint = FLinearColor::White)
	{
		if (InBrush)
		{
			const FLinearColor Final = Tint * InBrush->TintColor.GetSpecifiedColor();
			FSlateDrawElement::MakeRotatedBox(Out, ++Layer, Rect(X, Y, W, H), InBrush, ESlateDrawEffect::None, Angle,
				TOptional<FVector2f>(), FSlateDrawElement::RelativeToElement, Final);
		}
	}

	void ArtCentered(const FSlateBrush* InBrush, float X, float Y, float Size, const FLinearColor& Tint = FLinearColor::White)
	{
		Brush(InBrush, X - Size * 0.5f, Y - Size * 0.5f, Size, Size, Tint);
	}

	/**
	 * 가로로 프레임을 이어 붙인 캐릭터 시트에서 한 칸만 그린다. (FootX, FootY) = 발 위치, Size = 칸을 그릴 크기.
	 * bFlip이면 좌우를 뒤집는다 (렌더 변환으로 뒤집어 그림 한 장으로 양쪽을 쓴다).
	 */
	void Sprite(const FSlateBrush* Sheet, int32 Frame, int32 Frames, float FootX, float FootY, float Size, bool bFlip, const FLinearColor& Tint = FLinearColor::White)
	{
		if (!Sheet || Frames <= 0)
		{
			return;
		}
		Frame = FMath::Clamp(Frame, 0, Frames - 1);
		FSlateBrush Cell = *Sheet;
		Cell.SetUVRegion(FBox2f(FVector2f(static_cast<float>(Frame) / Frames, 0.f), FVector2f(static_cast<float>(Frame + 1) / Frames, 1.f)));
		const FPaintGeometry Geometry = Canvas.ToPaintGeometry(FVector2f(Size, Size),
			FSlateLayoutTransform(FVector2f(FootX - Size * 0.5f, FootY - Size * SurvivorGame::PivotY)),
			FSlateRenderTransform(FScale2f(bFlip ? -1.f : 1.f, 1.f)), FVector2f(0.5f, 0.5f));
		FSlateDrawElement::MakeBox(Out, ++Layer, Geometry, &Cell, ESlateDrawEffect::None, Tint);
	}

	/** Y는 글자 세로 가운데. 크기는 CSS px. OutlinePx > 0이면 진한 갈색 테두리 (밝은 바닥 위 글자) */
	void Text(const FString& InText, float X, float Y, float Px, const FLinearColor& Color,
		EAlign Align = EAlign::Left, bool bBold = false, int32 OutlinePx = 0)
	{
		if (InText.IsEmpty() || !FSlateApplication::IsInitialized())
		{
			return;
		}

		// Slate 글꼴 크기는 pt(96dpi 기준 px의 3/4)
		FFontOutlineSettings Outline;
		if (OutlinePx > 0)
		{
			Outline = FFontOutlineSettings(OutlinePx, SurvivorGame::WithAlpha(SurvivorGame::Ink(), Color.A * 0.9f));
		}
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Px * 0.75f, Outline);
		const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(InText, Font);

		float TextLeft = X;
		if (Align == EAlign::Center)
		{
			TextLeft = X - Size.X * 0.5f;
		}
		else if (Align == EAlign::Right)
		{
			TextLeft = X - Size.X;
		}
		FSlateDrawElement::MakeText(Out, ++Layer, Rect(TextLeft, Y - Size.Y * 0.5f, Size.X, Size.Y), InText, Font, ESlateDrawEffect::None, Color);
	}
};

// 햄버거 · 꽃집 위젯도 EAlign을 쓴다 - 유니티 빌드로 한 파일에 묶이면 충돌하므로 이름을 달리한다
using ESurvivorAlign = FSurvivorPainter::EAlign;

// ---------------------------------------------------------------------------
// 생성 · 준비
// ---------------------------------------------------------------------------

USurvivorMiniGameWidget::USurvivorMiniGameWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	auto AddArt = [this](const TCHAR* Key, const TCHAR* Folder, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Survivor/Textures/%s/%s.%s"), Folder, Asset, Asset);
		Art.Add(Key, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path)));
	};

	AddArt(TEXT("hero_walk_front"), TEXT("Characters"), TEXT("T_Survivor_Hero_WalkFront"));
	AddArt(TEXT("hero_walk_back"), TEXT("Characters"), TEXT("T_Survivor_Hero_WalkBack"));
	AddArt(TEXT("hero_idle_front"), TEXT("Characters"), TEXT("T_Survivor_Hero_IdleFront"));
	AddArt(TEXT("hero_idle_back"), TEXT("Characters"), TEXT("T_Survivor_Hero_IdleBack"));
	AddArt(TEXT("hero_hurt"), TEXT("Characters"), TEXT("T_Survivor_Hero_Hurt"));
	AddArt(TEXT("hero_zen"), TEXT("Characters"), TEXT("T_Survivor_Hero_Zen"));
	AddArt(TEXT("hero_failure"), TEXT("Characters"), TEXT("T_Survivor_Hero_Failure"));
	AddArt(TEXT("hero_success"), TEXT("Characters"), TEXT("T_Survivor_Hero_Success"));

	// 손님: <art>_walk_front / _walk_back + 피격용 흰 실루엣 (_flash)
	const TCHAR* const EnemyAssets[][2] = { { TEXT("tipsy"), TEXT("Tipsy") }, { TEXT("rusher"), TEXT("Rusher") }, { TEXT("coupon"), TEXT("Coupon") }, { TEXT("group"), TEXT("Group") } };
	for (const auto& Pair : EnemyAssets)
	{
		for (const TCHAR* Dir : { TEXT("Front"), TEXT("Back") })
		{
			const FString Key = FString::Printf(TEXT("%s_walk_%s"), Pair[0], Dir[0] == TEXT('F') ? TEXT("front") : TEXT("back"));
			const FString Asset = FString::Printf(TEXT("T_Survivor_%s_Walk%s"), Pair[1], Dir);
			AddArt(*Key, TEXT("Characters"), *Asset);
			AddArt(*(Key + TEXT("_flash")), TEXT("Characters"), *(Asset + TEXT("_Flash")));
		}
	}
	AddArt(TEXT("coupon_throw"), TEXT("Characters"), TEXT("T_Survivor_Coupon_Throw"));
	AddArt(TEXT("coupon_throw_flash"), TEXT("Characters"), TEXT("T_Survivor_Coupon_Throw_Flash"));

	AddArt(TEXT("xp_receipt"), TEXT("Items"), TEXT("T_Survivor_Item_XpReceipt"));
	AddArt(TEXT("xp_blue"), TEXT("Items"), TEXT("T_Survivor_Item_XpBlue"));
	AddArt(TEXT("manual"), TEXT("Items"), TEXT("T_Survivor_Item_Manual"));
	for (int32 i = 0; i < 3; ++i)
	{
		AddArt(*FString::Printf(TEXT("coupon%d"), i), TEXT("Items"), *FString::Printf(TEXT("T_Survivor_Item_Coupon%d"), i));
	}
	for (int32 i = 0; i < 4; ++i)
	{
		AddArt(*FString::Printf(TEXT("scrap%d"), i), TEXT("Items"), *FString::Printf(TEXT("T_Survivor_Item_Scrap%d"), i));
	}
	AddArt(TEXT("apology"), TEXT("UI"), TEXT("T_Survivor_UI_Apology"));

	const TCHAR* const SkillAssets[][2] =
	{
		{ TEXT("skill_apology"), TEXT("Apology") }, { TEXT("skill_closing"), TEXT("Closing") }, { TEXT("skill_manager"), TEXT("Manager") },
		{ TEXT("skill_manual"), TEXT("Manual") }, { TEXT("skill_receipt"), TEXT("Receipt") }, { TEXT("skill_soulless"), TEXT("Soulless") },
		{ TEXT("skill_speed"), TEXT("Speed") }, { TEXT("skill_zen"), TEXT("Zen") }, { TEXT("skill_drink"), TEXT("DiscardedDrink") },
	};
	for (const auto& Pair : SkillAssets)
	{
		AddArt(Pair[0], TEXT("Skills"), *FString::Printf(TEXT("T_Survivor_Skill_%s"), Pair[1]));
	}

	AddArt(TEXT("floor"), TEXT("Background"), TEXT("T_Survivor_Floor"));
	AddArt(TEXT("shelf"), TEXT("Background"), TEXT("T_Survivor_Shelf"));
	AddArt(TEXT("exit"), TEXT("Background"), TEXT("T_Survivor_Exit"));

	// 효과음 (convenience-sfx-trimmed-v1에서 쓸 구간만 잘라 만든 파일)
	//   적 피격음 → Hit, 적 퇴점 → Exit, 경험치 획득 → Pickup, 레벨 업 → LevelUp, 카드 선택 → Click, 피격시 → Hurt,
	//   10초 남았을시 → Rush, 클리어 시 → ShiftEnd, 클리어 실패 시 → Fail, 회복시 → Heal, 게임 시작 → Start
	auto AddSfx = [this](const TCHAR* Key, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Survivor/Audio/SFX/%s.%s"), Asset, Asset);
		Sfx.Add(Key, TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path)));
	};
	AddSfx(TEXT("hit"), TEXT("S_Survivor_SFX_Hit"));
	AddSfx(TEXT("exit"), TEXT("S_Survivor_SFX_Exit"));
	AddSfx(TEXT("pickup"), TEXT("S_Survivor_SFX_Pickup"));
	AddSfx(TEXT("levelup"), TEXT("S_Survivor_SFX_LevelUp"));
	AddSfx(TEXT("click"), TEXT("S_Survivor_SFX_Click"));
	AddSfx(TEXT("hurt"), TEXT("S_Survivor_SFX_Hurt"));
	AddSfx(TEXT("rush"), TEXT("S_Survivor_SFX_Rush"));
	AddSfx(TEXT("shift_end"), TEXT("S_Survivor_SFX_ShiftEnd"));
	AddSfx(TEXT("fail"), TEXT("S_Survivor_SFX_Fail"));
	AddSfx(TEXT("heal"), TEXT("S_Survivor_SFX_Heal"));
	AddSfx(TEXT("start"), TEXT("S_Survivor_SFX_Start"));
}

void USurvivorMiniGameWidget::NativeConstruct()
{
	Super::NativeConstruct();
	using namespace SurvivorGame;

	LoadedTextures.Reset();
	ArtBrushes.Reset();
	for (const TPair<FName, TSoftObjectPtr<UTexture2D>>& Pair : Art)
	{
		UTexture2D* Texture = Pair.Value.LoadSynchronous();
		if (!Texture)
		{
			UE_LOG(LogSurvivorMiniGame, Warning, TEXT("그림을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
			continue;
		}
		LoadedTextures.Add(Texture);

		FSlateBrush& NewBrush = ArtBrushes.Add(Pair.Key);
		NewBrush.SetResourceObject(Texture);
		NewBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		NewBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	// 바닥은 256px 타일을 128px로 깔아 반복한다
	if (FSlateBrush* Floor = ArtBrushes.Find(TEXT("floor")))
	{
		Floor->Tiling = ESlateBrushTileType::Both;
		Floor->ImageSize = FVector2D(128.f, 128.f);
	}

	LoadedSfx.Reset();
	for (const TPair<FName, TSoftObjectPtr<USoundBase>>& Pair : Sfx)
	{
		if (USoundBase* Sound = Pair.Value.LoadSynchronous())
		{
			LoadedSfx.Add(Pair.Key, Sound);
		}
		else
		{
			UE_LOG(LogSurvivorMiniGame, Warning, TEXT("효과음을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
		}
	}

	const FLinearColor Edge = Hex(TEXT("b9a58f"));
	SolidBrush = FSlateColorBrush(FLinearColor::White);
	CircleBrush = FSlateRoundedBoxBrush(FLinearColor::White, 1.f);
	CircleBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	PanelBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fffaf0")), 0.95f), 22.f, Edge, 3.f);
	DarkPanelBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("3a2b24")), 0.88f), 16.f);
	OverlayBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fffaf0")), 0.97f), 26.f, Edge, 4.f);
	CardBrush = FSlateRoundedBoxBrush(Hex(TEXT("fffaf0")), 22.f, Edge, 3.f);
	CardHoverBrush = FSlateRoundedBoxBrush(Hex(TEXT("f1f9ef")), 22.f, Mint(), 5.f);
	ButtonBrush = FSlateRoundedBoxBrush(Mint(), 18.f, Hex(TEXT("2f7d50")), 3.f);
	ButtonHoverBrush = FSlateRoundedBoxBrush(Hex(TEXT("52c582")), 18.f, Hex(TEXT("2f7d50")), 3.f);
	SecondaryBrush = FSlateRoundedBoxBrush(FLinearColor::White, 14.f, Mint(), 2.f);
	SecondaryHoverBrush = FSlateRoundedBoxBrush(Hex(TEXT("e6f4e9")), 14.f, Mint(), 2.f);
	BarBackBrush = FSlateRoundedBoxBrush(Hex(TEXT("e5dccd")), 8.f);
	ReceiptBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f, Edge, 2.f);

	ResetRound();
	PlayCount = GetPlayCount();
	TutorialPage = 0;
	bSkipped = false;
	Mode = EMode::Title;
}

const FSlateBrush* USurvivorMiniGameWidget::FindBrush(FName Name) const
{
	return ArtBrushes.Find(Name);
}

const FSlateBrush* USurvivorMiniGameWidget::SkillIcon(ESkill Skill) const
{
	return FindBrush(SurvivorGame::Skills[static_cast<int32>(Skill)].Icon);
}

void USurvivorMiniGameWidget::PlaySfx(FName Key)
{
	const TObjectPtr<USoundBase>* Sound = LoadedSfx.Find(Key);
	if (!Sound)
	{
		return;
	}

	// 한 프레임에 여럿 처치 · 줍기처럼 몰려도 한 번만 들리게
	const double Now = FPlatformTime::Seconds();
	double& Last = LastSfxTime.FindOrAdd(Key, -1.0);
	if (Now - Last < SurvivorGame::SfxRepeatGuard)
	{
		return;
	}
	Last = Now;
	UGameplayStatics::PlaySound2D(this, *Sound, SfxVolume);
}

// ---------------------------------------------------------------------------
// 규칙
// ---------------------------------------------------------------------------

void USurvivorMiniGameWidget::ResetRound()
{
	using namespace SurvivorGame;

	T = 0.f;
	HeroPos = FVector2f(CenterX, AreaY + AreaH * 0.5f);
	HeroHurt = 0.f;
	HeroWalk = 0.f;
	bHeroBack = false;
	bHeroFlip = false;
	bHeroMoving = false;

	Mental = 100.f;
	MoveSpeed = 390.f;
	ShotCount = 1;
	ShotCooldown = 0.5f;
	FireTimer = 0.f;
	Damage = 2;
	ManualCount = 0;
	ManualAngle = 0.f;
	ManualTimer = 0.f;
	BossLevel = 0;
	BossTimer = 0.f;
	ZenLevel = 0;
	Magnet = 150.f;
	for (int32& Count : Taken)
	{
		Count = 0;
	}

	Level = 1;
	Xp = 0;
	XpNeed = 5;
	PendingLevels = 0;
	Choices.Reset();
	Exits = 0;
	SpawnTimer = 0.f;
	bRush = false;

	Enemies.Reset();
	Shots.Reset();
	Coupons.Reset();
	Gems.Reset();
	Pops.Reset();
	Rings.Reset();
	Departures.Reset();
	Scraps.Reset();

	EndingT = 0.f;
	FailT = 0.f;
	bSurvived = false;
}

void USurvivorMiniGameWidget::StartRound()
{
	ResetRound();
	bUpHeld = bDownHeld = bLeftHeld = bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;
	bSkipped = false;
	Mode = EMode::Play;
	PlaySfx(TEXT("start"));
}

void USurvivorMiniGameWidget::TogglePause()
{
	if (Mode == EMode::Play)
	{
		Mode = EMode::Pause;
	}
	else if (Mode == EMode::Pause)
	{
		Mode = EMode::Play;
	}
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;
}

void USurvivorMiniGameWidget::SkipShift()
{
	// 점수 0으로 정산 → 0점으로 끝낸 것과 같은 최소 보상 (돈 고정 + 말솜씨 +1). 플레이 횟수에는 안 센다.
	ResetRound();
	bSkipped = true;
	Mode = EMode::Result;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	FMiniGameResult Result;
	Result.Score = 0;
	Result.bSkipped = true;
	FinalReward = FinishGame(Result);
}

void USurvivorMiniGameWidget::DoButton(EButtonAction Action)
{
	PlaySfx(TEXT("click"));
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	switch (Action)
	{
	case EButtonAction::Start:
		// 처음이면 연습 판부터 직접 해 본다
		if (PlayCount == 0)
		{
			StartPractice();
		}
		else
		{
			StartRound();
		}
		break;
	case EButtonAction::StartPractice:
		StartPractice();
		break;
	case EButtonAction::SkipPractice:
		StartRound();
		break;
	case EButtonAction::ShowCards:
		TutorialPage = 0;
		Mode = EMode::Tutorial;
		break;
	case EButtonAction::NextPage:
		if (Mode == EMode::Practice)
		{
			SetPracticeStep(PracticeStep + 1); // 설명 단계의 "알겠어요"
		}
		else
		{
			ChangeCardPage(1);
		}
		break;
	case EButtonAction::PrevPage:
		ChangeCardPage(-1);
		break;
	case EButtonAction::CloseCards:
		Mode = EMode::Title;
		break;
	case EButtonAction::SkipShift:
		if (PlayCount > 0)
		{
			SkipShift();
		}
		break;
	case EButtonAction::Resume:
		TogglePause();
		break;
	case EButtonAction::Exit:
		ExitGame();
		break;
	case EButtonAction::Choose0:
		ChooseSkill(0);
		break;
	case EButtonAction::Choose1:
		ChooseSkill(1);
		break;
	case EButtonAction::Choose2:
		ChooseSkill(2);
		break;
	}
}

void USurvivorMiniGameWidget::ChangeCardPage(int32 Delta)
{
	const int32 NewPage = TutorialPage + Delta;
	if (NewPage >= 3)
	{
		Mode = EMode::Title; // 마지막 장에서 다음 → 닫기
	}
	else
	{
		TutorialPage = FMath::Max(0, NewPage);
	}
}

void USurvivorMiniGameWidget::PressPrimaryButton()
{
	for (const FOverlayButton& Button : GetOverlayButtons())
	{
		if (Button.bPrimary)
		{
			DoButton(Button.Action);
			return;
		}
	}
}

void USurvivorMiniGameWidget::AddPop(const FVector2f& Pos, const FString& Text, const FLinearColor& Color)
{
	Pops.Add({ Pos, Text, Color, 1.1f });
}

void USurvivorMiniGameWidget::AddScraps(const FVector2f& Pos, int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float Speed = 160.f + FMath::FRand() * 220.f;
		FScrap& Scrap = Scraps.AddDefaulted_GetRef();
		Scrap.Pos = Pos;
		Scrap.Vel = FVector2f(FMath::Cos(Angle) * Speed, FMath::Sin(Angle) * Speed - 120.f);
		Scrap.Rot = FMath::FRand() * 6.f;
		Scrap.Spin = (FMath::FRand() - 0.5f) * 14.f;
		Scrap.Variant = FMath::RandRange(0, 3);
		Scrap.Life = 0.55f;
	}
}

void USurvivorMiniGameWidget::SpawnEnemy(EEnemyType Type, const FVector2f* Near)
{
	using namespace SurvivorGame;

	// 가장자리 네 곳 중 하나에서 들어온다. 단체 손님은 첫 손님 곁에서
	FVector2f Pos;
	if (Near)
	{
		Pos = *Near + FVector2f((FMath::FRand() - 0.5f) * 80.f, (FMath::FRand() - 0.5f) * 80.f);
	}
	else
	{
		switch (FMath::RandRange(0, 3))
		{
		case 0:  Pos = FVector2f(FMath::FRandRange(AreaX + 30.f, AreaRight - 30.f), AreaY + 15.f); break;
		case 1:  Pos = FVector2f(AreaRight - 5.f, FMath::FRandRange(AreaY + 30.f, AreaBottom - 30.f)); break;
		case 2:  Pos = FVector2f(FMath::FRandRange(AreaX + 30.f, AreaRight - 30.f), AreaBottom - 20.f); break;
		default: Pos = FVector2f(AreaX + 5.f, FMath::FRandRange(AreaY + 30.f, AreaBottom - 30.f)); break;
		}
	}

	const FEnemyDef& Def = EnemyDefs[static_cast<int32>(Type)];
	FEnemy& Enemy = Enemies.AddDefaulted_GetRef();
	Enemy.Pos = Pos;
	Enemy.Type = Type;
	Enemy.R = Def.Radius;
	// 25초마다 인내심 +1 (HTML 그대로)
	Enemy.MaxHp = Enemy.Hp = Def.BaseHp + FMath::FloorToInt(T / 25.f);
	Enemy.Age = FMath::FRand() * 6.f;
	Enemy.Action = 1.f + FMath::FRand();
	// 걷기를 무작위로 시작해 같은 손님끼리 발이 똑같이 맞지 않게
	Enemy.Walk = FMath::FRand();
	Enemy.bFlip = FMath::RandBool();
}

void USurvivorMiniGameWidget::SpawnWave()
{
	// 시간이 지날수록 손님 종류가 늘어난다 (HTML wave)
	TArray<EEnemyType, TInlineAllocator<4>> Types = { EEnemyType::Drunk, EEnemyType::Group };
	if (T >= 10.f)
	{
		Types.Add(EEnemyType::Angry);
	}
	if (T >= 18.f)
	{
		Types.Add(EEnemyType::Coupon);
	}
	const EEnemyType Type = Types[FMath::RandRange(0, Types.Num() - 1)];
	SpawnEnemy(Type);
	if (Type == EEnemyType::Group)
	{
		const FVector2f First = Enemies.Last().Pos;
		SpawnEnemy(Type, &First);
		SpawnEnemy(Type, &First);
	}
}

void USurvivorMiniGameWidget::HitEnemy(FEnemy& Enemy, int32 InDamage, const FVector2f& Push)
{
	using namespace SurvivorGame;

	if (Enemy.Hp <= 0)
	{
		return;
	}
	Enemy.Hp -= InDamage;
	Enemy.Flash = 0.12f;
	Enemy.Knock += Push * 190.f;
	AddScraps(Enemy.Pos - FVector2f(0.f, 20.f), 2);
	PlaySfx(TEXT("hit"));

	if (Enemy.Hp <= 0)
	{
		++Exits;
		FGem& Gem = Gems.AddDefaulted_GetRef();
		Gem.Pos = FVector2f(FMath::Clamp(Enemy.Pos.X, AreaX, AreaRight), FMath::Clamp(Enemy.Pos.Y, AreaY, AreaBottom));
		Gem.Value = Enemy.Type == EEnemyType::Angry ? 2 : 1;
		Gem.Seed = FMath::FRand() * 6.f;

		FDeparture& Leave = Departures.AddDefaulted_GetRef();
		Leave.Pos = Enemy.Pos;
		Leave.Type = Enemy.Type;
		Leave.R = Enemy.R;
		Leave.Walk = Enemy.Walk;
		Leave.Dir = Enemy.Pos.X < CenterX ? -1.f : 1.f;
		PlaySfx(TEXT("exit"));
		if (Exits % 5 == 1)
		{
			AddPop(Enemy.Pos - FVector2f(0.f, 35.f), TEXT("다른 데 갈 거야!"), Hex(TEXT("ffcf85")));
		}
	}
}

void USurvivorMiniGameWidget::Stress(float Amount)
{
	using namespace SurvivorGame;

	if (HeroHurt > 0.f)
	{
		return;
	}
	HeroHurt = HurtInvincible;
	PlaySfx(TEXT("hurt"));

	// 연습 판에서는 멘탈이 깎이지 않는다 (맞는 느낌만)
	if (Mode == EMode::Practice)
	{
		AddPop(HeroPos - FVector2f(0.f, 120.f), TEXT("연습이라 괜찮아요"), Hex(TEXT("ffcf85")));
		return;
	}

	const float Lost = Amount * (1.f - 0.2f * ZenLevel);
	Mental = FMath::Max(0.f, Mental - Lost);
	AddPop(HeroPos - FVector2f(0.f, 120.f), FString::Printf(TEXT("멘탈 −%d"), FMath::RoundToInt(Lost)), Hex(TEXT("ff5c6c")));
	if (Mental <= 0.f)
	{
		BeginFailing();
	}
}

void USurvivorMiniGameWidget::OpenLevelUp()
{
	// 아직 최대가 아닌 스킬 중 3개를 무작위로
	TArray<ESkill> Pool;
	for (int32 i = 0; i < static_cast<int32>(ESkill::Num); ++i)
	{
		if (Taken[i] < SurvivorGame::Skills[i].Max)
		{
			Pool.Add(static_cast<ESkill>(i));
		}
	}
	Choices.Reset();
	while (Choices.Num() < 3 && Pool.Num() > 0)
	{
		Choices.Add(Pool[FMath::RandRange(0, Pool.Num() - 1)]);
		Pool.Remove(Choices.Last());
	}
	if (Choices.Num() == 0)
	{
		PendingLevels = 0;
		return;
	}

	ResumeMode = Mode;
	Mode = EMode::LevelUp;
	bUpHeld = bDownHeld = bLeftHeld = bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;
	PlaySfx(TEXT("levelup"));
}

void USurvivorMiniGameWidget::ChooseSkill(int32 Index)
{
	if (Mode != EMode::LevelUp || !Choices.IsValidIndex(Index))
	{
		return;
	}
	const ESkill Skill = Choices[Index];
	ApplySkill(Skill);
	++Taken[static_cast<int32>(Skill)];
	--PendingLevels;

	if (PendingLevels > 0)
	{
		Mode = ResumeMode;
		OpenLevelUp();
		return;
	}
	Mode = ResumeMode;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	// 연습 판: 스킬을 골라 봤으면 다음 설명으로
	if (Mode == EMode::Practice && PracticeStep == SurvivorGame::PracticeReceiptStep)
	{
		SetPracticeStep(SurvivorGame::PracticeExplainStep);
	}
}

void USurvivorMiniGameWidget::ApplySkill(ESkill Skill)
{
	switch (Skill)
	{
	case ESkill::Multi:  ++ShotCount; break;
	case ESkill::Fast:   ShotCooldown *= 0.75f; break;
	case ESkill::Manual: ++ManualCount; break;
	case ESkill::Boss:   ++BossLevel; BossTimer = 0.f; break;
	case ESkill::Zen:
		++ZenLevel;
		Mental = FMath::Min(100.f, Mental + 25.f);
		PlaySfx(TEXT("heal"));
		break;
	case ESkill::Speed:  MoveSpeed *= 1.2f; break;
	case ESkill::Magnet: Magnet *= 1.6f; break;
	case ESkill::Heal:
		Mental = FMath::Min(100.f, Mental + 45.f);
		++Damage;
		PlaySfx(TEXT("heal"));
		break;
	default: break;
	}
}

void USurvivorMiniGameWidget::BeginFailing()
{
	Mode = EMode::Failing;
	FailT = 0.f;
	Coupons.Reset();
	PlaySfx(TEXT("fail"));
}

void USurvivorMiniGameWidget::BeginEnding()
{
	Mode = EMode::Ending;
	EndingT = 0.f;
	EndingStart = HeroPos;
	Coupons.Reset();
	PlaySfx(TEXT("shift_end"));
	AddPop(HeroPos - FVector2f(0.f, 150.f), TEXT("근무 종료. 이제 손님입니다."), SurvivorGame::Hex(TEXT("ffb340")));
}

void USurvivorMiniGameWidget::FinishShift(bool bInSurvived)
{
	Mode = EMode::Result;
	bSurvived = bInSurvived;
	bUpHeld = bDownHeld = bLeftHeld = bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	FMiniGameResult Result;
	Result.Score = Exits;
	FinalReward = FinishGame(Result);
}

// ---------------------------------------------------------------------------
// 연습 판
//   시간 제한 · 멘탈 감소 없이 직접 해 본다.
//   0 움직이기 → 1 자동 응대로 취객 3명 퇴점 → 2 영수증 주워 레벨업 → 스킬 고르기
//   → 3 손님 · 멘탈 설명 → 4 준비 끝
// ---------------------------------------------------------------------------

void USurvivorMiniGameWidget::StartPractice()
{
	ResetRound();
	bUpHeld = bDownHeld = bLeftHeld = bRightHeld = false;
	bSkipped = false;
	Mode = EMode::Practice;
	// 연습 손님 3명이 떨군 영수증 3장이면 바로 레벨업
	XpNeed = 3;
	SetPracticeStep(SurvivorGame::PracticeMoveStep);
}

void USurvivorMiniGameWidget::SetPracticeStep(int32 Step)
{
	using namespace SurvivorGame;

	PracticeStep = Step;
	PracticeStepTime = 0.f;
	PracticeMoved = 0.f;
	PracticeExitBase = Exits;

	if (Step == PracticeApologyStep)
	{
		// 주인공 둘레 세 방향에서 취객이 천천히 다가온다
		for (int32 i = 0; i < PracticeExitGoal; ++i)
		{
			const float Angle = -UE_HALF_PI + (i - 1) * 1.2f;
			const FVector2f Pos(FMath::Clamp(HeroPos.X + FMath::Cos(Angle) * 600.f, AreaX + 40.f, AreaRight - 40.f),
				FMath::Clamp(HeroPos.Y + FMath::Sin(Angle) * 600.f, AreaY + 30.f, AreaBottom - 30.f));
			SpawnEnemy(EEnemyType::Drunk, &Pos);
		}
	}
}

void USurvivorMiniGameWidget::UpdatePractice(float Dt)
{
	using namespace SurvivorGame;

	PracticeStepTime += Dt;
	switch (PracticeStep)
	{
	case PracticeMoveStep:
		if (PracticeMoved >= PracticeMoveGoal)
		{
			SetPracticeStep(PracticeApologyStep);
		}
		break;
	case PracticeApologyStep:
		if (Exits - PracticeExitBase >= PracticeExitGoal)
		{
			SetPracticeStep(PracticeReceiptStep);
		}
		break;
	case PracticeReceiptStep:
		// 레벨업 카드에서 고르면 ChooseSkill이 다음 단계로 넘긴다.
		// 영수증을 다 줍기 전에 손님이 모자라면(밀려서 겹침 등) 한 명 더 보낸다
		if (Gems.Num() == 0 && Enemies.Num() == 0 && Xp < XpNeed && PendingLevels == 0)
		{
			const FVector2f Pos(FMath::Clamp(HeroPos.X + 500.f, AreaX + 40.f, AreaRight - 40.f), HeroPos.Y);
			SpawnEnemy(EEnemyType::Drunk, &Pos);
		}
		break;
	case PracticeExplainStep:
		break; // 키 입력이나 버튼으로만 넘어간다
	default:
		if (PracticeStepTime >= PracticeFinishDelay)
		{
			StartRound();
		}
		break;
	}
}

// ---------------------------------------------------------------------------
// 진행 (HTML update)
// ---------------------------------------------------------------------------

void USurvivorMiniGameWidget::Update(float Dt)
{
	using namespace SurvivorGame;

	const bool bPractice = Mode == EMode::Practice;
	if (!bPractice)
	{
		T = FMath::Min(Duration, T + Dt);
		if (T >= Duration)
		{
			BeginEnding();
			return;
		}
		// 마지막 10초: 손님이 한꺼번에 몰려온다
		if (T >= Duration - WarningSeconds && !bRush)
		{
			bRush = true;
			PlaySfx(TEXT("rush"));
			for (int32 i = 0; i < 4; ++i)
			{
				SpawnWave();
			}
		}
	}

	// ---- 주인공 이동 ----
	HeroHurt = FMath::Max(0.f, HeroHurt - Dt);
	const float Dx = (bRightHeld ? 1.f : 0.f) - (bLeftHeld ? 1.f : 0.f);
	const float Dy = (bDownHeld ? 1.f : 0.f) - (bUpHeld ? 1.f : 0.f);
	const float Len = FMath::Max(1.f, FMath::Sqrt(Dx * Dx + Dy * Dy));
	const FVector2f Before = HeroPos;
	HeroPos.X = FMath::Clamp(HeroPos.X + Dx / Len * MoveSpeed * Dt, AreaX + HeroR, AreaRight - HeroR);
	HeroPos.Y = FMath::Clamp(HeroPos.Y + Dy / Len * MoveSpeed * Dt, AreaY + HeroR, AreaBottom - HeroR);
	PracticeMoved += FVector2f::Distance(Before, HeroPos);
	// 위로 가면 뒷모습, 아래로 가면 앞모습. 좌우로만 움직이면 보던 쪽을 유지하고 좌우 반전만 바꾼다
	bHeroMoving = Dx != 0.f || Dy != 0.f;
	if (Dy < 0.f) { bHeroBack = true; }
	else if (Dy > 0.f) { bHeroBack = false; }
	if (Dx != 0.f) { bHeroFlip = Dx > 0.f; }
	StepWalk(HeroWalk, bHeroMoving ? MoveSpeed : 0.f, Dt);

	// ---- 손님 입장 (연습 판은 단계에서 직접 보낸다) ----
	if (!bPractice)
	{
		SpawnTimer -= Dt;
		if (SpawnTimer <= 0.f)
		{
			if (Enemies.Num() < MaxEnemies)
			{
				SpawnWave();
				if (bRush)
				{
					SpawnWave();
				}
			}
			SpawnTimer = (bRush ? 0.48f : FMath::Max(0.6f, 1.3f - T * 0.014f)) * SpawnIntervalScale;
		}
	}

	// ---- 죄송합니다: 가까운 손님 N명에게 자동 발사 ----
	FireTimer -= Dt;
	if (FireTimer <= 0.f && Enemies.Num() > 0)
	{
		TArray<const FEnemy*> Targets;
		for (const FEnemy& Enemy : Enemies)
		{
			if (Enemy.Hp > 0)
			{
				Targets.Add(&Enemy);
			}
		}
		const FVector2f From = HeroPos;
		Targets.Sort([&From](const FEnemy& A, const FEnemy& B) { return FVector2f::DistSquared(A.Pos, From) < FVector2f::DistSquared(B.Pos, From); });
		for (int32 i = 0; i < FMath::Min(ShotCount, Targets.Num()); ++i)
		{
			const FVector2f Dir = (Targets[i]->Pos - HeroPos).GetSafeNormal();
			Shots.Add({ HeroPos - FVector2f(0.f, 30.f), Dir * ShotSpeed, 1.5f });
		}
		FireTimer = ShotCooldown;
	}
	for (FShot& Shot : Shots)
	{
		Shot.Pos += Shot.Vel * Dt;
		Shot.Life -= Dt;
		for (FEnemy& Enemy : Enemies)
		{
			if (Enemy.Hp > 0 && FVector2f::Distance(Enemy.Pos, Shot.Pos) < Enemy.R + 15.f)
			{
				HitEnemy(Enemy, Damage, Shot.Vel / ShotSpeed);
				Shot.Life = 0.f;
				break;
			}
		}
	}
	Shots.RemoveAll([](const FShot& Shot) { return Shot.Life <= 0.f; });

	// ---- 매뉴얼: 주인공 둘레를 돌며 0.2초마다 닿은 손님을 민다 ----
	ManualAngle += 3.2f * Dt;
	ManualTimer -= Dt;
	if (ManualCount > 0 && ManualTimer <= 0.f)
	{
		for (int32 i = 0; i < ManualCount; ++i)
		{
			const float A = ManualAngle + i * UE_TWO_PI / ManualCount;
			const FVector2f At = HeroPos + FVector2f(FMath::Cos(A), FMath::Sin(A)) * ManualRadius;
			for (FEnemy& Enemy : Enemies)
			{
				if (FVector2f::Distance(Enemy.Pos, At) < Enemy.R + 30.f)
				{
					HitEnemy(Enemy, 2, (Enemy.Pos - HeroPos).GetSafeNormal() * 2.f);
				}
			}
		}
		ManualTimer = 0.2f;
	}

	// ---- 점장 호출: 7초마다 광역 응대 + 근처 쿠폰 제거 ----
	BossTimer -= Dt;
	if (BossLevel > 0 && BossTimer <= 0.f)
	{
		const float Radius = 220.f + BossLevel * 50.f;
		Rings.Add({ HeroPos, Radius, 1.f });
		AddPop(HeroPos - FVector2f(0.f, 160.f), TEXT("규정대로 하세요!"), Hex(TEXT("ffb340")));
		for (FEnemy& Enemy : Enemies)
		{
			if (FVector2f::Distance(Enemy.Pos, HeroPos) < Radius)
			{
				HitEnemy(Enemy, 4 + BossLevel * 2, (Enemy.Pos - HeroPos).GetSafeNormal() * 3.f);
			}
		}
		const FVector2f Center = HeroPos;
		Coupons.RemoveAll([&Center, Radius](const FCouponShot& Coupon) { return FVector2f::Distance(Coupon.Pos, Center) <= Radius; });
		BossTimer = BossCallInterval;
	}

	// ---- 손님 ----
	const EMode ModeBefore = Mode;
	for (FEnemy& Enemy : Enemies)
	{
		if (Enemy.Hp <= 0)
		{
			continue;
		}
		Enemy.Age += Dt;
		Enemy.Flash = FMath::Max(0.f, Enemy.Flash - Dt);
		Enemy.Action -= Dt;

		const FVector2f ToHero = HeroPos - Enemy.Pos;
		const float Dist = FMath::Max(1.f, ToHero.Size());
		FVector2f Dir = ToHero / Dist;
		float Speed = 95.f + T * 0.65f;

		switch (Enemy.Type)
		{
		case EEnemyType::Drunk:
		{
			// 비틀비틀: 방향이 좌우로 흔들린다
			const float A = FMath::Atan2(ToHero.Y, ToHero.X) + FMath::Sin(Enemy.Age * 4.f) * 0.8f;
			Dir = FVector2f(FMath::Cos(A), FMath::Sin(A));
			break;
		}
		case EEnemyType::Coupon:
			// 240~350 거리를 유지하며 쿠폰을 던진다. 투척 모션 동안은 서 있고, 손을 떠나는 프레임에 쿠폰이 나간다
			Speed = Dist < 240.f ? -35.f : Dist > 350.f ? 80.f : 0.f;
			if (Enemy.ThrowT < 0.f && Enemy.Action <= 0.f)
			{
				Enemy.ThrowT = 0.f;
				Enemy.bThrown = false;
				Enemy.Action = 2.8f;
			}
			if (Enemy.ThrowT >= 0.f)
			{
				Speed = 0.f;
				Enemy.ThrowT += Dt;
				if (!Enemy.bThrown && Enemy.ThrowT >= CouponReleaseTime)
				{
					Enemy.bThrown = true;
					Coupons.Add({ Enemy.Pos - FVector2f(0.f, 45.f), Dir * CouponSpeed, 4.f, FMath::RandRange(0, 2) });
				}
				if (Enemy.ThrowT >= Length(ThrowFrames))
				{
					Enemy.ThrowT = -1.f;
				}
			}
			break;
		case EEnemyType::Angry:
			// 0.65초 멈칫(붉게 떨림) → 0.7초 돌진
			if (Enemy.Action <= 0.f)
			{
				Enemy.DashDir = Dir;
				Enemy.Dash = 0.7f;
				Enemy.Action = 3.2f;
			}
			if (Enemy.Dash > 0.f)
			{
				Enemy.Dash -= Dt;
				Dir = Enemy.DashDir;
				Speed = 350.f;
			}
			else if (Enemy.Action < 0.65f)
			{
				Speed = 0.f;
			}
			break;
		default:
			break;
		}
		if (bPractice)
		{
			Speed *= PracticeEnemySpeed;
		}

		// 실제로 움직이는 방향으로 그린다 (뒷걸음질이면 speed가 음수라 반대). 위로 가면 뒷모습
		const FVector2f Move = Dir * Speed;
		if (FMath::Abs(Move.X) > 15.f) { Enemy.bFlip = Move.X > 0.f; }
		if (FMath::Abs(Move.Y) > 15.f) { Enemy.bBack = Move.Y < 0.f; }
		StepWalk(Enemy.Walk, Speed, Dt);

		Enemy.Pos += (Move + Enemy.Knock) * Dt;
		Enemy.Knock *= FMath::Max(0.f, 1.f - 8.f * Dt);
		Enemy.Pos.X = FMath::Clamp(Enemy.Pos.X, AreaX, AreaRight);
		Enemy.Pos.Y = FMath::Clamp(Enemy.Pos.Y, AreaY, AreaBottom);

		if (FVector2f::Distance(HeroPos, Enemy.Pos) < Enemy.R + HeroR)
		{
			Stress(16.f);
			if (Mode != ModeBefore)
			{
				return;
			}
		}
	}
	Enemies.RemoveAll([](const FEnemy& Enemy) { return Enemy.Hp <= 0; });

	// 손님끼리 겹치지 않게 밀어낸다
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		for (int32 j = i + 1; j < Enemies.Num(); ++j)
		{
			FEnemy& A = Enemies[i];
			FEnemy& B = Enemies[j];
			const FVector2f Delta = B.Pos - A.Pos;
			const float D = Delta.Size();
			const float R = A.R + B.R;
			if (D > 0.f && D < R)
			{
				const FVector2f Push = Delta / D * ((R - D) * 0.3f);
				A.Pos -= Push;
				B.Pos += Push;
			}
		}
	}

	// ---- 쿠폰 ----
	for (FCouponShot& Coupon : Coupons)
	{
		Coupon.Pos += Coupon.Vel * Dt;
		Coupon.Life -= Dt;
		if (FVector2f::Distance(Coupon.Pos, HeroPos - FVector2f(0.f, 20.f)) < HeroR + 13.f)
		{
			Coupon.Life = 0.f;
			Stress(12.f);
			if (Mode != ModeBefore)
			{
				return;
			}
		}
	}
	Coupons.RemoveAll([](const FCouponShot& Coupon) { return Coupon.Life <= 0.f; });

	// ---- 영수증: 반경 안이면 끌려오고 닿으면 줍는다 ----
	for (FGem& Gem : Gems)
	{
		const FVector2f Delta = HeroPos - Gem.Pos;
		const float D = Delta.Size();
		if (D < Magnet && D > 0.f)
		{
			Gem.Pos += Delta / D * FMath::Min(D, GemPullSpeed * Dt);
		}
		if (FVector2f::Distance(HeroPos, Gem.Pos) < HeroR + 12.f)
		{
			Xp += Gem.Value;
			Gem.Value = 0;
			PlaySfx(TEXT("pickup"));
		}
	}
	Gems.RemoveAll([](const FGem& Gem) { return Gem.Value <= 0; });

	while (Xp >= XpNeed)
	{
		Xp -= XpNeed;
		++Level;
		XpNeed = FMath::RoundToInt(XpNeed * 1.45f) + 2;
		++PendingLevels;
	}

	UpdateEffects(Dt);
	if (bPractice)
	{
		UpdatePractice(Dt);
	}
	if (PendingLevels > 0 && (Mode == EMode::Play || Mode == EMode::Practice))
	{
		OpenLevelUp();
	}
}

void USurvivorMiniGameWidget::UpdateEffects(float Dt)
{
	for (FPopText& Pop : Pops)
	{
		Pop.Life -= Dt;
		Pop.Pos.Y -= 35.f * Dt;
	}
	Pops.RemoveAll([](const FPopText& Pop) { return Pop.Life <= 0.f; });

	for (FCallRing& Ring : Rings)
	{
		Ring.Life -= Dt;
	}
	Rings.RemoveAll([](const FCallRing& Ring) { return Ring.Life <= 0.f; });

	// 퇴점: 가까운 쪽 화면 밖으로 걸어 나간다 (옆모습이 없어 정면 걷기를 그대로 쓴다)
	for (FDeparture& Leave : Departures)
	{
		Leave.Life -= Dt;
		Leave.Pos.X += Leave.Dir * 220.f * Dt;
		SurvivorGame::StepWalk(Leave.Walk, 220.f, Dt);
	}
	Departures.RemoveAll([](const FDeparture& Leave) { return Leave.Life <= 0.f; });

	for (FScrap& Scrap : Scraps)
	{
		Scrap.Life -= Dt;
		Scrap.Pos += Scrap.Vel * Dt;
		Scrap.Vel.Y += 900.f * Dt;
		Scrap.Rot += Scrap.Spin * Dt;
	}
	Scraps.RemoveAll([](const FScrap& Scrap) { return Scrap.Life <= 0.f; });
}

void USurvivorMiniGameWidget::UpdateEnding(float Dt)
{
	using namespace SurvivorGame;

	// 칼퇴: 제자리에서 조끼 벗는 모션 → 아래쪽 출구로 걸어 나간다 (아래로 걸으니 앞모습)
	EndingT += Dt;
	const float K = FMath::Clamp((EndingT - EndingWalkStart) / EndingWalkTime, 0.f, 1.f);
	const FVector2f Before = HeroPos;
	const FVector2f Door(CenterX, AreaBottom + 60.f);
	HeroPos = EndingStart + (Door - EndingStart) * K;
	StepWalk(HeroWalk, FVector2f::Distance(Before, HeroPos) / FMath::Max(Dt, 1e-4f), Dt);
	bHeroFlip = false;
	UpdateEffects(Dt);
	if (EndingT >= EndingTotal)
	{
		FinishShift(true);
	}
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------

void USurvivorMiniGameWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 프레임이 튀어도 한 번에 0.05초까지만 진행한다 (HTML 그대로)
	const float Dt = FMath::Min(InDeltaTime, 0.05f);
	switch (Mode)
	{
	case EMode::Play:
	case EMode::Practice:
		Update(Dt);
		break;
	case EMode::Ending:
		UpdateEnding(Dt);
		break;
	case EMode::Failing:
		FailT += Dt;
		UpdateEffects(Dt);
		if (FailT >= SurvivorGame::FailingTotal)
		{
			FinishShift(false);
		}
		break;
	case EMode::Title:
	case EMode::Result:
		UpdateEffects(Dt);
		break;
	default:
		break; // 레벨업 · 일시정지 · 설명 카드 중에는 시간이 멈춘다
	}
}

FReply USurvivorMiniGameWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	if (Mode == EMode::Tutorial && !InKeyEvent.IsRepeat())
	{
		if (Key == EKeys::Left || Key == EKeys::A) { ChangeCardPage(-1); return FReply::Handled(); }
		if (Key == EKeys::Right || Key == EKeys::D) { ChangeCardPage(1); return FReply::Handled(); }
		if (Key == EKeys::Escape) { Mode = EMode::Title; return FReply::Handled(); }
	}

	// 레벨업 카드는 숫자 1 / 2 / 3
	if (Mode == EMode::LevelUp && !InKeyEvent.IsRepeat())
	{
		const int32 Index = Key == EKeys::One || Key == EKeys::NumPadOne ? 0
			: Key == EKeys::Two || Key == EKeys::NumPadTwo ? 1
			: Key == EKeys::Three || Key == EKeys::NumPadThree ? 2 : INDEX_NONE;
		if (Index != INDEX_NONE)
		{
			PlaySfx(TEXT("click"));
			ChooseSkill(Index);
			return FReply::Handled();
		}
	}

	if (Key == EKeys::W || Key == EKeys::Up) { bUpHeld = true; return FReply::Handled(); }
	if (Key == EKeys::S || Key == EKeys::Down) { bDownHeld = true; return FReply::Handled(); }
	if (Key == EKeys::A || Key == EKeys::Left) { bLeftHeld = true; return FReply::Handled(); }
	if (Key == EKeys::D || Key == EKeys::Right) { bRightHeld = true; return FReply::Handled(); }
	if (InKeyEvent.IsRepeat())
	{
		return FReply::Handled();
	}
	if (Key == EKeys::P || Key == EKeys::Escape)
	{
		TogglePause();
		return FReply::Handled();
	}
	if (Key == EKeys::SpaceBar || Key == EKeys::Enter)
	{
		PressPrimaryButton();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply USurvivorMiniGameWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::W || Key == EKeys::Up) { bUpHeld = false; return FReply::Handled(); }
	if (Key == EKeys::S || Key == EKeys::Down) { bDownHeld = false; return FReply::Handled(); }
	if (Key == EKeys::A || Key == EKeys::Left) { bLeftHeld = false; return FReply::Handled(); }
	if (Key == EKeys::D || Key == EKeys::Right) { bRightHeld = false; return FReply::Handled(); }
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply USurvivorMiniGameWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PressedButton = FindButtonAt(ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	}
	// 클릭해도 키보드 포커스를 잃지 않게 붙잡아 둔다
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply USurvivorMiniGameWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && PressedButton != INDEX_NONE)
	{
		// 누른 버튼 위에서 뗐을 때만 실행한다
		const int32 Released = FindButtonAt(ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition()));
		const TArray<FOverlayButton> Buttons = GetOverlayButtons();
		const bool bSame = Released == PressedButton && Buttons.IsValidIndex(Released);
		PressedButton = INDEX_NONE;
		if (bSame)
		{
			DoButton(Buttons[Released].Action);
		}
	}
	return FReply::Handled();
}

FReply USurvivorMiniGameWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	HoveredButton = FindButtonAt(ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void USurvivorMiniGameWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnFocusLost(InFocusEvent);

	// 창을 벗어나면 키를 뗀 것으로 보고 멈춘다 (HTML의 blur 처리)
	bUpHeld = bDownHeld = bLeftHeld = bRightHeld = false;
	if (Mode == EMode::Play)
	{
		TogglePause();
	}
}

// ---------------------------------------------------------------------------
// 화면
// ---------------------------------------------------------------------------

void USurvivorMiniGameWidget::GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset)
{
	using namespace SurvivorGame;
	OutScale = FMath::Max(0.01f, FMath::Min(LocalSize.Y / ScreenH, LocalSize.X / ScreenW));
	OutOffset = (LocalSize - FVector2f(ScreenW, ScreenH) * OutScale) * 0.5f;
}

FVector2f USurvivorMiniGameWidget::ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos)
{
	float Scale;
	FVector2f Offset;
	GetCanvasTransform(Geometry.GetLocalSize(), Scale, Offset);
	return (Geometry.AbsoluteToLocal(ScreenPos) - Offset) / Scale;
}

TArray<USurvivorMiniGameWidget::FOverlayButton> USurvivorMiniGameWidget::GetOverlayButtons() const
{
	using namespace SurvivorGame;

	TArray<FOverlayButton> Buttons;
	auto Add = [&Buttons](float X, float Y, float W, const TCHAR* Label, EButtonAction Action, bool bPrimary)
	{
		FOverlayButton& Button = Buttons.AddDefaulted_GetRef();
		Button.X = X;
		Button.Y = Y;
		Button.W = W;
		Button.H = bPrimary ? 84.f : 60.f;
		Button.Label = Label;
		Button.Action = Action;
		Button.bPrimary = bPrimary;
	};
	const float CX = CenterX;

	switch (Mode)
	{
	case EMode::Title:
		if (PlayCount == 0)
		{
			Add(CX, 900.f, 360.f, TEXT("출근 도장 찍기"), EButtonAction::Start, true);
		}
		else
		{
			// 한 번 해 본 뒤에는 건너뛰기 · 연습 · 설명을 연다
			Add(CX, 830.f, 360.f, TEXT("출근 도장 찍기"), EButtonAction::Start, true);
			Add(CX, 930.f, 320.f, TEXT("대충 일하고 퇴근"), EButtonAction::SkipShift, false);
			Add(CX - 130.f, 1010.f, 240.f, TEXT("연습하기"), EButtonAction::StartPractice, false);
			Add(CX + 130.f, 1010.f, 240.f, TEXT("설명 보기"), EButtonAction::ShowCards, false);
		}
		break;
	case EMode::Practice:
		if (PracticeStep == PracticeExplainStep)
		{
			Add(CX, 930.f, 320.f, TEXT("알겠어요"), EButtonAction::NextPage, true);
		}
		// 연습 중에는 아래쪽 띠에 작게
		Add(ScreenW - 200.f, 1195.f, 260.f, TEXT("연습 건너뛰기"), EButtonAction::SkipPractice, false);
		break;
	case EMode::Tutorial:
		if (TutorialPage > 0)
		{
			Add(CX - 180.f, 960.f, 220.f, TEXT("이전"), EButtonAction::PrevPage, false);
		}
		Add(TutorialPage > 0 ? CX + 80.f : CX, 960.f, 280.f, TutorialPage < 2 ? TEXT("다음") : TEXT("닫기"), EButtonAction::NextPage, true);
		if (TutorialPage < 2)
		{
			Add(CX, 1050.f, 200.f, TEXT("닫기"), EButtonAction::CloseCards, false);
		}
		break;
	case EMode::Pause:
		Add(CX, 780.f, 340.f, TEXT("근무 복귀"), EButtonAction::Resume, true);
		break;
	case EMode::Result:
		Add(CX, 1010.f, 340.f, TEXT("퇴근하기"), EButtonAction::Exit, true);
		break;
	case EMode::LevelUp:
		// 레벨업 카드 3장 (가로로 나란히). 첫 장이 Enter/Space
		for (int32 i = 0; i < Choices.Num(); ++i)
		{
			FOverlayButton& Card = Buttons.AddDefaulted_GetRef();
			Card.X = CX + (i - (Choices.Num() - 1) * 0.5f) * 600.f;
			Card.Y = 680.f;
			Card.W = 540.f;
			Card.H = 470.f;
			Card.Action = static_cast<EButtonAction>(static_cast<int32>(EButtonAction::Choose0) + i);
			Card.bPrimary = i == 0;
			Card.bCard = true;
		}
		break;
	default:
		break;
	}
	return Buttons;
}

int32 USurvivorMiniGameWidget::FindButtonAt(const FVector2f& ScreenPos) const
{
	const TArray<FOverlayButton> Buttons = GetOverlayButtons();
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FOverlayButton& B = Buttons[i];
		if (FMath::Abs(ScreenPos.X - B.X) <= B.W * 0.5f && FMath::Abs(ScreenPos.Y - B.Y) <= B.H * 0.5f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 USurvivorMiniGameWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace SurvivorGame;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	float Scale;
	FVector2f Offset;
	GetCanvasTransform(AllottedGeometry.GetLocalSize(), Scale, Offset);

	// 화면 밖(가로세로 비율이 다를 때 남는 띠)은 어둡게
	FSurvivorPainter Back{ OutDrawElements, AllottedGeometry, BaseLayer, &SolidBrush };
	Back.Box(0.f, 0.f, AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Hex(TEXT("1b1714")));

	FSurvivorPainter P{ OutDrawElements, AllottedGeometry.MakeChild(FVector2f(ScreenW, ScreenH), FSlateLayoutTransform(Scale, Offset)), Back.Layer, &SolidBrush };
	PaintStore(P);
	PaintActors(P);
	PaintProjectiles(P);
	PaintHud(P);

	switch (Mode)
	{
	case EMode::Practice:
		PaintPractice(P);
		break;
	case EMode::LevelUp:
		PaintLevelUp(P);
		break;
	case EMode::Play:
	case EMode::Failing:
		break;
	case EMode::Ending:
		P.Brush(&DarkPanelBrush, CenterX - 420.f, 420.f, 840.f, 110.f);
		P.Text(TEXT("근무 끝! 저도 이제 손님인데요?"), CenterX, 475.f, 36.f, Hex(TEXT("ffcf85")), ESurvivorAlign::Center, true);
		break;
	default:
		PaintOverlay(P);
		break;
	}
	return P.Layer;
}

void USurvivorMiniGameWidget::PaintStore(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	// 벽 (위 · 아래 띠) → 바닥 타일 → 진열대 → 출구
	P.Box(0.f, 0.f, ScreenW, ScreenH, Hex(TEXT("e9dfcc")));
	P.Box(0.f, 0.f, ScreenW, AreaY - 30.f, Hex(TEXT("cfe0d2")));
	P.Brush(FindBrush(TEXT("floor")), AreaX, AreaY, AreaW, AreaH);
	// 바닥 테두리
	P.Box(AreaX - 6.f, AreaY - 6.f, AreaW + 12.f, 6.f, Ink());
	P.Box(AreaX - 6.f, AreaBottom, AreaW + 12.f, 6.f, Ink());
	P.Box(AreaX - 6.f, AreaY - 6.f, 6.f, AreaH + 12.f, Ink());
	P.Box(AreaRight, AreaY - 6.f, 6.f, AreaH + 12.f, Ink());

	// 진열대: 512칸 중 실제 그림은 x116~394, y100~410. 위쪽 벽에 쭉 붙여 세운다 (좌우 끝은 HUD 판이 덮는다)
	constexpr float ShelfCell = 230.f;
	const float ShelfTop = AreaY - 4.f - ShelfCell * (410.f / 512.f);
	for (float X = 70.f; X < ScreenW; X += 135.f)
	{
		P.Brush(FindBrush(TEXT("shelf")), X - ShelfCell * 0.5f, ShelfTop, ShelfCell, ShelfCell);
	}
	// 벽 아래 옅은 그림자 띠
	P.Box(AreaX, AreaY, AreaW, 12.f, WithAlpha(Ink(), 0.12f));

	// 출구: 문 아래끝이 화면 아래에 닿게 (문은 바닥 테두리를 살짝 덮는다)
	constexpr float ExitCell = 260.f;
	P.Brush(FindBrush(TEXT("exit")), CenterX - ExitCell * 0.5f, ScreenH - 4.f - ExitCell * (410.f / 512.f), ExitCell, ExitCell);
	P.Text(TEXT("퇴근"), CenterX, ScreenH - 4.f - ExitCell * (310.f / 512.f) - 22.f, 24.f, Hex(TEXT("80e6bc")), ESurvivorAlign::Center, true, 2);
}

void USurvivorMiniGameWidget::PaintEnemy(FSurvivorPainter& P, const FEnemy& Enemy) const
{
	using namespace SurvivorGame;

	const FEnemyDef& Def = EnemyDefs[static_cast<int32>(Enemy.Type)];
	const float Size = Def.DrawSize;
	const float FootY = Enemy.Pos.Y + Enemy.R * 0.9f;
	P.Brush(&CircleBrush, Enemy.Pos.X - Size * 0.22f, FootY - Size * 0.07f, Size * 0.44f, Size * 0.14f, FLinearColor(0.f, 0.f, 0.f, 0.25f));

	// 돌진 예고: 바닥 붉은 원 + 붉게 물들고 부르르 떤다
	const bool bWarn = Enemy.Type == EEnemyType::Angry && Enemy.Action < 0.65f && Enemy.Dash <= 0.f;
	float ShakeX = 0.f;
	if (bWarn)
	{
		const float R = Enemy.R + 14.f;
		P.Brush(&CircleBrush, Enemy.Pos.X - R, FootY - 6.f - R * 0.45f, R * 2.f, R * 0.9f, WithAlpha(Hex(TEXT("c0464f")), 0.55f));
		ShakeX = FMath::Sin(Enemy.Age * 70.f) * 3.f;
	}
	const FLinearColor Tint = bWarn ? FLinearColor(1.f, 0.6f, 0.6f, 1.f) : FLinearColor::White;

	FString Sheet;
	int32 Frame;
	if (Enemy.Type == EEnemyType::Coupon && Enemy.ThrowT >= 0.f)
	{
		Sheet = TEXT("coupon_throw");
		Frame = FrameAt(ThrowFrames, Enemy.ThrowT);
	}
	else
	{
		Sheet = FString::Printf(TEXT("%s_walk_%s"), Def.Art, Enemy.bBack ? TEXT("back") : TEXT("front"));
		Frame = WalkFrame(Enemy.Walk);
	}
	P.Sprite(FindBrush(*Sheet), Frame, 4, Enemy.Pos.X + ShakeX, FootY, Size, Enemy.bFlip, Tint);
	// 맞으면 0.12초 동안 하얗게 번쩍 (같은 칸의 흰 실루엣을 위에 겹친다)
	if (Enemy.Flash > 0.f)
	{
		P.Sprite(FindBrush(*(Sheet + TEXT("_flash"))), Frame, 4, Enemy.Pos.X + ShakeX, FootY, Size, Enemy.bFlip, WithAlpha(FLinearColor::White, Enemy.Flash / 0.12f * 0.85f));
	}

	// 머리 위 인내심 막대
	const float Top = FootY - Size * 0.92f;
	P.Box(Enemy.Pos.X - 25.f, Top - 1.f, 50.f, 8.f, Ink());
	P.Box(Enemy.Pos.X - 24.f, Top, 48.f, 6.f, Hex(TEXT("6b5550")));
	P.Box(Enemy.Pos.X - 24.f, Top, 48.f * FMath::Max(0.f, static_cast<float>(Enemy.Hp) / Enemy.MaxHp), 6.f, Hex(TEXT("ff8994")));
}

void USurvivorMiniGameWidget::PaintHero(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	const float FootY = HeroPos.Y + HeroR * 0.9f;
	P.Brush(&CircleBrush, HeroPos.X - HeroSize * 0.2f, FootY - HeroSize * 0.064f, HeroSize * 0.4f, HeroSize * 0.128f, FLinearColor(0.f, 0.f, 0.f, 0.28f));

	auto Draw = [&](const TCHAR* Sheet, int32 Frame, int32 Frames, bool bFlip)
	{
		P.Sprite(FindBrush(Sheet), Frame, Frames, HeroPos.X, FootY, HeroSize, bFlip);
	};

	if (Mode == EMode::Ending)
	{
		if (EndingT < EndingWalkStart)
		{
			Draw(TEXT("hero_success"), FrameAt(SuccessFrames, FMath::Max(0.f, EndingT - 0.15f)), 4, false);
		}
		else
		{
			Draw(TEXT("hero_walk_front"), WalkFrame(HeroWalk), 4, false);
		}
		return;
	}
	if (Mode == EMode::Failing || (Mode == EMode::Result && !bSurvived && !bSkipped))
	{
		Draw(TEXT("hero_failure"), FrameAt(FailureFrames, FailT), 3, bHeroFlip);
		return;
	}

	// 맞은 직후엔 아파하는 모션, 그 뒤 무적 시간 동안은 깜빡인다
	const float HurtT = HurtInvincible - HeroHurt;
	if (HeroHurt > 0.f && HurtT < Length(HurtFrames))
	{
		Draw(TEXT("hero_hurt"), FrameAt(HurtFrames, HurtT), 2, bHeroFlip);
		return;
	}
	if (HeroHurt > 0.f && FMath::FloorToInt(HeroHurt * 15.f) % 2 == 0)
	{
		return;
	}
	if (bHeroMoving)
	{
		Draw(bHeroBack ? TEXT("hero_walk_back") : TEXT("hero_walk_front"), WalkFrame(HeroWalk), 4, bHeroFlip);
	}
	else if (bHeroBack)
	{
		Draw(TEXT("hero_idle_back"), 0, 1, bHeroFlip);
	}
	else
	{
		// 해탈을 배운 뒤엔 멈춰 있을 때 해탈 얼굴
		Draw(ZenLevel > 0 ? TEXT("hero_zen") : TEXT("hero_idle_front"), 0, 1, bHeroFlip);
	}
}

void USurvivorMiniGameWidget::PaintActors(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	// 영수증과 점장 호출 고리는 바닥에 깔린다
	for (const FGem& Gem : Gems)
	{
		const float Bob = FMath::Sin(T * 5.f + Gem.Seed) * 3.f;
		P.ArtCentered(FindBrush(Gem.Value >= 2 ? TEXT("xp_blue") : TEXT("xp_receipt")), Gem.Pos.X, Gem.Pos.Y + Bob, 46.f);
	}
	for (const FCallRing& Ring : Rings)
	{
		const float R = Ring.Radius * (1.1f - Ring.Life * 0.1f);
		P.Brush(&CircleBrush, Ring.Pos.X - R, Ring.Pos.Y - R, R * 2.f, R * 2.f, WithAlpha(Hex(TEXT("ffb340")), Ring.Life * 0.25f));
	}

	// 손님 · 퇴점 손님 · 주인공을 발 높이 순으로 그려 앞뒤가 맞게 겹친다
	struct FDrawItem { float Y; int32 Kind; int32 Index; };
	TArray<FDrawItem> Items;
	Items.Reserve(Enemies.Num() + Departures.Num() + 1);
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		Items.Add({ Enemies[i].Pos.Y + Enemies[i].R * 0.9f, 0, i });
	}
	for (int32 i = 0; i < Departures.Num(); ++i)
	{
		Items.Add({ Departures[i].Pos.Y + Departures[i].R * 0.9f, 1, i });
	}
	Items.Add({ HeroPos.Y + HeroR * 0.9f, 2, 0 });
	Items.Sort([](const FDrawItem& A, const FDrawItem& B) { return A.Y < B.Y; });

	for (const FDrawItem& Item : Items)
	{
		if (Item.Kind == 0)
		{
			PaintEnemy(P, Enemies[Item.Index]);
		}
		else if (Item.Kind == 1)
		{
			const FDeparture& Leave = Departures[Item.Index];
			const FEnemyDef& Def = EnemyDefs[static_cast<int32>(Leave.Type)];
			const float Alpha = FMath::Max(0.f, Leave.Life / 0.65f);
			P.Sprite(FindBrush(*FString::Printf(TEXT("%s_walk_front"), Def.Art)), WalkFrame(Leave.Walk), 4, Leave.Pos.X, Item.Y, Def.DrawSize, Leave.Dir > 0.f, WithAlpha(FLinearColor::White, Alpha));
			P.Text(TEXT("퇴점"), Leave.Pos.X, Item.Y - Def.DrawSize * 0.95f, 22.f, WithAlpha(Hex(TEXT("80e6bc")), Alpha), ESurvivorAlign::Center, true, 2);
		}
		else
		{
			PaintHero(P);
		}
	}
}

void USurvivorMiniGameWidget::PaintProjectiles(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	for (const FScrap& Scrap : Scraps)
	{
		P.RotatedBrush(FindBrush(*FString::Printf(TEXT("scrap%d"), Scrap.Variant)), Scrap.Pos.X - 15.f, Scrap.Pos.Y - 15.f, 30.f, 30.f, Scrap.Rot,
			WithAlpha(FLinearColor::White, Scrap.Life / 0.3f));
	}
	// 죄송합니다: 글자가 박힌 말풍선 그림 (320×160 → 104×52)
	for (const FShot& Shot : Shots)
	{
		P.Brush(FindBrush(TEXT("apology")), Shot.Pos.X - 52.f, Shot.Pos.Y - 26.f, 104.f, 52.f);
	}
	for (const FCouponShot& Coupon : Coupons)
	{
		P.RotatedBrush(FindBrush(*FString::Printf(TEXT("coupon%d"), Coupon.Variant)), Coupon.Pos.X - 30.f, Coupon.Pos.Y - 30.f, 60.f, 60.f, Coupon.Life * 7.f);
	}
	// 매뉴얼: 주인공 둘레를 돌면서 그림 자체도 흔들린다
	for (int32 i = 0; i < ManualCount; ++i)
	{
		const float A = ManualAngle + i * UE_TWO_PI / ManualCount;
		const FVector2f At = HeroPos + FVector2f(FMath::Cos(A), FMath::Sin(A)) * ManualRadius;
		P.RotatedBrush(FindBrush(TEXT("manual")), At.X - 32.f, At.Y - 32.f, 64.f, 64.f, FMath::Sin(ManualAngle * 2.f + i) * 0.35f);
	}
	for (const FPopText& Pop : Pops)
	{
		P.Text(Pop.Text, Pop.Pos.X, Pop.Pos.Y, 28.f, WithAlpha(Pop.Color, Pop.Life), ESurvivorAlign::Center, true, 3);
	}
}

void USurvivorMiniGameWidget::PaintHud(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	const bool bPractice = Mode == EMode::Practice;

	// ---- 왼쪽 위: 남은 시간 · 멘탈 · 숙련도 ----
	{
		constexpr float X = 30.f;
		constexpr float W = 680.f;
		P.Brush(&PanelBrush, X, 18.f, W, 152.f);
		P.Text(TEXT("남은 시간"), X + 30.f, 52.f, 22.f, Muted());
		if (bPractice)
		{
			P.Text(TEXT("연습"), X + 30.f, 112.f, 64.f, Mint(), ESurvivorAlign::Left, true);
		}
		else
		{
			P.Text(FString::Printf(TEXT("%02d초"), FMath::CeilToInt(Duration - T)), X + 30.f, 112.f, 64.f, bRush ? Bad() : Ink(), ESurvivorAlign::Left, true);
		}

		const float BarX = X + 260.f;
		const float BarW = W - 290.f;
		const bool bLow = Mental < 30.f;
		P.Text(FString::Printf(TEXT("멘탈 %d"), FMath::CeilToInt(Mental)), BarX, 50.f, 24.f, bLow ? Bad() : Ink(), ESurvivorAlign::Left, true);
		P.Brush(&BarBackBrush, BarX, 68.f, BarW, 22.f);
		P.Box(BarX + 3.f, 71.f, (BarW - 6.f) * Mental / 100.f, 16.f, bLow ? Bad() : Mint());

		P.Text(FString::Printf(TEXT("Lv.%d  숙련도"), Level), BarX, 118.f, 24.f, Ink(), ESurvivorAlign::Left, true);
		P.Brush(&BarBackBrush, BarX, 134.f, BarW, 16.f);
		P.Box(BarX + 3.f, 137.f, (BarW - 6.f) * FMath::Clamp(static_cast<float>(Xp) / XpNeed, 0.f, 1.f), 10.f, Hex(TEXT("5ad48a")));
	}

	// ---- 오른쪽 위: 퇴점 · 예상 보상 ----
	{
		constexpr float W = 680.f;
		constexpr float X = ScreenW - 30.f - W;
		P.Brush(&PanelBrush, X, 18.f, W, 152.f);
		P.Text(TEXT("퇴점시킨 손님"), X + 30.f, 52.f, 22.f, Muted());
		P.Text(FString::Printf(TEXT("%d명"), Exits), X + 30.f, 112.f, 64.f, Ink(), ESurvivorAlign::Left, true);

		const FMiniGameReward Preview = PreviewReward(Exits);
		const float R = X + W - 30.f;
		P.Text(FString::Printf(TEXT("급여 %s원"), *FString::FormatAsNumber(Preview.Money)), R, 52.f, 22.f, Muted(), ESurvivorAlign::Right);
		P.Text(FString::Printf(TEXT("%s +%d"), *StatLabel(Preview.Stat), Preview.StatAmount), R, 102.f, 40.f, Mint(), ESurvivorAlign::Right, true);
		for (int32 More = 1; More <= 60; ++More)
		{
			if (PreviewReward(Exits + More).StatAmount > Preview.StatAmount)
			{
				P.Text(FString::Printf(TEXT("%d명 더 퇴점시키면 +%d"), More, Preview.StatAmount + 1), R, 145.f, 20.f, Muted(), ESurvivorAlign::Right);
				break;
			}
		}
	}

	// ---- 마지막 10초 띠 ----
	if (bRush && (Mode == EMode::Play || Mode == EMode::LevelUp || Mode == EMode::Pause))
	{
		P.Brush(&DarkPanelBrush, CenterX - 400.f, AreaY + 18.f, 800.f, 64.f);
		P.Text(TEXT("퇴근 10초 전 — 지금 다 오신다고요?"), CenterX, AreaY + 50.f, 30.f, Hex(TEXT("ffe1dd")), ESurvivorAlign::Center, true);
	}

	// ---- 아래쪽 띠: 익힌 스킬 · 조작 ----
	const float BandY = AreaBottom + (ScreenH - AreaBottom) * 0.5f + 3.f;
	P.Text(TEXT("익힌 스킬"), 50.f, BandY, 22.f, Muted(), ESurvivorAlign::Left, true);
	float SkillX = 170.f;
	for (int32 i = 0; i < static_cast<int32>(ESkill::Num); ++i)
	{
		// 죄송합니다는 처음부터 Lv.1 (동시 응대 수 = 레벨)
		const int32 Lv = i == static_cast<int32>(ESkill::Multi) ? ShotCount : Taken[i];
		if (Lv <= 0)
		{
			continue;
		}
		P.ArtCentered(SkillIcon(static_cast<ESkill>(i)), SkillX + 28.f, BandY, 58.f);
		P.Text(FString::Printf(TEXT("%d"), Lv), SkillX + 62.f, BandY + 18.f, 22.f, Ink(), ESurvivorAlign::Left, true);
		SkillX += 100.f;
	}
	if (Mode != EMode::Practice)
	{
		P.Text(TEXT("WASD / 방향키  이동    응대는 자동    P / Esc  일시정지"), ScreenW - 50.f, BandY, 20.f, Muted(), ESurvivorAlign::Right);
	}
}

void USurvivorMiniGameWidget::PaintOverlay(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	// 화면을 살짝 어둡게 덮고 가운데 판
	P.Box(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.08f, 0.06f, 0.05f, 0.45f));
	constexpr float PanelW = 1180.f;
	const float L = CenterX - PanelW * 0.5f;
	P.Brush(&OverlayBrush, L, 150.f, PanelW, 960.f);

	if (Mode == EMode::Title)
	{
		const FMiniGameReward Reward = PreviewReward(0);
		P.Sprite(FindBrush(TEXT("hero_idle_front")), 0, 1, CenterX, 380.f, 190.f, false);
		P.Text(TEXT("NIGHT SHIFT · 23:59"), CenterX, 410.f, 20.f, Tag(), ESurvivorAlign::Center);
		P.Text(TEXT("진상 퇴치"), CenterX, 470.f, 52.f, Ink(), ESurvivorAlign::Center, true);
		P.Text(TEXT("죄송합니다. 퇴근은 안 죄송합니다."), CenterX, 530.f, 26.f, Accent(), ESurvivorAlign::Center, true);
		P.Text(TEXT("WASD / 방향키로 이동 · 응대는 자동"), CenterX, 600.f, 26.f, Ink(), ESurvivorAlign::Center);
		P.Text(TEXT("초록 영수증을 모으면 서비스 스킬 3택!"), CenterX, 645.f, 26.f, Ink(), ESurvivorAlign::Center);
		P.Text(FString::Printf(TEXT("멘탈을 지키며 %d초만 버티세요. 마지막 10초엔 손님이 몰려옵니다."), FMath::RoundToInt(Duration)), CenterX, 690.f, 26.f, Ink(), ESurvivorAlign::Center);
		P.Text(FString::Printf(TEXT("급여 %s원 · 손님을 많이 퇴점시킬수록 %s이 올라요"), *FString::FormatAsNumber(Reward.Money), *StatLabel(Reward.Stat)),
			CenterX, 750.f, 24.f, Mint(), ESurvivorAlign::Center, true);
		if (PlayCount == 0)
		{
			P.Text(TEXT("처음이라 출근 전에 짧게 연습해 볼게요."), CenterX, 820.f, 22.f, Muted(), ESurvivorAlign::Center);
		}
		else
		{
			P.Text(FString::Printf(TEXT("대충 일하면 급여는 같고 %s은 +1만 올라요."), *StatLabel(Reward.Stat)), CenterX, 1070.f, 20.f, Muted(), ESurvivorAlign::Center);
		}
	}
	else if (Mode == EMode::Tutorial)
	{
		PaintTutorialPage(P);
	}
	else if (Mode == EMode::Pause)
	{
		P.Text(TEXT("PART-TIME ARCADE"), CenterX, 420.f, 20.f, Tag(), ESurvivorAlign::Center);
		P.Text(TEXT("잠깐, 심호흡."), CenterX, 490.f, 48.f, Ink(), ESurvivorAlign::Center, true);
		P.Text(TEXT("근무 시간이 멈춰 있습니다."), CenterX, 560.f, 26.f, Ink(), ESurvivorAlign::Center);
		P.Text(TEXT("WASD / 방향키  이동    응대는 자동    P / Esc  일시정지"), CenterX, 640.f, 22.f, Muted(), ESurvivorAlign::Center);
	}
	else if (Mode == EMode::Result)
	{
		P.Text(TEXT("SHIFT COMPLETE"), CenterX, 230.f, 20.f, Tag(), ESurvivorAlign::Center);
		if (bSkipped)
		{
			P.Text(TEXT("대충 일하고 퇴근했어요."), CenterX, 300.f, 46.f, Ink(), ESurvivorAlign::Center, true);
			P.Text(TEXT("오늘은 계산대만 지키다 왔어요."), CenterX, 370.f, 26.f, Ink(), ESurvivorAlign::Center);
			P.Text(FString::Printf(TEXT("직접 일하면 %s을 더 올릴 수 있어요."), *StatLabel(FinalReward.Stat)), CenterX, 420.f, 26.f, Muted(), ESurvivorAlign::Center);
		}
		else
		{
			P.Text(bSurvived ? TEXT("유니폼 반납. 칼퇴 완료!") : TEXT("멘탈이 먼저 퇴근했습니다."), CenterX, 300.f, 46.f, Ink(), ESurvivorAlign::Center, true);
			P.Text(bSurvived ? TEXT("손님: \"저기요!\"   나: \"저도 손님인데요.\"") : TEXT("점장: \"오늘은 들어가 봐요.\"  그래도 일한 돈은 챙겨 갑니다."),
				CenterX, 370.f, 26.f, Ink(), ESurvivorAlign::Center);
			P.Text(FString::Printf(TEXT("퇴점 %d명 · Lv.%d · %d초 근무"), Exits, Level, FMath::FloorToInt(T)), CenterX, 420.f, 26.f, Ink(), ESurvivorAlign::Center, true);
		}

		// 영수증
		const float RX = CenterX - 280.f;
		P.Brush(&ReceiptBrush, RX, 480.f, 560.f, 330.f);
		P.Text(TEXT("급여"), RX + 40.f, 540.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), RX + 520.f, 540.f, 26.f, Ink(), ESurvivorAlign::Right, true);
		P.Text(StatLabel(FinalReward.Stat), RX + 40.f, 610.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("+%d"), FinalReward.StatAmount), RX + 520.f, 610.f, 26.f, Ink(), ESurvivorAlign::Right, true);
		P.Box(RX + 40.f, 665.f, 480.f, 2.f, Hex(TEXT("b9a58f")));
		P.Text(TEXT("받은 돈"), RX + 40.f, 730.f, 28.f, Ink(), ESurvivorAlign::Left, true);
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), RX + 520.f, 730.f, 32.f, Mint(), ESurvivorAlign::Right, true);
		if (!bSkipped)
		{
			P.Text(FString::Printf(TEXT("퇴점시킨 손님이 많을수록 %s이 더 올라요"), *StatLabel(FinalReward.Stat)), CenterX, 860.f, 20.f, Muted(), ESurvivorAlign::Center);
		}
	}

	PaintButtons(P);
}

void USurvivorMiniGameWidget::PaintLevelUp(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	P.Box(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.08f, 0.06f, 0.05f, 0.55f));
	P.Text(TEXT("SERVICE LEVEL UP"), CenterX, 300.f, 22.f, Hex(TEXT("ffcf85")), ESurvivorAlign::Center, true);
	P.Text(TEXT("알바 숙련도 상승"), CenterX, 360.f, 52.f, FLinearColor::White, ESurvivorAlign::Center, true, 3);
	P.Text(TEXT("하나를 고르세요 · 숫자 1 / 2 / 3 · 고르는 동안 시간이 멈춥니다"), CenterX, 420.f, 24.f, Hex(TEXT("f3e9da")), ESurvivorAlign::Center);
	PaintButtons(P);
}

void USurvivorMiniGameWidget::PaintButtons(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	const TArray<FOverlayButton> Buttons = GetOverlayButtons();
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FOverlayButton& B = Buttons[i];
		const bool bPressed = PressedButton == i;
		const bool bHovered = HoveredButton == i;
		const float Press = bPressed ? 3.f : 0.f;
		const float Left = B.X - B.W * 0.5f;
		const float Top = B.Y - B.H * 0.5f + Press;

		if (B.bCard)
		{
			// 레벨업 카드: 아이콘 · 이름 · 레벨 · 설명 두 줄 · 단축키
			const int32 Index = static_cast<int32>(B.Action) - static_cast<int32>(EButtonAction::Choose0);
			if (!Choices.IsValidIndex(Index))
			{
				continue;
			}
			const ESkill Skill = Choices[Index];
			const FSkillDef& Def = Skills[static_cast<int32>(Skill)];
			const int32 NextLv = (Skill == ESkill::Multi ? ShotCount : Taken[static_cast<int32>(Skill)]) + 1;
			P.Brush(bHovered || bPressed ? &CardHoverBrush : &CardBrush, Left, Top, B.W, B.H);
			P.ArtCentered(SkillIcon(Skill), B.X, Top + 125.f, 170.f);
			P.Text(Def.Name, B.X, Top + 255.f, 34.f, Ink(), ESurvivorAlign::Center, true);
			P.Text(FString::Printf(TEXT("Lv.%d"), NextLv), B.X, Top + 298.f, 22.f, Accent(), ESurvivorAlign::Center, true);
			FString LineA, LineB;
			SplitDesc(Def.Desc, LineA, LineB);
			P.Text(LineA, B.X, Top + 350.f, 22.f, Muted(), ESurvivorAlign::Center);
			P.Text(LineB, B.X, Top + 384.f, 22.f, Muted(), ESurvivorAlign::Center);
			P.Text(FString::Printf(TEXT("%d"), Index + 1), Left + 34.f, Top + 36.f, 30.f, Mint(), ESurvivorAlign::Center, true);
		}
		else if (B.bPrimary)
		{
			P.Brush(bHovered || bPressed ? &ButtonHoverBrush : &ButtonBrush, Left, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 30.f, FLinearColor::White, ESurvivorAlign::Center, true, 2);
		}
		else
		{
			P.Brush(bHovered || bPressed ? &SecondaryHoverBrush : &SecondaryBrush, Left, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 24.f, Mint(), ESurvivorAlign::Center, true);
		}
	}
}

void USurvivorMiniGameWidget::PaintTutorialPage(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	// 설명 카드 문구 (Live Coding 크래시를 피하려고 함수 안 static 배열은 쓰지 않는다)
	const FMiniGameReward Reward = PreviewReward(0);
	const FString Stat = StatLabel(Reward.Stat);
	const FString Titles[] = { TEXT("움직이면 응대는 자동"), TEXT("영수증과 서비스 스킬"), TEXT("오늘의 방문객") };
	const FString Lines[3][3] =
	{
		{
			TEXT("WASD / 방향키로 매장을 돌아다녀요."),
			TEXT("\"죄송합니다\"는 가까운 손님에게 저절로 날아가요."),
			TEXT("손님 인내심(머리 위 막대)을 다 깎으면 퇴점!"),
		},
		{
			TEXT("퇴점한 손님은 초록 영수증을 떨어뜨려요."),
			TEXT("영수증으로 숙련도가 차면 서비스 스킬 3개 중 하나를 골라요."),
			TEXT("고르는 동안은 시간이 멈춰요 (숫자 1 / 2 / 3)."),
		},
		{
			TEXT("손님과 부딪히거나 쿠폰에 맞으면 멘탈이 깎여요."),
			TEXT("화난 손님은 붉게 떨다가 돌진해요. 옆으로 피하세요!"),
			FString::Printf(TEXT("60초 버티면 칼퇴. 많이 퇴점시킬수록 %s이 더 올라요."), *Stat),
		},
	};

	const int32 Page = FMath::Clamp(TutorialPage, 0, 2);
	P.Text(FString::Printf(TEXT("일하는 법  %d / 3"), Page + 1), CenterX, 220.f, 22.f, Tag(), ESurvivorAlign::Center);
	P.Text(Titles[Page], CenterX, 285.f, 46.f, Ink(), ESurvivorAlign::Center, true);

	if (Page == 0)
	{
		P.Sprite(FindBrush(TEXT("hero_walk_front")), 1, 4, CenterX - 200.f, 600.f, 210.f, false);
		P.Brush(FindBrush(TEXT("apology")), CenterX - 40.f, 430.f, 160.f, 80.f);
		P.Sprite(FindBrush(TEXT("tipsy_walk_front")), 2, 4, CenterX + 230.f, 600.f, 190.f, true);
	}
	else if (Page == 1)
	{
		P.ArtCentered(FindBrush(TEXT("xp_receipt")), CenterX - 380.f, 500.f, 110.f);
		P.Text(TEXT("›"), CenterX - 270.f, 500.f, 60.f, Muted(), ESurvivorAlign::Center);
		const TCHAR* const Icons[] = { TEXT("skill_apology"), TEXT("skill_manual"), TEXT("skill_manager") };
		for (int32 i = 0; i < 3; ++i)
		{
			P.ArtCentered(FindBrush(Icons[i]), CenterX - 110.f + i * 190.f, 500.f, 150.f);
		}
	}
	else
	{
		for (int32 i = 0; i < UE_ARRAY_COUNT(EnemyDefs); ++i)
		{
			const float X = CenterX + (i - 1.5f) * 230.f;
			P.Sprite(FindBrush(*FString::Printf(TEXT("%s_walk_front"), EnemyDefs[i].Art)), 0, 4, X, 590.f, 180.f, false);
			P.Text(EnemyDefs[i].Name, X, 620.f, 24.f, Ink(), ESurvivorAlign::Center, true);
		}
	}

	for (int32 i = 0; i < 3; ++i)
	{
		P.Text(Lines[Page][i], CenterX, 700.f + i * 52.f, 27.f, i == 2 ? Mint() : Ink(), ESurvivorAlign::Center, i == 2);
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const float X = CenterX + (i - 1) * 32.f;
		P.Brush(&CircleBrush, X - 8.f, 880.f, 16.f, 16.f, i == Page ? Mint() : Hex(TEXT("d6cbb8")));
	}

	PaintButtons(P);
}

void USurvivorMiniGameWidget::PaintPractice(FSurvivorPainter& P) const
{
	using namespace SurvivorGame;

	// 단계마다 한 줄 안내 + 한 줄 보충 (Live Coding 크래시를 피하려고 함수 안 static 배열은 쓰지 않는다)
	const TCHAR* const Prompts[PracticeSteps][2] =
	{
		{ TEXT("WASD / 방향키로 매장을 걸어 보세요"), TEXT("위로 가면 뒷모습, 아래로 가면 앞모습이에요") },
		{ TEXT("손님이 와요! 가까이 가 보세요"), TEXT("\"죄송합니다\"는 저절로 날아가요. 인내심을 다 깎으면 퇴점!") },
		{ TEXT("떨어진 초록 영수증을 주워요"), TEXT("숙련도가 차면 서비스 스킬을 하나 고를 수 있어요") },
		{ TEXT("마지막으로 조심할 것"), TEXT("") },
		{ TEXT("준비 끝! 곧 출근해요"), TEXT("") },
	};

	const int32 Step = FMath::Clamp(PracticeStep, 0, PracticeSteps - 1);
	const float Pulse = 0.6f + 0.4f * FMath::Sin(PracticeStepTime * 6.f);

	// 안내판 (위쪽 띠 아래, 바닥 위쪽 가운데)
	P.Brush(&PanelBrush, CenterX - 560.f, AreaY + 20.f, 1120.f, 150.f);
	P.Text(FString::Printf(TEXT("연습  %d / %d"), FMath::Min(Step + 1, PracticeSteps - 1), PracticeSteps - 1), CenterX, AreaY + 52.f, 20.f, Tag(), ESurvivorAlign::Center);
	P.Text(Prompts[Step][0], CenterX, AreaY + 98.f, 32.f, Ink(), ESurvivorAlign::Center, true);
	P.Text(Prompts[Step][1], CenterX, AreaY + 142.f, 22.f, Muted(), ESurvivorAlign::Center);

	if (Step == PracticeMoveStep)
	{
		// 얼마나 걸었는지 막대로 (안내판 바로 아래 - 바닥 아래쪽은 출구 문과 겹친다)
		P.Brush(&BarBackBrush, CenterX - 200.f, AreaY + 190.f, 400.f, 20.f);
		P.Box(CenterX - 197.f, AreaY + 193.f, 394.f * FMath::Clamp(PracticeMoved / PracticeMoveGoal, 0.f, 1.f), 14.f, Mint());
		P.Text(TEXT("WASD / ← ↑ → ↓"), HeroPos.X, HeroPos.Y - 190.f, 34.f, WithAlpha(Mint(), Pulse), ESurvivorAlign::Center, true, 3);
	}
	else if (Step == PracticeApologyStep)
	{
		P.Text(FString::Printf(TEXT("퇴점 %d / %d"), FMath::Min(Exits - PracticeExitBase, PracticeExitGoal), PracticeExitGoal), CenterX, AreaY + 205.f, 30.f, Ink(), ESurvivorAlign::Center, true, 2);
	}
	else if (Step == PracticeReceiptStep)
	{
		// 영수증마다 반짝이는 표시
		for (const FGem& Gem : Gems)
		{
			P.Brush(&CircleBrush, Gem.Pos.X - 40.f, Gem.Pos.Y - 40.f, 80.f, 80.f, WithAlpha(Hex(TEXT("5ad48a")), 0.25f * Pulse));
		}
	}
	else if (Step == PracticeExplainStep)
	{
		P.Box(0.f, 0.f, ScreenW, ScreenH, FLinearColor(0.08f, 0.06f, 0.05f, 0.35f));
		P.Brush(&OverlayBrush, CenterX - 520.f, 400.f, 1040.f, 600.f);
		P.Text(TEXT("멘탈을 지켜요"), CenterX, 460.f, 38.f, Bad(), ESurvivorAlign::Center, true);

		struct FRow { const TCHAR* Label; const TCHAR* Value; };
		const FRow Rows[] =
		{
			{ TEXT("손님과 부딪힘"), TEXT("멘탈 −16") },
			{ TEXT("쿠폰 진상의 쿠폰"), TEXT("멘탈 −12 · 멀리서 던져요") },
			{ TEXT("붉게 떠는 화난 손님"), TEXT("곧 돌진! 옆으로 피하세요") },
			{ TEXT("마지막 10초"), TEXT("손님이 한꺼번에 몰려와요") },
			{ TEXT("60초 버티면"), TEXT("칼퇴 · 멘탈 0이면 퇴근") },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
		{
			const float Y = 535.f + i * 58.f;
			P.Text(Rows[i].Label, CenterX - 440.f, Y, 26.f, Ink(), ESurvivorAlign::Left, true);
			P.Text(Rows[i].Value, CenterX + 440.f, Y, 26.f, i == 4 ? Mint() : Bad(), ESurvivorAlign::Right, true);
		}
		P.Text(TEXT("Space 또는 \"알겠어요\"를 눌러 계속"), CenterX, 990.f + 40.f, 20.f, Muted(), ESurvivorAlign::Center);
	}

	PaintButtons(P);
}
