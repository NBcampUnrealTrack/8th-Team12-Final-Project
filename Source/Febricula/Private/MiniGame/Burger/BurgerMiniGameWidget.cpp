// [skydodo] 2026-10-10 작성
// 요약: 햄버거집 알바 미니게임 구현 (play.html 이식)
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/Burger/BurgerMiniGameWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogBurgerMiniGame, Log, All);

namespace BurgerGame
{
	/** HTML 캔버스 크기. 모든 좌표는 이 좌표계 기준이다 */
	constexpr float CanvasW = 840.f;
	constexpr float CanvasH = 1240.f;

	/**
	 * 가로형 화면. 세로형 HTML 캔버스만 그리면 16:9에서 가로의 37%만 쓴다.
	 * 플레이 영역(위 캔버스)은 가운데 그대로 두고, 위·아래에 있던 주문판·상태판을 좌우 패널로 옮겼다.
	 * 게임 밸런스(이동 거리·속도)는 플레이 영역 안이라 그대로다.
	 */
	constexpr float ScreenW = 2200.f;
	constexpr float ScreenH = 1240.f;
	constexpr float PlayX = (ScreenW - CanvasW) * 0.5f;
	constexpr float PanelW = 600.f;
	constexpr float LeftPanelX = PlayX - 40.f - PanelW;
	constexpr float RightPanelX = PlayX + CanvasW + 40.f;

	struct FIngredient
	{
		const TCHAR* Name;
		const TCHAR* Asset;
		const TCHAR* Color; // 받았을 때 터지는 조각 색
	};

	const FIngredient Ingredients[] =
	{
		{ TEXT("빵 세트"), TEXT("bun-set"), TEXT("eebc77") },
		{ TEXT("패티"), TEXT("patty"), TEXT("a56c55") },
		{ TEXT("치즈"), TEXT("cheese"), TEXT("ffd05d") },
		{ TEXT("양상추"), TEXT("lettuce"), TEXT("87c777") },
		{ TEXT("토마토"), TEXT("tomato"), TEXT("e17d6c") },
	};
	constexpr int32 NumIngredients = UE_ARRAY_COUNT(Ingredients);

	/** 재료 번호 다음 번호가 상한 패티 (주문에는 절대 안 나온다) */
	constexpr int32 SpoiledId = NumIngredients;

	struct FRecipe
	{
		const TCHAR* Name;
		int32 Parts[4];
		int32 Count;
	};

	const FRecipe Recipes[] =
	{
		{ TEXT("클래식 버거"), { 0, 1 }, 2 },
		{ TEXT("치즈 버거"), { 0, 1, 2 }, 3 },
		{ TEXT("가든 버거"), { 0, 1, 3 }, 3 },
		{ TEXT("스페셜 버거"), { 0, 1, 3, 4 }, 4 },
	};
	constexpr int32 NumRecipes = UE_ARRAY_COUNT(Recipes);

	// ---- 규칙 수치 (HTML 그대로) ----
	constexpr float MoveSpeed = 740.f;
	constexpr float LoadSlowPerItem = 0.065f;
	constexpr float MaxLoadSlow = 0.58f;
	constexpr float MinX = 85.f;
	constexpr float MaxX = 755.f;
	constexpr float CatchTop = 950.f;
	constexpr float CatchBottom = 980.f;
	constexpr float CatchHalfWidth = 82.f;

	/**
	 * HTML은 주문판 아래(y 290)에서 재료가 나타났다. 주문판을 옆으로 뺐으니 위쪽(y 120)에서 떨어뜨리되,
	 * 트레이까지 걸리는 시간은 원본과 같도록 속도를 거리 비율만큼 올린다.
	 */
	constexpr float SpawnY = 120.f;
	constexpr float OriginalSpawnY = 290.f;
	constexpr float FallSpeedScale = (CatchTop - SpawnY) / (CatchTop - OriginalSpawnY);
	constexpr float SpawnInterval = 0.65f;
	constexpr float RushSpawnInterval = 0.43f;
	constexpr float RushSeconds = 10.f;
	constexpr int32 MaxJunk = 5;
	constexpr float SubmitPause = 0.65f;
	constexpr float CleanPause = 0.45f;
	constexpr float SuccessFaceSeconds = 0.65f;
	constexpr float FailureFaceSeconds = 0.45f;

	/**
	 * 상한 패티 벌칙 (HTML보다 세게 - 다른 주문 재료와 구분되게).
	 * 다른 주문 재료는 HTML 그대로 점수 차감 없이 콤보 초기화 + 적재량 +1.
	 */
	constexpr int32 SpoiledScorePenalty = 1;   // 완성 수 -1 (0 밑으로는 안 내려간다)
	constexpr int32 SpoiledJunk = 2;           // 적재량 +2
	constexpr float SpoiledStunSeconds = 0.6f; // 그동안 못 움직인다
	constexpr float SpoiledFaceSeconds = 0.8f;

	/** 연습 판 단계 수 (마지막 단계는 "준비 끝" 안내 후 자동 출근) */
	constexpr int32 PracticeSteps = 10;
	/** 상한 패티를 받은 직후 멈춰서 손해 내역을 보여 주는 단계 (Space · Enter · 버튼으로 넘어간다) */
	constexpr int32 SpoiledExplainStep = 5;
	/** "움직여 보세요" 단계에서 이만큼 움직이면 넘어간다 (무거울 때 / 가벼울 때 비교) */
	constexpr float PracticeMoveDistance = 320.f;
	/** 이 적재량부터 머리 위에 "무거워요" 표시 */
	constexpr int32 HeavyWarnLoad = 3;
	constexpr float PracticeFallSpeed = 230.f;
	constexpr float PracticeFinishDelay = 1.6f;

	/** 오버레이 버튼 (캔버스 좌표, 가운데 기준) */
	constexpr float ButtonW = 300.f;
	constexpr float ButtonH = 84.f;

	FLinearColor Hex(const TCHAR* InHex)
	{
		return FLinearColor(FColor::FromHex(InHex));
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = FMath::Clamp(Alpha, 0.f, 1.f);
		return Color;
	}

	FLinearColor Ink() { return Hex(TEXT("59443a")); }
	FLinearColor Coral() { return Hex(TEXT("d97664")); }
	FLinearColor Green() { return Hex(TEXT("708b68")); }
	FLinearColor Muted() { return Hex(TEXT("987e6a")); }
}

/** 캔버스 좌표로 그리는 도우미. 그릴 때마다 레이어를 하나씩 올려 그린 순서가 곧 앞뒤가 된다 */
struct FBurgerPainter
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
			// MakeBox는 브러시의 TintColor를 보지 않고 넘긴 색만 쓴다 (SImage는 호출하는 쪽에서 곱한다).
			// 안 곱하면 둥근 패널이 전부 흰색으로 그려진다.
			const FLinearColor Final = Tint * InBrush->TintColor.GetSpecifiedColor();
			FSlateDrawElement::MakeBox(Out, ++Layer, Rect(X, Y, W, H), InBrush, ESlateDrawEffect::None, Final);
		}
	}

	/** HTML의 art(): (X, Y)를 가운데로 Size×Size */
	void ArtCentered(const FSlateBrush* InBrush, float X, float Y, float Size)
	{
		Brush(InBrush, X - Size * 0.5f, Y - Size * 0.5f, Size, Size);
	}

	/** HTML의 txt(): Y는 글자 세로 가운데. 크기는 CSS px */
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
			Outline = FFontOutlineSettings(OutlinePx, BurgerGame::WithAlpha(BurgerGame::Ink(), Color.A * 0.55f));
		}
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Px * 0.75f, Outline);
		const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(InText, Font);

		float Left = X;
		if (Align == EAlign::Center)
		{
			Left = X - Size.X * 0.5f;
		}
		else if (Align == EAlign::Right)
		{
			Left = X - Size.X;
		}
		FSlateDrawElement::MakeText(Out, ++Layer, Rect(Left, Y - Size.Y * 0.5f, Size.X, Size.Y), InText, Font, ESlateDrawEffect::None, Color);
	}
};

using EAlign = FBurgerPainter::EAlign;

// ---------------------------------------------------------------------------
// 생성 · 준비
// ---------------------------------------------------------------------------

UBurgerMiniGameWidget::UBurgerMiniGameWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	auto AddArt = [this](const TCHAR* Key, const TCHAR* Folder, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Burger/Textures/%s/%s.%s"), Folder, Asset, Asset);
		Art.Add(Key, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path)));
	};

	AddArt(TEXT("kitchen"), TEXT("Background"), TEXT("T_Burger_Kitchen"));

	AddArt(TEXT("idle"), TEXT("Characters"), TEXT("T_Burger_Char_Idle"));
	AddArt(TEXT("blink"), TEXT("Characters"), TEXT("T_Burger_Char_Blink"));
	AddArt(TEXT("success"), TEXT("Characters"), TEXT("T_Burger_Char_Success"));
	AddArt(TEXT("failure"), TEXT("Characters"), TEXT("T_Burger_Char_Failure"));
	for (int32 i = 0; i < 4; ++i)
	{
		Art.Add(*FString::Printf(TEXT("walk-left-%d"), i), TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/MiniGame/Burger/Textures/Characters/T_Burger_Char_WalkLeft%d.T_Burger_Char_WalkLeft%d"), i, i))));
		Art.Add(*FString::Printf(TEXT("walk-right-%d"), i), TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/MiniGame/Burger/Textures/Characters/T_Burger_Char_WalkRight%d.T_Burger_Char_WalkRight%d"), i, i))));
	}

	AddArt(TEXT("bun-set"), TEXT("Items"), TEXT("T_Burger_Item_BunSet"));
	AddArt(TEXT("patty"), TEXT("Items"), TEXT("T_Burger_Item_Patty"));
	AddArt(TEXT("cheese"), TEXT("Items"), TEXT("T_Burger_Item_Cheese"));
	AddArt(TEXT("lettuce"), TEXT("Items"), TEXT("T_Burger_Item_Lettuce"));
	AddArt(TEXT("tomato"), TEXT("Items"), TEXT("T_Burger_Item_Tomato"));
	AddArt(TEXT("spoiled_patty"), TEXT("Items"), TEXT("T_Burger_Item_SpoiledPatty"));
	AddArt(TEXT("finished_burger"), TEXT("Items"), TEXT("T_Burger_Item_FinishedBurger"));

	AddArt(TEXT("order-panel"), TEXT("UI"), TEXT("T_Burger_UI_OrderPanel"));
	AddArt(TEXT("status-panel"), TEXT("UI"), TEXT("T_Burger_UI_StatusPanel"));
	AddArt(TEXT("button-normal"), TEXT("UI"), TEXT("T_Burger_UI_ButtonNormal"));
	AddArt(TEXT("button-hover"), TEXT("UI"), TEXT("T_Burger_UI_ButtonHover"));
	AddArt(TEXT("button-pressed"), TEXT("UI"), TEXT("T_Burger_UI_ButtonPressed"));

	// 효과음 (원본 파일 이름 → 애셋 이름)
	//   먹었을 때 → Catch, 리스트에 있는거 다 먹었을 때 → RecipeComplete, 잘못된 걸 먹었을 때 → Wrong,
	//   상한 패티를 먹었을 때 → Spoiled, 제출할때 → Submit, 제출 성공 → SubmitOk,
	//   10초 남았을시 → Rush, 성공 시 → ShiftEnd, 버튼 클릭시 → Click
	auto AddSfx = [this](const TCHAR* Key, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Burger/Audio/SFX/%s.%s"), Asset, Asset);
		Sfx.Add(Key, TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path)));
	};
	AddSfx(TEXT("catch"), TEXT("S_Burger_SFX_Catch"));
	AddSfx(TEXT("complete"), TEXT("S_Burger_SFX_RecipeComplete"));
	AddSfx(TEXT("wrong"), TEXT("S_Burger_SFX_Wrong"));
	AddSfx(TEXT("spoiled"), TEXT("S_Burger_SFX_Spoiled"));
	AddSfx(TEXT("submit"), TEXT("S_Burger_SFX_Submit"));
	AddSfx(TEXT("submit_ok"), TEXT("S_Burger_SFX_SubmitOk"));
	AddSfx(TEXT("rush"), TEXT("S_Burger_SFX_Rush"));
	AddSfx(TEXT("shift_end"), TEXT("S_Burger_SFX_ShiftEnd"));
	AddSfx(TEXT("click"), TEXT("S_Burger_SFX_Click"));
}

void UBurgerMiniGameWidget::NativeConstruct()
{
	Super::NativeConstruct();
	using namespace BurgerGame;

	LoadedTextures.Reset();
	ArtBrushes.Reset();
	for (const TPair<FName, TSoftObjectPtr<UTexture2D>>& Pair : Art)
	{
		UTexture2D* Texture = Pair.Value.LoadSynchronous();
		if (!Texture)
		{
			UE_LOG(LogBurgerMiniGame, Warning, TEXT("그림을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
			continue;
		}
		LoadedTextures.Add(Texture);

		FSlateBrush& NewBrush = ArtBrushes.Add(Pair.Key);
		NewBrush.SetResourceObject(Texture);
		NewBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		NewBrush.DrawAs = ESlateBrushDrawType::Image;
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
			UE_LOG(LogBurgerMiniGame, Warning, TEXT("효과음을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
		}
	}

	SolidBrush = FSlateColorBrush(FLinearColor::White);
	SlotDoneBrush = FSlateRoundedBoxBrush(Hex(TEXT("e3ecd8")), 12.f, Hex(TEXT("9eb58d")), 2.f);
	SlotWaitBrush = FSlateRoundedBoxBrush(Hex(TEXT("f2e7d7")), 12.f, Hex(TEXT("dbc6ae")), 2.f);
	BarBackBrush = FSlateRoundedBoxBrush(Hex(TEXT("d8c6ae")), 8.f);
	BarFillBrush = FSlateRoundedBoxBrush(Coral(), 8.f);
	SecondaryBrush = FSlateRoundedBoxBrush(FLinearColor::White, 12.f, Coral(), 2.f);
	SecondaryHoverBrush = FSlateRoundedBoxBrush(Hex(TEXT("fbe7e1")), 12.f, Coral(), 2.f);
	HighlightBrush = FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.01f), 60.f, Coral(), 5.f);
	PanelBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fff8eb")), 0.92f), 22.f, Hex(TEXT("ccb397")), 3.f);
	OverlayBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fff8eb")), 0.93f), 22.f, Hex(TEXT("ccb397")), 3.f);
	ReceiptBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f, Hex(TEXT("c6aa88")), 2.f);
	// 채우기 알파가 0이면 Slate가 요소째로 버려서 테두리도 안 그려진다. 거의 투명하게만 둔다.
	FrameBrush = FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.01f), 22.f, Hex(TEXT("ccb397")), 3.f);

	ResetRound();
	PlayCount = GetPlayCount();
	TutorialPage = 0;
	bSkipped = false;
	Mode = EMode::Title;
}

const FSlateBrush* UBurgerMiniGameWidget::FindBrush(FName Name) const
{
	return ArtBrushes.Find(Name);
}

// ---------------------------------------------------------------------------
// 규칙
// ---------------------------------------------------------------------------

void UBurgerMiniGameWidget::ResetRound()
{
	T = 0.f;
	PlayerX = 420.f;
	Items.Reset();
	Pops.Reset();
	Particles.Reset();
	RecipeIndex = 0;
	Got.Reset();
	Junk = 0;
	Orders = 0;
	Errors = 0;
	Combo = 0;
	Best = 0;
	SpawnTimer = 0.f;
	Serving = 0.f;
	Bounce = 0.f;
	Serial = 0;
	Reaction = NAME_None;
	ReactionUntil = 0.f;
	Stun = 0.f;
	SpoiledCount = 0;
	JunkIds.Reset();
	WalkPhase = 0.f;
	PracticeMoved = 0.f;
	bRushAnnounced = false;
}

void UBurgerMiniGameWidget::PlaySfx(FName Key) const
{
	if (const TObjectPtr<USoundBase>* Sound = LoadedSfx.Find(Key))
	{
		UGameplayStatics::PlaySound2D(this, *Sound, SfxVolume);
	}
}

float UBurgerMiniGameWidget::GetSpeedRatio() const
{
	using namespace BurgerGame;
	const int32 Load = Got.Num() + Junk;
	return 1.f - FMath::Min(MaxLoadSlow, Load * LoadSlowPerItem);
}

void UBurgerMiniGameWidget::AddJunk(int32 Id, int32 Count)
{
	using namespace BurgerGame;
	for (int32 i = 0; i < Count && JunkIds.Num() < MaxJunk; ++i)
	{
		JunkIds.Add(Id);
	}
	Junk = JunkIds.Num();
}

void UBurgerMiniGameWidget::ClearJunk()
{
	JunkIds.Reset();
	Junk = 0;
}

void UBurgerMiniGameWidget::StartRound()
{
	ResetRound();
	bLeftHeld = false;
	bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;
	bSkipped = false;
	Mode = EMode::Play;
}

void UBurgerMiniGameWidget::TogglePause()
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

void UBurgerMiniGameWidget::EndRound()
{
	Mode = EMode::Result;
	bLeftHeld = false;
	bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	FMiniGameResult Result;
	Result.Score = Orders;
	FinalReward = FinishGame(Result);
	PlaySfx(TEXT("shift_end"));
}

void UBurgerMiniGameWidget::SkipShift()
{
	// 점수 0으로 정산 → 0점으로 끝낸 것과 같은 최소 보상 (돈 고정 + 손재주 +1). 플레이 횟수에는 안 센다.
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

void UBurgerMiniGameWidget::DoButton(EButtonAction Action)
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
			SetPracticeStep(PracticeStep + 1); // 연습 판 설명 단계의 "알겠어요"
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
	}
}

void UBurgerMiniGameWidget::ChangeCardPage(int32 Delta)
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

// ---------------------------------------------------------------------------
// 연습 판
//   0 빵 세트 받기 → 1 패티 받기 → 2 Space로 제출 → 3 다른 재료 3개 받아 보기(트레이에 쌓임)
//   → 4 상한 패티 받아 보기 → 5 무거운 채로 움직여 보기(느림) → 6 Space로 치우기
//   → 7 가벼운 채로 움직여 보기(빠름) → 8 준비 끝 (잠깐 뒤 실제 근무 시작)
// 시간 제한 · 무작위 재료 없이 단계마다 재료 하나만 떨어뜨린다. 놓치면 다시 떨어뜨린다.
// ---------------------------------------------------------------------------

void UBurgerMiniGameWidget::StartPractice()
{
	ResetRound();
	bLeftHeld = false;
	bRightHeld = false;
	bSkipped = false;
	Mode = EMode::Practice;
	SetPracticeStep(0);
}

void UBurgerMiniGameWidget::SetPracticeStep(int32 Step)
{
	PracticeStep = Step;
	PracticeStepTime = 0.f;
	PracticeMoved = 0.f;
	Items.Reset();
}

void UBurgerMiniGameWidget::UpdatePractice(float Dt)
{
	using namespace BurgerGame;

	PracticeStepTime += Dt;

	const int32 WrongCount = Errors - SpoiledCount;

	// 단계 조건
	switch (PracticeStep)
	{
	case 0: if (Got.Contains(0))                     { SetPracticeStep(1); } break;
	case 1: if (Missing().Num() == 0)                { SetPracticeStep(2); } break;
	case 2: if (Orders > 0)                          { SetPracticeStep(3); } break;
	case 3: if (WrongCount >= 3)                     { SetPracticeStep(4); } break;
	case 4: if (SpoiledCount > 0)                    { SetPracticeStep(5); } break;
	case 5: break; // 손해 내역 확인 - 키 입력이나 버튼으로만 넘어간다
	case 6: if (PracticeMoved >= PracticeMoveDistance && Stun <= 0.f) { SetPracticeStep(7); } break;
	case 7: if (Junk == 0 && Serving <= 0.f)         { SetPracticeStep(8); } break;
	case 8: if (PracticeMoved >= PracticeMoveDistance) { SetPracticeStep(9); } break;
	case 9:
		if (PracticeStepTime >= PracticeFinishDelay)
		{
			StartRound();
			return;
		}
		break;
	default: break;
	}

	// 제출하면 주문이 무작위로 바뀌므로 연습 중에는 클래식 버거로 고정
	RecipeIndex = 0;

	// 이번 단계의 재료를 하나만 떨어뜨린다 (놓쳐서 사라지면 다시)
	if (Items.Num() == 0 && Serving <= 0.f && PracticeStepTime > 0.4f)
	{
		const int32 WrongIds[] = { 2, 3, 4 }; // 클래식 버거에 없는 치즈 · 양상추 · 토마토
		int32 Id = INDEX_NONE;
		float X = 0.f;
		switch (PracticeStep)
		{
		// 처음엔 일부러 떨어진 곳에 떨어뜨려 움직이게 만든다
		case 0: Id = 0; X = PlayerX < 420.f ? 700.f : 140.f; break;
		case 1: Id = 1; X = PlayerX < 420.f ? 640.f : 200.f; break;
		// 잘못된 재료 · 상한 패티는 받아 보게 하려고 바로 머리 위로 (트레이에 쌓이는 걸 보여 준다)
		case 3: Id = WrongIds[WrongCount % 3]; X = PlayerX; break;
		case 4: Id = SpoiledId; X = PlayerX; break;
		default: break;
		}
		if (Id != INDEX_NONE)
		{
			Items.Add({ Id, X, SpawnY, PracticeFallSpeed });
		}
	}
}

void UBurgerMiniGameWidget::PressPrimaryButton()
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

TArray<int32> UBurgerMiniGameWidget::Missing() const
{
	const BurgerGame::FRecipe& Recipe = BurgerGame::Recipes[RecipeIndex];
	TArray<int32> Want(Recipe.Parts, Recipe.Count);
	for (const int32 Id : Got)
	{
		Want.RemoveSingle(Id);
	}
	return Want;
}

void UBurgerMiniGameWidget::AddPop(float InX, float InY, const FString& Text, const FLinearColor& Color)
{
	Pops.Add({ FVector2f(InX, InY), Text, Color, 1.f });
}

void UBurgerMiniGameWidget::Burst(float InX, float InY, const FLinearColor& Color)
{
	for (int32 i = 0; i < 16; ++i)
	{
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float Speed = 60.f + FMath::FRand() * 220.f;
		Particles.Add({ FVector2f(InX, InY), FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Speed, Color, 0.7f });
	}
}

void UBurgerMiniGameWidget::Spawn()
{
	using namespace BurgerGame;

	// 3번 중 2번은 지금 필요한 재료를 떨어뜨려 너무 운에 기대지 않게 한다 (HTML 규칙)
	const TArray<int32> Want = Missing();
	int32 Id;
	if (Want.Num() > 0 && (Serial++ % 3) != 2)
	{
		Id = Want[FMath::RandRange(0, Want.Num() - 1)];
	}
	else
	{
		Id = FMath::FRand() < 0.13f ? SpoiledId : FMath::RandRange(0, NumIngredients - 1);
	}
	Items.Add({ Id, 55.f + FMath::FRand() * 730.f, SpawnY, (270.f + T * 3.4f) * FallSpeedScale });
}

void UBurgerMiniGameWidget::CatchItem(int32 Id)
{
	using namespace BurgerGame;

	if (Serving > 0.f)
	{
		return;
	}

	if (Id < NumIngredients && Missing().Contains(Id))
	{
		Got.Add(Id);
		Bounce = 0.2f;
		AddPop(PlayerX, 920.f, TEXT("필요한 재료!"), Hex(TEXT("90dfaf")));
		Burst(PlayerX, 965.f, Hex(Ingredients[Id].Color));
		if (Missing().Num() == 0)
		{
			AddPop(420.f, 780.f, TEXT("완성! Space로 제출"), Hex(TEXT("ffd18d")));
			PlaySfx(TEXT("complete"));
		}
		else
		{
			PlaySfx(TEXT("catch"));
		}
		return;
	}

	++Errors;
	Combo = 0;
	Reaction = TEXT("failure");

	if (Id == SpoiledId)
	{
		// 상한 패티: 완성 -1, 적재량 +2, 잠깐 못 움직임
		++SpoiledCount;
		const int32 Lost = FMath::Min(Orders, SpoiledScorePenalty);
		Orders -= Lost;
		AddJunk(SpoiledId, SpoiledJunk);
		Stun = SpoiledStunSeconds;
		ReactionUntil = T + SpoiledFaceSeconds;
		AddPop(PlayerX, 925.f, Lost > 0 ? TEXT("상한 패티! 완성 -1") : TEXT("상한 패티! 배탈 났어요"), Hex(TEXT("ff5a6e")));
		Burst(PlayerX, 965.f, Hex(TEXT("7d9a4a")));
		PlaySfx(TEXT("spoiled"));
		return;
	}

	// 다른 주문 재료: 점수 차감 없이 콤보 초기화 + 적재량 +1 (HTML 그대로)
	AddJunk(Id, 1);
	ReactionUntil = T + FailureFaceSeconds;
	AddPop(PlayerX, 925.f, TEXT("다른 주문 재료예요"), Hex(TEXT("ff8592")));
	PlaySfx(TEXT("wrong"));
}

void UBurgerMiniGameWidget::Action()
{
	using namespace BurgerGame;

	if (Serving > 0.f)
	{
		return;
	}

	if (Missing().Num() == 0)
	{
		++Orders;
		++Combo;
		Best = FMath::Max(Best, Combo);
		AddPop(PlayerX, 900.f, TEXT("주문 완료!"), Hex(TEXT("ffd18d")));
		Burst(PlayerX, 945.f, Hex(TEXT("ffcf78")));
		Got.Reset();
		ClearJunk();
		Serving = SubmitPause;
		// 두 주문마다 한 단계씩 어려운 버거가 섞인다
		RecipeIndex = FMath::Min(NumRecipes - 1, Orders / 2 + FMath::RandRange(0, 1));
		Reaction = TEXT("success");
		ReactionUntil = T + SuccessFaceSeconds;
		PlaySfx(TEXT("submit_ok"));
	}
	else if (Junk > 0)
	{
		ClearJunk();
		Combo = 0;
		Serving = CleanPause;
		AddPop(PlayerX, 900.f, TEXT("불필요한 재료 정리"), Hex(TEXT("b7c6d8")));
		PlaySfx(TEXT("submit"));
	}
	else
	{
		AddPop(PlayerX, 900.f, TEXT("주문 재료가 더 필요해요"), Hex(TEXT("ff8592")));
		PlaySfx(TEXT("submit"));
	}
}

void UBurgerMiniGameWidget::Update(float Dt)
{
	using namespace BurgerGame;

	const bool bPractice = Mode == EMode::Practice;

	if (bPractice)
	{
		// 연습 판은 시간 제한이 없다. T는 걷기·표정 애니메이션용으로만 흐른다.
		T += Dt;
	}
	else
	{
		T = FMath::Min(Duration, T + Dt);
		if (T >= Duration)
		{
			EndRound();
			return;
		}
		if (!bRushAnnounced && T >= Duration - RushSeconds)
		{
			bRushAnnounced = true;
			PlaySfx(TEXT("rush"));
		}
	}

	Serving = FMath::Max(0.f, Serving - Dt);
	Bounce = FMath::Max(0.f, Bounce - Dt);
	Stun = FMath::Max(0.f, Stun - Dt);

	// 트레이가 무거울수록 느려진다 (발걸음 애니메이션도 같은 비율로 느려진다)
	const float Ratio = GetSpeedRatio();
	const int32 Dir = (bRightHeld ? 1 : 0) - (bLeftHeld ? 1 : 0);
	if (Serving <= 0.f && Stun <= 0.f)
	{
		const float OldX = PlayerX;
		PlayerX = FMath::Clamp(PlayerX + Dir * MoveSpeed * Ratio * Dt, MinX, MaxX);
		PracticeMoved += FMath::Abs(PlayerX - OldX);
		if (Dir != 0)
		{
			WalkPhase += Dt * 7.f * Ratio;
		}
	}

	if (bPractice)
	{
		UpdatePractice(Dt);
		if (Mode != EMode::Practice)
		{
			return; // 연습이 끝나 실제 근무가 시작됐다
		}
	}
	else
	{
		SpawnTimer -= Dt;
		if (SpawnTimer <= 0.f)
		{
			Spawn();
			SpawnTimer = T >= Duration - RushSeconds ? RushSpawnInterval : SpawnInterval;
		}
	}

	for (FFallingItem& Item : Items)
	{
		const float OldY = Item.Y;
		Item.Y += Item.Speed * Dt;
		// 트레이 높이 띠를 이번 프레임에 지나갔으면 받은 것으로 친다 (빠르게 떨어져도 놓치지 않게)
		if (Serving <= 0.f && OldY < CatchBottom && Item.Y >= CatchTop && FMath::Abs(Item.X - PlayerX) < CatchHalfWidth)
		{
			CatchItem(Item.Id);
			Item.Y = 1400.f;
		}
	}
	Items.RemoveAll([](const FFallingItem& Item) { return Item.Y >= 1300.f; });

	UpdateEffects(Dt);
}

void UBurgerMiniGameWidget::UpdateEffects(float Dt)
{
	for (FPopText& Pop : Pops)
	{
		Pop.Life -= Dt;
		Pop.Pos.Y -= 55.f * Dt;
	}
	Pops.RemoveAll([](const FPopText& Pop) { return Pop.Life <= 0.f; });

	for (FParticle& Part : Particles)
	{
		Part.Life -= Dt;
		Part.Pos += Part.Vel * Dt;
		Part.Vel.Y += 180.f * Dt;
	}
	Particles.RemoveAll([](const FParticle& Part) { return Part.Life <= 0.f; });
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------

void UBurgerMiniGameWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 프레임이 튀어도 재료가 트레이를 뚫고 지나가지 않게 한 번에 0.05초까지만 진행한다
	if (Mode == EMode::Play || Mode == EMode::Practice)
	{
		Update(FMath::Min(InDeltaTime, 0.05f));
	}
	else if (Mode == EMode::Result || Mode == EMode::Title)
	{
		// 결과·타이틀 뒤에서도 팝업·조각이 끝까지 흩어지게
		UpdateEffects(FMath::Min(InDeltaTime, 0.05f));
	}
}

FReply UBurgerMiniGameWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// 설명 카드에서는 좌우 키로 앞뒤 장을 넘긴다
	if (Mode == EMode::Tutorial && !InKeyEvent.IsRepeat())
	{
		if (Key == EKeys::Left || Key == EKeys::A)
		{
			ChangeCardPage(-1);
			return FReply::Handled();
		}
		if (Key == EKeys::Right || Key == EKeys::D)
		{
			ChangeCardPage(1);
			return FReply::Handled();
		}
		if (Key == EKeys::Escape)
		{
			Mode = EMode::Title;
			return FReply::Handled();
		}
	}

	if (Key == EKeys::Left || Key == EKeys::A)
	{
		bLeftHeld = true;
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D)
	{
		bRightHeld = true;
		return FReply::Handled();
	}
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
		if (Mode == EMode::Practice && PracticeStep == BurgerGame::SpoiledExplainStep)
		{
			// 손해 내역 확인 단계에서는 Space가 정리가 아니라 "알겠어요"
			// (정리는 무거운 채로 움직여 본 다음 단계에서 배운다)
			PressPrimaryButton();
		}
		else if (Mode == EMode::Play || Mode == EMode::Practice)
		{
			Action();
		}
		else
		{
			PressPrimaryButton();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UBurgerMiniGameWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || Key == EKeys::A)
	{
		bLeftHeld = false;
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D)
	{
		bRightHeld = false;
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply UBurgerMiniGameWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PressedButton = FindButtonAt(ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	}
	// 클릭해도 키보드 포커스를 잃지 않게 붙잡아 둔다
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply UBurgerMiniGameWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply UBurgerMiniGameWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	HoveredButton = FindButtonAt(ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition()));
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UBurgerMiniGameWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnFocusLost(InFocusEvent);

	// 창을 벗어나면 키를 뗀 것으로 보고 멈춘다 (HTML의 blur 처리)
	bLeftHeld = false;
	bRightHeld = false;
	if (Mode == EMode::Play)
	{
		TogglePause();
	}
}

// ---------------------------------------------------------------------------
// 화면
// ---------------------------------------------------------------------------

void UBurgerMiniGameWidget::GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset)
{
	using namespace BurgerGame;
	OutScale = FMath::Max(0.01f, FMath::Min(LocalSize.Y / ScreenH, LocalSize.X / ScreenW));
	OutOffset = (LocalSize - FVector2f(ScreenW, ScreenH) * OutScale) * 0.5f;
}

FVector2f UBurgerMiniGameWidget::ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos)
{
	float Scale;
	FVector2f Offset;
	const FVector2f LocalSize = Geometry.GetLocalSize();
	GetCanvasTransform(LocalSize, Scale, Offset);
	const FVector2f Local = Geometry.AbsoluteToLocal(ScreenPos);
	return (Local - Offset) / Scale;
}

TArray<UBurgerMiniGameWidget::FOverlayButton> UBurgerMiniGameWidget::GetOverlayButtons() const
{
	using namespace BurgerGame;

	TArray<FOverlayButton> Buttons;
	auto Add = [&Buttons](float X, float Y, float W, const TCHAR* Label, EButtonAction Action, bool bPrimary)
	{
		FOverlayButton& Button = Buttons.AddDefaulted_GetRef();
		Button.X = X;
		Button.Y = Y;
		Button.W = W;
		Button.H = bPrimary ? ButtonH : 60.f;
		Button.Label = Label;
		Button.Action = Action;
		Button.bPrimary = bPrimary;
	};
	const float CX = CanvasW * 0.5f;

	switch (Mode)
	{
	case EMode::Title:
		if (PlayCount == 0)
		{
			Add(CX, 880.f, ButtonW, TEXT("출근하기"), EButtonAction::Start, true);
		}
		else
		{
			// 한 번 해 본 뒤에는 건너뛰기 · 연습 · 설명을 연다
			Add(CX, 800.f, ButtonW, TEXT("출근하기"), EButtonAction::Start, true);
			Add(CX, 900.f, 300.f, TEXT("대충 일하고 퇴근"), EButtonAction::SkipShift, false);
			Add(CX - 125.f, 980.f, 230.f, TEXT("연습하기"), EButtonAction::StartPractice, false);
			Add(CX + 125.f, 980.f, 230.f, TEXT("설명 보기"), EButtonAction::ShowCards, false);
		}
		break;
	case EMode::Practice:
		if (PracticeStep == SpoiledExplainStep)
		{
			Add(CX, 760.f, ButtonW, TEXT("알겠어요"), EButtonAction::NextPage, true);
		}
		// 연습 중에는 게임 화면 아래쪽에 작게
		Add(CX, 1175.f, 240.f, TEXT("연습 건너뛰기"), EButtonAction::SkipPractice, false);
		break;
	case EMode::Tutorial:
		if (TutorialPage > 0)
		{
			Add(CX - 175.f, 1010.f, 200.f, TEXT("이전"), EButtonAction::PrevPage, false);
		}
		Add(TutorialPage > 0 ? CX + 70.f : CX, 1010.f, 260.f, TutorialPage < 2 ? TEXT("다음") : TEXT("닫기"), EButtonAction::NextPage, true);
		if (TutorialPage < 2)
		{
			Add(CX, 1100.f, 200.f, TEXT("닫기"), EButtonAction::CloseCards, false);
		}
		break;
	case EMode::Pause:
		Add(CX, 700.f, ButtonW, TEXT("계속하기"), EButtonAction::Resume, true);
		break;
	case EMode::Result:
		Add(CX, 960.f, ButtonW, TEXT("퇴근하기"), EButtonAction::Exit, true);
		break;
	default:
		break;
	}
	return Buttons;
}

int32 UBurgerMiniGameWidget::FindButtonAt(const FVector2f& ScreenPos) const
{
	using namespace BurgerGame;

	// 버튼 좌표는 플레이 영역 기준이다
	const FVector2f Local(ScreenPos.X - PlayX, ScreenPos.Y);
	const TArray<FOverlayButton> Buttons = GetOverlayButtons();
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FOverlayButton& B = Buttons[i];
		if (FMath::Abs(Local.X - B.X) <= B.W * 0.5f && FMath::Abs(Local.Y - B.Y) <= B.H * 0.5f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FName UBurgerMiniGameWidget::CurrentPose() const
{
	if (!Reaction.IsNone() && T < ReactionUntil)
	{
		return Reaction;
	}
	const int32 Dir = (bRightHeld ? 1 : 0) - (bLeftHeld ? 1 : 0);
	if ((Mode == EMode::Play || Mode == EMode::Practice) && Dir != 0 && Serving <= 0.f && Stun <= 0.f)
	{
		return *FString::Printf(TEXT("walk-%s-%d"), Dir < 0 ? TEXT("left") : TEXT("right"), FMath::FloorToInt(WalkPhase) % 4);
	}
	// 3초마다 0.14초 눈 깜빡임
	if (FMath::Fmod(T, 3.f) > 2.86f)
	{
		return TEXT("blink");
	}
	return TEXT("idle");
}

int32 UBurgerMiniGameWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace BurgerGame;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	float Scale;
	FVector2f Offset;
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	GetCanvasTransform(LocalSize, Scale, Offset);

	// 가로형 화면 전체 (좌우 패널) → 가운데 플레이 영역 순서로 그린다.
	// 플레이 영역은 HTML 캔버스 좌표를 그대로 쓰도록 자식 지오메트리로 옮긴다.
	FBurgerPainter Screen{ OutDrawElements, AllottedGeometry.MakeChild(FVector2f(ScreenW, ScreenH), FSlateLayoutTransform(Scale, Offset)), BaseLayer, &SolidBrush };
	PaintSidePanels(Screen);

	FBurgerPainter P{ OutDrawElements, Screen.Canvas.MakeChild(FVector2f(CanvasW, CanvasH), FSlateLayoutTransform(FVector2f(PlayX, 0.f))), Screen.Layer, &SolidBrush };
	PaintGame(P);
	if (Mode == EMode::Practice)
	{
		PaintPractice(P);
	}
	else if (Mode != EMode::Play)
	{
		PaintOverlay(P);
	}
	P.Brush(&FrameBrush, 0.f, 0.f, CanvasW, CanvasH);
	return P.Layer;
}

void UBurgerMiniGameWidget::PaintGame(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	const bool bRush = T >= Duration - RushSeconds;

	// 배경. 주문판·상태판은 좌우 패널로 옮겼으므로 플레이 영역은 위아래를 다 쓴다.
	P.Brush(FindBrush(TEXT("kitchen")), 0.f, 0.f, CanvasW, CanvasH);

	if (bRush)
	{
		P.Text(TEXT("마감까지 10초! 마지막 주문을 부탁해요"), 420.f, 70.f, 26.f, Hex(TEXT("bd6553")), EAlign::Center, true, 2);
	}

	// 떨어지는 재료
	for (const FFallingItem& Item : Items)
	{
		const TCHAR* Asset = Item.Id == SpoiledId ? TEXT("spoiled_patty") : Ingredients[Item.Id].Asset;
		P.ArtCentered(FindBrush(Asset), Item.X, Item.Y, 86.f);
	}

	// 알바생과 트레이
	const float Bump = Bounce * 12.f;
	const int32 Load = Got.Num() + Junk;
	// 무거울수록 트레이 위 재료가 눌려 내려앉는다
	const float Sag = Load * 3.f;
	P.ArtCentered(FindBrush(CurrentPose()), PlayerX, 900.f - Bump, 320.f);

	int32 Stack = 0;
	if (Missing().Num() == 0)
	{
		P.ArtCentered(FindBrush(TEXT("finished_burger")), PlayerX, 928.f - Bump + Sag, 105.f);
		Stack = 2;
	}
	else
	{
		for (int32 j = 0; j < Got.Num(); ++j)
		{
			P.ArtCentered(FindBrush(Ingredients[Got[j]].Asset), PlayerX, 947.f - Stack * 18.f - Bump + Sag, 70.f);
			++Stack;
		}
	}
	// 잘못 받은 재료도 트레이 위에 그대로 쌓아 보여 준다 (살짝 어둡게)
	for (const int32 Id : JunkIds)
	{
		const TCHAR* Asset = Id == SpoiledId ? TEXT("spoiled_patty") : Ingredients[Id].Asset;
		P.Brush(FindBrush(Asset), PlayerX - 35.f, 947.f - Stack * 18.f - Bump + Sag - 35.f, 70.f, 70.f, FLinearColor(0.75f, 0.7f, 0.7f, 1.f));
		++Stack;
	}

	// 무거우면 머리 위에 속도를 보여 준다
	if (Load >= HeavyWarnLoad || Stun > 0.f)
	{
		const float Ratio = GetSpeedRatio();
		const FString Warn = Stun > 0.f ? TEXT("배탈! 못 움직여요")
			: FString::Printf(TEXT("무거워요! 속도 %d%%"), FMath::RoundToInt(Ratio * 100.f));
		const float WarnY = 700.f - Stack * 18.f;
		P.Text(Warn, PlayerX, WarnY, 24.f, Hex(TEXT("c14b40")), EAlign::Center, true, 2);
		P.Brush(&BarBackBrush, PlayerX - 70.f, WarnY + 22.f, 140.f, 12.f);
		P.Brush(&BarFillBrush, PlayerX - 70.f, WarnY + 22.f, 140.f * (Stun > 0.f ? 0.f : Ratio), 12.f);
	}
	if (Junk > 0)
	{
		P.Text(FString::Printf(TEXT("정리할 재료 %d개 · Space"), Junk), PlayerX, 1060.f, 20.f, Hex(TEXT("b1574c")), EAlign::Center, true, 2);
	}

	// 효과
	for (const FParticle& Part : Particles)
	{
		P.Box(Part.Pos.X, Part.Pos.Y, 7.f, 7.f, WithAlpha(Part.Color, Part.Life));
	}
	for (const FPopText& Pop : Pops)
	{
		// HTML 색은 어두운 배경용이라 밝은 주방 배경에서는 외곽선을 둘러 읽히게 한다
		P.Text(Pop.Text, FMath::Clamp(Pop.Pos.X, 160.f, 680.f), Pop.Pos.Y, 28.f, WithAlpha(Pop.Color, Pop.Life), EAlign::Center, false, 2);
	}
}

void UBurgerMiniGameWidget::PaintSidePanels(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	const bool bRush = T >= Duration - RushSeconds;
	const FLinearColor Line = Hex(TEXT("dbc6ae"));

	// ---- 왼쪽: 시간 · 점수 · 주문 · 조작 ----
	{
		const float L = LeftPanelX + 44.f;
		P.Brush(&PanelBrush, LeftPanelX, 40.f, PanelW, 1160.f);
		P.Text(TEXT("PART-TIME / BURGER SHOP"), L, 100.f, 20.f, Hex(TEXT("b56b55")));
		P.Text(TEXT("BURGER DAY"), L, 150.f, 44.f, Coral(), EAlign::Left, true);

		P.Text(TEXT("남은 시간"), L, 240.f, 22.f, Muted());
		if (Mode == EMode::Practice)
		{
			P.Text(TEXT("연습"), L, 310.f, 88.f, Green(), EAlign::Left, true);
		}
		else
		{
			P.Text(FString::Printf(TEXT("%d초"), FMath::CeilToInt(Duration - T)), L, 310.f, 88.f,
				bRush ? Hex(TEXT("c14b40")) : Ink(), EAlign::Left, true);
		}
		P.Text(FString::Printf(TEXT("완성 %d개  ·  연속 %d"), Orders, Combo), L, 400.f, 30.f, Ink());

		// 주문판 (원본 792×176 비율)
		const FRecipe& Recipe = Recipes[RecipeIndex];
		P.Brush(FindBrush(TEXT("order-panel")), LeftPanelX + 24.f, 455.f, 552.f, 123.f);
		P.Text(TEXT("지금 주문"), L, 492.f, 20.f, Muted());
		P.Text(Recipe.Name, L, 535.f, 34.f, Ink(), EAlign::Left, true);

		TArray<int32> Unused = Got;
		for (int32 j = 0; j < Recipe.Count; ++j)
		{
			const int32 Id = Recipe.Parts[j];
			const bool bDone = Unused.RemoveSingle(Id) > 0;
			const float X = L + j * 128.f;
			P.Brush(bDone ? &SlotDoneBrush : &SlotWaitBrush, X, 605.f, 112.f, 125.f);
			P.ArtCentered(FindBrush(Ingredients[Id].Asset), X + 56.f, 652.f, 84.f);
			P.Text(bDone ? TEXT("완료") : Ingredients[Id].Name, X + 56.f, 708.f, 20.f, bDone ? Green() : Hex(TEXT("8f7868")), EAlign::Center, bDone);
		}
		P.Text(TEXT("순서 상관없이 · 빵은 세트 1개면 돼요"), L, 770.f, 20.f, Muted());

		P.Box(L, 860.f, PanelW - 88.f, 2.f, Line);
		P.Text(TEXT("조작"), L, 905.f, 24.f, Ink(), EAlign::Left, true);
		P.Text(TEXT("방향키 / A · D"), L, 955.f, 22.f, Ink());
		P.Text(TEXT("이동"), L + PanelW - 88.f, 955.f, 22.f, Muted(), EAlign::Right);
		P.Text(TEXT("Space"), L, 1000.f, 22.f, Ink());
		P.Text(TEXT("제출 · 정리"), L + PanelW - 88.f, 1000.f, 22.f, Muted(), EAlign::Right);
		P.Text(TEXT("P / Esc"), L, 1045.f, 22.f, Ink());
		P.Text(TEXT("일시정지"), L + PanelW - 88.f, 1045.f, 22.f, Muted(), EAlign::Right);
	}

	// ---- 오른쪽: 상태 · 보상 · 트레이 · 기록 ----
	{
		const float R = RightPanelX + 44.f;
		const float RightEdge = RightPanelX + PanelW - 44.f;
		P.Brush(&PanelBrush, RightPanelX, 40.f, PanelW, 1160.f);
		P.Text(TEXT("오늘의 근무"), R, 110.f, 30.f, Ink(), EAlign::Left, true);

		// 상태판 (원본 800×100 비율)
		P.Brush(FindBrush(TEXT("status-panel")), RightPanelX + 24.f, 160.f, 552.f, 69.f);
		const FString Status = Mode == EMode::Practice ? TEXT("연습 중 - 시간 제한 없어요")
			: Mode != EMode::Play ? TEXT("근무 준비 중")
			: Stun > 0.f ? TEXT("배탈! 잠깐 못 움직여요")
			: Serving > 0.f ? TEXT("포장 중이에요")
			: Missing().Num() == 0 ? TEXT("주문 완성! Space로 제출해 주세요")
			: Junk > 0 ? TEXT("불필요한 재료는 Space로 치워요")
			: TEXT("주문판의 재료를 트레이로 받아 주세요");
		P.Text(Status, RightPanelX + PanelW * 0.5f, 194.f, 22.f, Ink(), EAlign::Center);

		// 예상 보상 + 다음 단계까지 몇 개 남았는지
		const FMiniGameReward Preview = PreviewReward(Orders);
		P.Text(TEXT("예상 보상"), R, 300.f, 22.f, Muted());
		P.Text(FString::Printf(TEXT("손재주 +%d"), Preview.StatAmount), R, 362.f, 48.f, Coral(), EAlign::Left, true);
		P.Text(FString::Printf(TEXT("급여 %s원 (고정)"), *FString::FormatAsNumber(Preview.Money)), R, 428.f, 26.f, Ink());
		for (int32 More = 1; More <= 20; ++More)
		{
			if (PreviewReward(Orders + More).StatAmount > Preview.StatAmount)
			{
				P.Text(FString::Printf(TEXT("%d개 더 완성하면 +%d"), More, Preview.StatAmount + 1), R, 470.f, 22.f, Muted());
				break;
			}
		}

		// 트레이 적재량
		const int32 Load = Got.Num() + Junk;
		P.Text(TEXT("트레이 적재량"), R, 560.f, 24.f, Ink(), EAlign::Left, true);
		P.Text(FString::Printf(TEXT("%d / 10 · 속도 %d%%"), Load, FMath::RoundToInt(GetSpeedRatio() * 100.f)), RightEdge, 560.f, 22.f,
			Load >= HeavyWarnLoad ? Hex(TEXT("c14b40")) : Muted(), EAlign::Right);
		P.Brush(&BarBackBrush, R, 592.f, RightEdge - R, 24.f);
		const float Fill = (RightEdge - R) * FMath::Min(1.f, Load / 10.f);
		if (Fill > 0.f)
		{
			P.Brush(&BarFillBrush, R, 592.f, Fill, 24.f);
		}
		P.Text(Junk > 0 ? FString::Printf(TEXT("정리할 재료 %d개 · Space로 치워요"), Junk) : FString(TEXT("가벼울수록 빨리 움직여요")),
			R, 650.f, 22.f, Junk > 0 ? Hex(TEXT("b1574c")) : Muted());

		// 기록
		P.Box(R, 720.f, RightEdge - R, 2.f, Line);
		P.Text(TEXT("최고 연속"), R, 770.f, 24.f, Ink());
		P.Text(FString::Printf(TEXT("%d개"), Best), RightEdge, 770.f, 24.f, Ink(), EAlign::Right, true);
		P.Text(TEXT("불필요한 재료"), R, 820.f, 24.f, Ink());
		P.Text(FString::Printf(TEXT("%d회"), Errors - SpoiledCount), RightEdge, 820.f, 24.f, Ink(), EAlign::Right, true);
		P.Text(TEXT("상한 패티"), R, 870.f, 24.f, Ink());
		P.Text(FString::Printf(TEXT("%d회"), SpoiledCount), RightEdge, 870.f, 24.f, SpoiledCount > 0 ? Hex(TEXT("c14b40")) : Ink(), EAlign::Right, true);

		P.Text(TEXT("상한 패티는 완성 -1 · 잠깐 못 움직여요"), R, 1040.f, 20.f, Hex(TEXT("b1574c")));
		P.Text(TEXT("완성하면 바로 제출해야 다시 가벼워져요"), R, 1080.f, 20.f, Muted());
		P.Text(TEXT("마지막 10초에는 재료가 더 자주 떨어져요"), R, 1120.f, 20.f, Muted());
	}
}

void UBurgerMiniGameWidget::PaintOverlay(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	P.Brush(&OverlayBrush, 0.f, 0.f, CanvasW, CanvasH);
	const FLinearColor Tag = Hex(TEXT("b56b55"));

	if (Mode == EMode::Title)
	{
		const FMiniGameReward Reward = PreviewReward(0);
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 300.f, 19.f, Tag, EAlign::Center);
		P.Text(TEXT("오늘도 맛있게, 버거 데이"), 420.f, 370.f, 42.f, Ink(), EAlign::Center, true);
		P.Text(TEXT("방향키 또는 A · D로 이동해 필요한 재료를 받으세요."), 420.f, 460.f, 26.f, Ink(), EAlign::Center);
		P.Text(TEXT("Space로 완성 주문 제출 · 불필요한 재료 정리"), 420.f, 510.f, 26.f, Ink(), EAlign::Center);
		P.Text(FString::Printf(TEXT("%d초 근무 · P / Esc 일시정지"), FMath::RoundToInt(Duration)), 420.f, 560.f, 26.f, Ink(), EAlign::Center);
		P.Text(FString::Printf(TEXT("급여 %s원 · 많이 완성할수록 손재주가 올라요"), *FString::FormatAsNumber(Reward.Money)),
			420.f, 650.f, 24.f, Coral(), EAlign::Center);

		if (PlayCount == 0)
		{
			P.Text(TEXT("처음이라 출근 전에 짧게 연습해 볼게요."), 420.f, 760.f, 22.f, Muted(), EAlign::Center);
		}
		else
		{
			P.Text(TEXT("대충 일하면 급여는 같고 손재주는 +1만 올라요."), 420.f, 1060.f, 20.f, Muted(), EAlign::Center);
		}
	}
	else if (Mode == EMode::Tutorial)
	{
		PaintTutorialPage(P);
	}
	else if (Mode == EMode::Pause)
	{
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 470.f, 19.f, Tag, EAlign::Center);
		P.Text(TEXT("잠깐, 쉬는 시간"), 420.f, 540.f, 42.f, Ink(), EAlign::Center, true);
		P.Text(TEXT("시간이 멈췄습니다."), 420.f, 610.f, 26.f, Ink(), EAlign::Center);
	}
	else if (Mode == EMode::Result)
	{
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 270.f, 19.f, Tag, EAlign::Center);
		if (bSkipped)
		{
			P.Text(TEXT("대충 일하고 퇴근했어요."), 420.f, 340.f, 42.f, Ink(), EAlign::Center, true);
			P.Text(TEXT("오늘은 카운터만 지키다 왔어요."), 420.f, 420.f, 26.f, Ink(), EAlign::Center);
			P.Text(TEXT("직접 일하면 손재주를 더 올릴 수 있어요."), 420.f, 470.f, 26.f, Muted(), EAlign::Center);
		}
		else
		{
			P.Text(TEXT("주방 마감! 수고했어요."), 420.f, 340.f, 42.f, Ink(), EAlign::Center, true);
			P.Text(FString::Printf(TEXT("완성 %d개 · 최고 연속 %d개"), Orders, Best), 420.f, 420.f, 26.f, Ink(), EAlign::Center);
			P.Text(FString::Printf(TEXT("불필요한 재료 %d회 · 상한 패티 %d회"), Errors - SpoiledCount, SpoiledCount), 420.f, 470.f, 26.f, Ink(), EAlign::Center);
		}

		// 영수증
		P.Brush(&ReceiptBrush, 170.f, 540.f, 500.f, 300.f);
		P.Text(TEXT("급여"), 210.f, 600.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), 630.f, 600.f, 26.f, Ink(), EAlign::Right, true);
		P.Text(TEXT("손재주"), 210.f, 670.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("+%d"), FinalReward.StatAmount), 630.f, 670.f, 26.f, Ink(), EAlign::Right, true);
		P.Box(210.f, 720.f, 420.f, 2.f, Hex(TEXT("c6aa88")));
		P.Text(TEXT("받은 돈"), 210.f, 780.f, 28.f, Ink(), EAlign::Left, true);
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), 630.f, 780.f, 30.f, Coral(), EAlign::Right, true);
	}

	PaintButtons(P);
}

void UBurgerMiniGameWidget::PaintButtons(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	// 주 버튼은 그림, 보조 버튼은 테두리만 있는 작은 버튼
	const TArray<FOverlayButton> Buttons = GetOverlayButtons();
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FOverlayButton& B = Buttons[i];
		const bool bPressed = PressedButton == i;
		const bool bHovered = HoveredButton == i;
		const float Press = bPressed ? 2.f : 0.f;
		const float Left = B.X - B.W * 0.5f;
		const float Top = B.Y - B.H * 0.5f + Press;

		if (B.bPrimary)
		{
			const TCHAR* ButtonArt = bPressed ? TEXT("button-pressed") : bHovered ? TEXT("button-hover") : TEXT("button-normal");
			P.Brush(FindBrush(ButtonArt), Left, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 26.f, FLinearColor::White, EAlign::Center, true);
		}
		else
		{
			P.Brush(bHovered || bPressed ? &SecondaryHoverBrush : &SecondaryBrush, Left, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 22.f, Coral(), EAlign::Center, true);
		}
	}
}

void UBurgerMiniGameWidget::PaintTutorialPage(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	// 설명 카드 문구. 기획 확정 전 초안 - 문구만 바꾸면 된다.
	const TCHAR* const Titles[] = { TEXT("재료 받기"), TEXT("조심할 재료"), TEXT("주문 제출") };
	const TCHAR* const Lines[3][3] =
	{
		{
			TEXT("주문판에 나온 재료를 트레이로 받아요."),
			TEXT("순서는 상관없고, 빵은 세트 하나면 돼요."),
			TEXT("방향키 또는 A · D로 움직여요."),
		},
		{
			TEXT("주문에 없는 재료는 트레이를 무겁게 해요."),
			TEXT("상한 패티는 완성이 하나 깎이고 잠깐 못 움직여요."),
			TEXT("Space를 누르면 불필요한 재료만 치울 수 있어요."),
		},
		{
			TEXT("재료를 다 모으면 꼭 Space로 제출하세요."),
			TEXT("제출한 버거만 완성으로 쳐요."),
			TEXT("많이 완성할수록 손재주가 더 올라요 (급여는 고정)."),
		},
	};

	const int32 Page = FMath::Clamp(TutorialPage, 0, 2);
	P.Text(FString::Printf(TEXT("일하는 법  %d / 3"), Page + 1), 420.f, 250.f, 20.f, Hex(TEXT("b56b55")), EAlign::Center);
	P.Text(Titles[Page], 420.f, 320.f, 42.f, Ink(), EAlign::Center, true);

	// 그림
	if (Page == 0)
	{
		const TCHAR* const Row[] = { TEXT("bun-set"), TEXT("patty"), TEXT("cheese"), TEXT("lettuce"), TEXT("tomato") };
		for (int32 i = 0; i < UE_ARRAY_COUNT(Row); ++i)
		{
			P.ArtCentered(FindBrush(Row[i]), 420.f + (i - 2) * 140.f, 540.f, 110.f);
		}
	}
	else if (Page == 1)
	{
		P.ArtCentered(FindBrush(TEXT("spoiled_patty")), 290.f, 560.f, 150.f);
		P.ArtCentered(FindBrush(TEXT("failure")), 560.f, 540.f, 280.f);
	}
	else
	{
		P.ArtCentered(FindBrush(TEXT("finished_burger")), 290.f, 560.f, 160.f);
		P.ArtCentered(FindBrush(TEXT("success")), 560.f, 540.f, 280.f);
	}

	for (int32 i = 0; i < 3; ++i)
	{
		P.Text(Lines[Page][i], 420.f, 730.f + i * 50.f, 26.f, i == 2 ? Coral() : Ink(), EAlign::Center);
	}

	// 몇 번째 카드인지 점으로
	for (int32 i = 0; i < 3; ++i)
	{
		const float X = 420.f + (i - 1) * 30.f;
		P.Brush(i == Page ? &BarFillBrush : &BarBackBrush, X - 7.f, 913.f, 14.f, 14.f);
	}
}

void UBurgerMiniGameWidget::PaintPractice(FBurgerPainter& P) const
{
	using namespace BurgerGame;

	// 단계마다 한 줄 안내 + 한 줄 보충. 글을 길게 읽게 하지 않고 직접 해 보면서 익히게 한다.
	const TCHAR* const Prompts[PracticeSteps][2] =
	{
		{ TEXT("방향키(A · D)로 움직여 빵 세트를 받아요"), TEXT("떨어지는 재료를 트레이로 받으면 돼요") },
		{ TEXT("패티도 받아요"), TEXT("주문판의 재료를 다 모으면 버거가 완성돼요") },
		{ TEXT("Space를 눌러 제출해요"), TEXT("제출해야 완성 수가 올라요") },
		{ TEXT("주문에 없는 재료를 일부러 받아 보세요"), TEXT("받은 재료가 트레이에 그대로 쌓여요") },
		{ TEXT("상한 패티도 받아 보세요"), TEXT("받으면 어떻게 되는지 직접 확인해 봐요") },
		{ TEXT("으앗, 상한 패티를 먹었어요!"), TEXT("상한 패티는 이만큼 손해예요") },
		{ TEXT("좌우로 움직여 보세요 - 느리죠?"), TEXT("트레이가 무거울수록 걸음이 느려져요") },
		{ TEXT("Space로 불필요한 재료를 치워요"), TEXT("주문을 다 못 모았을 때 Space는 정리예요") },
		{ TEXT("다시 움직여 보세요 - 빨라졌죠?"), TEXT("가볍게 유지하는 게 많이 완성하는 비결이에요") },
		{ TEXT("준비 끝! 곧 출근해요"), TEXT("") },
	};

	const int32 Step = FMath::Clamp(PracticeStep, 0, PracticeSteps - 1);
	const float Pulse = 0.6f + 0.4f * FMath::Sin(PracticeStepTime * 6.f);

	// 안내판
	P.Brush(&PanelBrush, 50.f, 140.f, CanvasW - 100.f, 150.f);
	P.Text(FString::Printf(TEXT("연습  %d / %d"), FMath::Min(Step + 1, PracticeSteps - 1), PracticeSteps - 1), 420.f, 172.f, 20.f, Hex(TEXT("b56b55")), EAlign::Center);
	P.Text(Prompts[Step][0], 420.f, 218.f, 32.f, Ink(), EAlign::Center, true);
	P.Text(Prompts[Step][1], 420.f, 262.f, 22.f, Muted(), EAlign::Center);

	// 받아야 할 재료를 둥근 테두리로 표시
	for (const FFallingItem& Item : Items)
	{
		P.Brush(&HighlightBrush, Item.X - 65.f, Item.Y - 65.f, 130.f, 130.f, WithAlpha(FLinearColor::White, Pulse));
	}

	// Space 단계에서는 키 모양을 크게 깜빡인다
	if (Step == 2 || Step == 7)
	{
		P.Brush(&SecondaryBrush, 270.f, 520.f, 300.f, 90.f, WithAlpha(FLinearColor::White, Pulse));
		P.Text(TEXT("Space"), 420.f, 565.f, 40.f, WithAlpha(Coral(), Pulse), EAlign::Center, true);
	}

	// 상한 패티 손해 내역: 방금 겪은 걸 항목별로 짚어 준다
	if (Step == SpoiledExplainStep)
	{
		const FLinearColor Bad = Hex(TEXT("c14b40"));
		P.Brush(&ReceiptBrush, 90.f, 320.f, CanvasW - 180.f, 390.f);
		P.ArtCentered(FindBrush(TEXT("spoiled_patty")), 170.f, 385.f, 100.f);
		P.Text(TEXT("상한 패티를 먹으면"), 240.f, 385.f, 30.f, Bad, EAlign::Left, true);

		struct FRow { const TCHAR* Label; FString Value; };
		const FRow Rows[] =
		{
			{ TEXT("완성 수"), TEXT("1개에서 0개로  (-1)") },
			{ TEXT("트레이"), FString::Printf(TEXT("2칸 무거워짐  (속도 %d%%)"), FMath::RoundToInt(GetSpeedRatio() * 100.f)) },
			{ TEXT("몸 상태"), TEXT("배탈! 0.6초 동안 못 움직여요") },
			{ TEXT("연속 기록"), TEXT("0으로 초기화") },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
		{
			const float Y = 470.f + i * 52.f;
			P.Text(Rows[i].Label, 130.f, Y, 24.f, Ink(), EAlign::Left, true);
			P.Text(Rows[i].Value, CanvasW - 130.f, Y, 24.f, Bad, EAlign::Right, true);
		}
		P.Text(TEXT("Space 또는 \"알겠어요\"를 눌러 계속"), 420.f, 820.f, 20.f, Muted(), EAlign::Center);
	}

	// 움직여 보기 단계: 지금 속도를 큰 게이지로, 얼마나 더 움직이면 되는지 함께
	if (Step == 6 || Step == 8)
	{
		const float Ratio = Stun > 0.f ? 0.f : GetSpeedRatio();
		const bool bSlow = Ratio < 0.95f;
		P.Brush(&PanelBrush, 170.f, 340.f, 500.f, 150.f);
		P.Text(TEXT("이동 속도"), 200.f, 375.f, 22.f, Muted());
		P.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Ratio * 100.f)), 640.f, 378.f, 40.f,
			bSlow ? Hex(TEXT("c14b40")) : Green(), EAlign::Right, true);
		P.Brush(&BarBackBrush, 200.f, 415.f, 440.f, 26.f);
		P.Brush(bSlow ? &BarFillBrush : &SlotDoneBrush, 200.f, 415.f, 440.f * Ratio, 26.f);
		const float Progress = FMath::Clamp(PracticeMoved / PracticeMoveDistance, 0.f, 1.f);
		P.Text(FString::Printf(TEXT("움직인 거리 %d%%"), FMath::RoundToInt(Progress * 100.f)), 420.f, 465.f, 20.f, Muted(), EAlign::Center);
	}

	PaintButtons(P);
}
