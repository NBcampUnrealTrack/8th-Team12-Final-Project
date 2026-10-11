// [skydodo] 2026-10-12 작성
// 요약: 꽃집 알바 미니게임 구현 (flower.html 이식)
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/Flower/FlowerMiniGameWidget.h"

#include "FlowerPhysics.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlowerMiniGame, Log, All);

namespace FlowerGame
{
	/** HTML 캔버스 크기. 플레이 영역 좌표는 이 좌표계 기준이다 */
	constexpr float CanvasW = 840.f;
	constexpr float CanvasH = 1240.f;

	/** 가로형 화면 (햄버거 미니게임과 같은 틀). 플레이 영역은 가운데, 머리글·주문판·단계표는 좌우 패널로 */
	constexpr float ScreenW = 2200.f;
	constexpr float ScreenH = 1240.f;
	constexpr float PlayX = (ScreenW - CanvasW) * 0.5f;
	constexpr float PanelW = 600.f;
	constexpr float LeftPanelX = PlayX - 40.f - PanelW;
	constexpr float RightPanelX = PlayX + CanvasW + 40.f;

	struct FTier
	{
		const TCHAR* Name;
		const TCHAR* Asset;
		float Radius;      // 물리 충돌 반지름
		const TCHAR* Color; // 받침 테두리 · 합성 조각 색
	};

	const FTier Tiers[] =
	{
		{ TEXT("씨앗"), TEXT("seed"), 24.f, TEXT("738e69") },
		{ TEXT("새싹"), TEXT("sprout"), 32.f, TEXT("73b184") },
		{ TEXT("데이지"), TEXT("daisy"), 43.f, TEXT("e0ca80") },
		{ TEXT("튤립"), TEXT("tulip"), 55.f, TEXT("dd8fab") },
		{ TEXT("장미"), TEXT("rose"), 68.f, TEXT("c96c83") },
		{ TEXT("해바라기"), TEXT("sunflower"), 83.f, TEXT("e4b55c") },
		{ TEXT("수국"), TEXT("hydrangea"), 99.f, TEXT("9e8bbd") },
		{ TEXT("꽃다발"), TEXT("bouquet"), 116.f, TEXT("c9a589") },
	};
	constexpr int32 NumTiers = UE_ARRAY_COUNT(Tiers);
	constexpr int32 MaxTier = NumTiers - 1;

	// ---- 규칙 수치 ----
	// 작업대 · 커서 · 낙하 간격은 HTML 그대로. 물리(모양 · 마찰 · 합성)는 FlowerPhysics.h.
	constexpr float Left = 45.f;
	constexpr float Right = 795.f;
	constexpr float Floor = 1140.f;
	/** 위험선. HTML은 340이었는데 꽃을 1.4배 키우고 모양대로 쌓이게 바꾸면서 내렸다 */
	constexpr float DangerLine = 520.f;
	constexpr float DropY = 265.f;
	constexpr float CursorMargin = 24.f;
	constexpr float CursorSpeed = 600.f;
	constexpr float DropCooldown = 0.4f;
	/**
	 * 본 근무에 떨어지는 꽃: 씨앗 ~ 튤립. 장미부터는 직접 키워야 한다.
	 * (HTML은 데이지까지 - 종류가 적으면 아무렇게나 놓아도 저절로 합쳐졌다)
	 */
	constexpr int32 DropMaxTier = 3;
	constexpr float PhysicsStep = 1.f / 120.f;
	/** 물리 손잡이 (FlowerPhysics::FParams 덮어쓰기). 마찰 · 회전 감쇠가 크면 비스듬히 걸쳐 쌓인다 */
	constexpr float Friction = 0.8f;
	constexpr float AngularDamping = 6.f;
	constexpr float LinearDamping = 1.f;
	constexpr float DangerGrace = 1.3f;
	constexpr float OverflowSeconds = 2.f;
	constexpr float ChainWindow = 1.2f;
	constexpr float WarningSeconds = 10.f;
	/** 같은 효과음 중복 재생 간격 */
	constexpr double SfxRepeatGuard = 0.08;
	/** 첫 주문 (연습 판도 이 꽃) */
	constexpr int32 OrderBaseTier = 3;

	/**
	 * 주문 묶음. 묶음의 꽃이 **작업대에 동시에 모두 있으면** 한꺼번에 포장한다 (떨어뜨린 꽃도 인정).
	 * 같은 꽃 둘이 닿으면 합쳐져 버리므로 "튤립 2송이"는 서로 떨어뜨려 지켜야 한다 - 작업대를 계속 관리하게 만든다.
	 * 끝까지 가면 RepeatFrom부터 다시 돈다. 빈칸은 INDEX_NONE.
	 */
	constexpr int32 MaxOrderFlowers = 3;
	struct FOrderStep { int32 Tiers[MaxOrderFlowers]; };
	const FOrderStep OrderSteps[] =
	{
		{ { 3, 3, INDEX_NONE } },  // 튤립 ×2
		{ { 4, 3, INDEX_NONE } },  // 장미 + 튤립
		{ { 5, 3, INDEX_NONE } },  // 해바라기 + 튤립
		{ { 4, 4, INDEX_NONE } },  // 장미 ×2
		{ { 5, 4, 3 } },           // 해바라기 + 장미 + 튤립
		{ { 3, 3, 3 } },           // 튤립 ×3   ← 여기부터 반복
		{ { 6, 4, INDEX_NONE } },  // 수국 + 장미
		{ { 5, 5, INDEX_NONE } },  // 해바라기 ×2
		{ { 6, 5, 3 } },           // 수국 + 해바라기 + 튤립
		{ { 7, 4, INDEX_NONE } },  // 꽃다발 + 장미
	};
	constexpr int32 OrderRepeatFrom = 5;

	/**
	 * 연습 판 단계
	 *   0 꽃 떨어뜨려 보기 → 1 씨앗끼리 합치기 → 2 새싹끼리 합쳐 데이지 → 3 튤립 주문 채우기
	 *   → 4 위험선 설명 (Space · Enter · 버튼으로 넘어간다) → 5 준비 끝 (잠깐 뒤 실제 근무 시작)
	 */
	constexpr int32 PracticeDropStep = 0;
	constexpr int32 PracticeMergeStep = 1;
	constexpr int32 PracticeDaisyStep = 2;
	constexpr int32 PracticeOrderStep = 3;
	constexpr int32 DangerExplainStep = 4;
	constexpr int32 PracticeSteps = 6;
	constexpr float PracticeFinishDelay = 1.6f;

	/** 오버레이 버튼 (캔버스 좌표, 가운데 기준) */
	constexpr float ButtonW = 300.f;
	constexpr float ButtonH = 84.f;

	// ---- 효과 ----
	constexpr int32 MaxParticles = 700;
	/** 이 속도보다 세게 부딪혀야 착지 효과가 난다 (살짝 굴러 닿은 건 무시) */
	constexpr float LandImpactMin = 160.f;
	constexpr float SquashSeconds = 0.22f;
	constexpr float WobbleSeconds = 0.8f;
	constexpr float TrailInterval = 0.04f;
	/** 처음 착지할 때 되튀는 비율. 작은 꽃일수록 통통, 큰 꽃은 묵직하게 */
	const float LandHop[] = { 0.38f, 0.3f, 0.24f, 0.2f, 0.16f, 0.13f, 0.1f, 0.08f };
	/** 착지할 때 주변 꽃을 밀쳐내는 세기 (부딪힌 속도 대비) */
	constexpr float LandPush = 0.28f;
	/** 꽃끼리 이 속도보다 세게 부딪히면 이만큼 튕긴다 */
	constexpr float BounceMinSpeed = 220.f;
	constexpr float BallRestitution = 0.2f;

	/** 단계별 조각 모양 · 색 */
	struct FTierFx
	{
		uint8 Shape;        // 0 네모 · 1 꽃잎 · 2 동그라미
		const TCHAR* Color;
		const TCHAR* Accent;
		float Size;
		float Gravity;      // 음수면 위로 떠오른다 (수국 물방울)
		float Sway;
		float Life;
	};

	const FTierFx TierFx[] =
	{
		{ 0, TEXT("8a6a4a"), TEXT("b08d63"), 6.f, 600.f, 0.f, 0.5f },   // 씨앗: 흙 알갱이
		{ 1, TEXT("7cc46a"), TEXT("a6d98a"), 13.f, 90.f, 70.f, 1.0f },  // 새싹: 잎이 살랑
		{ 1, TEXT("ffffff"), TEXT("f3cf4a"), 11.f, 220.f, 40.f, 0.8f }, // 데이지: 흰 꽃잎
		{ 1, TEXT("f29ab5"), TEXT("ffc4d6"), 13.f, 200.f, 50.f, 0.9f }, // 튤립: 분홍 꽃잎
		{ 1, TEXT("d9506e"), TEXT("ffffff"), 13.f, 200.f, 50.f, 0.9f }, // 장미: 붉은 꽃잎 + 반짝
		{ 2, TEXT("f6c445"), TEXT("ffe08a"), 9.f, 260.f, 0.f, 0.7f },   // 해바라기: 햇살 불꽃
		{ 2, TEXT("b8a3e3"), TEXT("d9ccf5"), 14.f, -120.f, 30.f, 1.1f },// 수국: 몽글몽글 떠오르는 물방울
		{ 0, TEXT("f29ab5"), TEXT("8fd3c1"), 10.f, 260.f, 60.f, 1.2f }, // 꽃다발: 색종이
	};

	/** 꽃다발 색종이 색 */
	const TCHAR* const ConfettiColors[] = { TEXT("f29ab5"), TEXT("8fd3c1"), TEXT("f6c445"), TEXT("b8a3e3"), TEXT("ffffff") };

	/** 피어날 때 한마디 (해바라기부터) */
	const TCHAR* const BornCheers[] = { TEXT("씨앗!"), TEXT("새싹 쏙!"), TEXT("데이지 활짝!"), TEXT("튤립 피었다!"), TEXT("장미 활짝!"), TEXT("해바라기 활짝! 쿵!"), TEXT("수국 몽글몽글! 쿵!"), TEXT("꽃다발 완성! 쿵!!") };
	/** 조각 크기 배율 (화면에서 잘 보이게) */
	constexpr float ParticleScale = 1.7f;

	FLinearColor Hex(const TCHAR* InHex)
	{
		return FLinearColor(FColor::FromHex(InHex));
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = FMath::Clamp(Alpha, 0.f, 1.f);
		return Color;
	}

	FLinearColor Ink() { return Hex(TEXT("4f5b46")); }
	FLinearColor Leaf() { return Hex(TEXT("597251")); }
	FLinearColor Rose() { return Hex(TEXT("b7687d")); }
	FLinearColor Muted() { return Hex(TEXT("869574")); }
	FLinearColor Tag() { return Hex(TEXT("839775")); }

	/** 보상 스탯 이름 → 화면 표시 이름 */
	FString StatLabel(FName Stat)
	{
		if (Stat == TEXT("Health")) { return TEXT("체력"); }
		if (Stat == TEXT("HandWork")) { return TEXT("손재주"); }
		if (Stat == TEXT("Mental")) { return TEXT("멘탈"); }
		return Stat.ToString();
	}
}

/** 캔버스 좌표로 그리는 도우미 (햄버거의 FBurgerPainter와 같다). 그린 순서가 곧 앞뒤가 된다 */
struct FFlowerPainter
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

	/** (X, Y)를 가운데로 Size×Size */
	void ArtCentered(const FSlateBrush* InBrush, float X, float Y, float Size, const FLinearColor& Tint = FLinearColor::White)
	{
		Brush(InBrush, X - Size * 0.5f, Y - Size * 0.5f, Size, Size, Tint);
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
			Outline = FFontOutlineSettings(OutlinePx, FlowerGame::WithAlpha(FlowerGame::Ink(), Color.A * 0.55f));
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

using EAlign = FFlowerPainter::EAlign;

// ---------------------------------------------------------------------------
// 생성 · 준비
// ---------------------------------------------------------------------------

UFlowerMiniGameWidget::UFlowerMiniGameWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	auto AddArt = [this](const TCHAR* Key, const TCHAR* Folder, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Flower/Textures/%s/%s.%s"), Folder, Asset, Asset);
		Art.Add(Key, TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Path)));
	};

	AddArt(TEXT("shop"), TEXT("Background"), TEXT("T_Flower_Shop"));

	AddArt(TEXT("seed"), TEXT("Items"), TEXT("T_Flower_Item_Seed"));
	AddArt(TEXT("sprout"), TEXT("Items"), TEXT("T_Flower_Item_Sprout"));
	AddArt(TEXT("daisy"), TEXT("Items"), TEXT("T_Flower_Item_Daisy"));
	AddArt(TEXT("tulip"), TEXT("Items"), TEXT("T_Flower_Item_Tulip"));
	AddArt(TEXT("rose"), TEXT("Items"), TEXT("T_Flower_Item_Rose"));
	AddArt(TEXT("sunflower"), TEXT("Items"), TEXT("T_Flower_Item_Sunflower"));
	AddArt(TEXT("hydrangea"), TEXT("Items"), TEXT("T_Flower_Item_Hydrangea"));
	AddArt(TEXT("bouquet"), TEXT("Items"), TEXT("T_Flower_Item_Bouquet"));

	AddArt(TEXT("order-panel"), TEXT("UI"), TEXT("T_Flower_UI_OrderPanel"));
	AddArt(TEXT("status-panel"), TEXT("UI"), TEXT("T_Flower_UI_StatusPanel"));
	AddArt(TEXT("button-normal"), TEXT("UI"), TEXT("T_Flower_UI_ButtonNormal"));
	AddArt(TEXT("button-hover"), TEXT("UI"), TEXT("T_Flower_UI_ButtonHover"));
	AddArt(TEXT("button-pressed"), TEXT("UI"), TEXT("T_Flower_UI_ButtonPressed"));

	// 효과음 (원본 파일 이름 → 애셋 이름)
	//   놓기 → Drop, 합체 → Merge, 주문완료 → Order, 위기상황 → Warning(10초 남음),
	//   근무 종료 → Finish, 실패 → Overflow, 임시 click.wav → Click
	auto AddSfx = [this](const TCHAR* Key, const TCHAR* Asset)
	{
		const FString Path = FString::Printf(TEXT("/Game/MiniGame/Flower/Audio/SFX/%s.%s"), Asset, Asset);
		Sfx.Add(Key, TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path)));
	};
	AddSfx(TEXT("drop"), TEXT("S_Flower_SFX_Drop"));
	AddSfx(TEXT("merge"), TEXT("S_Flower_SFX_Merge"));
	AddSfx(TEXT("order"), TEXT("S_Flower_SFX_Order"));
	AddSfx(TEXT("warning"), TEXT("S_Flower_SFX_Warning"));
	AddSfx(TEXT("finish"), TEXT("S_Flower_SFX_Finish"));
	AddSfx(TEXT("overflow"), TEXT("S_Flower_SFX_Overflow"));
	AddSfx(TEXT("click"), TEXT("S_Flower_SFX_Click"));
}

void UFlowerMiniGameWidget::NativeConstruct()
{
	Super::NativeConstruct();
	using namespace FlowerGame;

	World = MakeShared<FlowerPhysics::FWorld>();
	World->P.Left = Left;
	World->P.Right = Right;
	World->P.Floor = Floor;
	World->P.Friction = Friction;
	World->P.AngularDamping = AngularDamping;
	World->P.LinearDamping = LinearDamping;

	LoadedTextures.Reset();
	ArtBrushes.Reset();
	for (const TPair<FName, TSoftObjectPtr<UTexture2D>>& Pair : Art)
	{
		UTexture2D* Texture = Pair.Value.LoadSynchronous();
		if (!Texture)
		{
			UE_LOG(LogFlowerMiniGame, Warning, TEXT("그림을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
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
			UE_LOG(LogFlowerMiniGame, Warning, TEXT("효과음을 찾지 못했다: %s (%s)"), *Pair.Key.ToString(), *Pair.Value.ToString());
		}
	}

	const FLinearColor Edge = Hex(TEXT("b8c6a5"));
	SolidBrush = FSlateColorBrush(FLinearColor::White);
	// 크기에 상관없이 원이 되도록 모서리 반지름을 높이의 절반으로
	CircleBrush = FSlateRoundedBoxBrush(FLinearColor::White, 1.f);
	CircleBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	RingBrush = FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.01f), 1.f, Rose(), 5.f);
	RingBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	WorkAreaBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("faf5e8")), 0.93f), 18.f, Hex(TEXT("a2b28e")), 3.f);
	SlotDoneBrush = FSlateRoundedBoxBrush(Hex(TEXT("e3ecd8")), 12.f, Hex(TEXT("9eb58d")), 2.f);
	SlotWaitBrush = FSlateRoundedBoxBrush(Hex(TEXT("f6f1e2")), 12.f, Hex(TEXT("cfd3bb")), 2.f);
	BarBackBrush = FSlateRoundedBoxBrush(Hex(TEXT("dfe0cc")), 8.f);
	BarFillBrush = FSlateRoundedBoxBrush(Leaf(), 8.f);
	DangerFillBrush = FSlateRoundedBoxBrush(Rose(), 8.f);
	SecondaryBrush = FSlateRoundedBoxBrush(FLinearColor::White, 12.f, Leaf(), 2.f);
	SecondaryHoverBrush = FSlateRoundedBoxBrush(Hex(TEXT("e9f0df")), 12.f, Leaf(), 2.f);
	HighlightBrush = FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.01f), 1.f, Rose(), 5.f);
	HighlightBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
	PanelBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fffaf0")), 0.93f), 22.f, Edge, 3.f);
	OverlayBrush = FSlateRoundedBoxBrush(WithAlpha(Hex(TEXT("fffaf0")), 0.94f), 22.f, Edge, 3.f);
	ReceiptBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f, Hex(TEXT("a9b893")), 2.f);
	// 채우기 알파가 0이면 Slate가 요소째로 버려서 테두리도 안 그려진다. 거의 투명하게만 둔다.
	FrameBrush = FSlateRoundedBoxBrush(FLinearColor(0.f, 0.f, 0.f, 0.01f), 22.f, Edge, 3.f);

	ResetRound();
	PlayCount = GetPlayCount();
	TutorialPage = 0;
	bSkipped = false;
	Mode = EMode::Title;
}

const FSlateBrush* UFlowerMiniGameWidget::FindBrush(FName Name) const
{
	return ArtBrushes.Find(Name);
}

const FSlateBrush* UFlowerMiniGameWidget::FlowerBrush(int32 Tier) const
{
	return FindBrush(FlowerGame::Tiers[FMath::Clamp(Tier, 0, FlowerGame::MaxTier)].Asset);
}

// ---------------------------------------------------------------------------
// 규칙
// ---------------------------------------------------------------------------

void UFlowerMiniGameWidget::ResetRound()
{
	T = 0.f;
	Cursor = 420.f;
	if (World)
	{
		World->Reset();
	}
	Pops.Reset();
	Particles.Reset();
	Rings.Reset();
	Shake = 0.f;
	ShakeOffset = FVector2f::ZeroVector;
	NextTier = FMath::RandRange(0, 2);
	QueueTier = FMath::RandRange(0, 2);
	Cooldown = 0.f;
	Orders = 0;
	MergeScore = 0;
	BestTier = 0;
	Over = 0.f;
	Serial = 0;
	NextOrder();
	Chain = 0;
	ChainTimer = 0.f;
	BestChain = 0;
	Bouquets = 0;
	Drops = 0;
	Merges = 0;
	bWarningAnnounced = false;
	bOverflowed = false;
}

void UFlowerMiniGameWidget::PlaySfx(FName Key)
{
	const TObjectPtr<USoundBase>* Sound = LoadedSfx.Find(Key);
	if (!Sound)
	{
		return;
	}

	// 연쇄 합성처럼 한 프레임에 여러 번 불려도 한 번만 들리게
	const double Now = FPlatformTime::Seconds();
	double& Last = LastSfxTime.FindOrAdd(Key, -1.0);
	if (Now - Last < FlowerGame::SfxRepeatGuard)
	{
		return;
	}
	Last = Now;
	UGameplayStatics::PlaySound2D(this, *Sound, SfxVolume);
}

void UFlowerMiniGameWidget::StartRound()
{
	ResetRound();
	bLeftHeld = false;
	bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;
	bSkipped = false;
	Mode = EMode::Play;
}

void UFlowerMiniGameWidget::TogglePause()
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

void UFlowerMiniGameWidget::EndRound(bool bOverflow)
{
	Mode = EMode::Result;
	bOverflowed = bOverflow;
	bLeftHeld = false;
	bRightHeld = false;
	HoveredButton = INDEX_NONE;
	PressedButton = INDEX_NONE;

	// 미완성 주문은 치지 않는다 (HTML 그대로)
	FMiniGameResult Result;
	Result.Score = Orders;
	FinalReward = FinishGame(Result);
	PlaySfx(bOverflow ? TEXT("overflow") : TEXT("finish"));
}

void UFlowerMiniGameWidget::SkipShift()
{
	// 점수 0으로 정산 → 0점으로 끝낸 것과 같은 최소 보상 (돈 고정 + 체력 +1). 플레이 횟수에는 안 센다.
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

void UFlowerMiniGameWidget::DoButton(EButtonAction Action)
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

void UFlowerMiniGameWidget::ChangeCardPage(int32 Delta)
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

void UFlowerMiniGameWidget::PressPrimaryButton()
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

// ---------------------------------------------------------------------------
// 연습 판
//   시간 제한 · 넘침 종료 없이, 단계마다 놓을 꽃을 정해 준다.
//   0 아무 데나 꽃 놓기 → 1 씨앗 위에 씨앗 (합성) → 2 새싹 위에 새싹 (데이지)
//   → 3 데이지끼리 합쳐 튤립 주문 채우기 (자동 포장) → 4 위험선 설명 → 5 준비 끝
// ---------------------------------------------------------------------------

void UFlowerMiniGameWidget::StartPractice()
{
	ResetRound();
	bLeftHeld = false;
	bRightHeld = false;
	bSkipped = false;
	Mode = EMode::Practice;
	SetPracticeStep(0);
}

void UFlowerMiniGameWidget::SetPracticeStep(int32 Step)
{
	using namespace FlowerGame;

	PracticeStep = Step;
	PracticeStepTime = 0.f;
	PracticeMergeBase = Merges;
	PracticeDropBase = Drops;

	// 주문은 튤립 단계에서만 걸어 둔다 (그 전에 우연히 튤립이 생겨도 포장되지 않게)
	Want.Reset();
	WantReady.Reset();
	ReadyIds.Reset();
	if (Step == PracticeOrderStep)
	{
		Want = { OrderBaseTier };
	}

	// 손에 든 꽃도 이번 단계 꽃으로 바로 바꾼다
	NextTier = RollNext();
	QueueTier = RollNext();
}

void UFlowerMiniGameWidget::UpdatePractice(float Dt)
{
	using namespace FlowerGame;

	PracticeStepTime += Dt;

	switch (PracticeStep)
	{
	case PracticeDropStep:  if (Drops > PracticeDropBase)   { SetPracticeStep(PracticeMergeStep); } break;
	case PracticeMergeStep: if (Merges > PracticeMergeBase) { SetPracticeStep(PracticeDaisyStep); } break;
	case PracticeDaisyStep: if (BestTier >= 2)              { SetPracticeStep(PracticeOrderStep); } break;
	case PracticeOrderStep: if (Orders > 0)                 { SetPracticeStep(DangerExplainStep); } break;
	case DangerExplainStep: break; // 키 입력이나 버튼으로만 넘어간다
	default:
		if (PracticeStepTime >= PracticeFinishDelay)
		{
			StartRound();
		}
		break;
	}
}

int32 UFlowerMiniGameWidget::RollNext() const
{
	using namespace FlowerGame;

	if (Mode == EMode::Practice)
	{
		switch (PracticeStep)
		{
		case PracticeDaisyStep: return 1; // 새싹
		case PracticeOrderStep: return 2; // 데이지
		default: return 0;               // 씨앗
		}
	}
	return FMath::RandRange(0, DropMaxTier);
}

void UFlowerMiniGameWidget::AddPop(float InX, float InY, const FString& Text, const FLinearColor& Color)
{
	Pops.Add({ FVector2f(InX, InY), Text, Color, 1.f });
}

void UFlowerMiniGameWidget::Burst(float InX, float InY, const FLinearColor& Color)
{
	for (int32 i = 0; i < 16; ++i)
	{
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float Speed = 60.f + FMath::FRand() * 220.f;
		Particles.Add({ FVector2f(InX, InY), FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Speed, Color, 0.7f });
	}
}

void UFlowerMiniGameWidget::Drop()
{
	using namespace FlowerGame;

	if (Cooldown > 0.f || !World || World->Count >= FlowerPhysics::MaxBodies)
	{
		return;
	}
	// 연습 판 설명 · 준비 단계에서는 놓지 않는다
	if (Mode == EMode::Practice && PracticeStep > PracticeOrderStep)
	{
		return;
	}

	const FlowerPhysics::FTierShape& Shape = World->Shapes[NextTier];
	const float X = FMath::Clamp(Cursor, Left + Shape.HalfWidth, Right - Shape.HalfWidth);
	const int32 Id = World->Add(NextTier, X, DropY, FMath::FRandRange(-0.3f, 0.3f), 0.f);
	if (FlowerPhysics::FBody* Body = World->FindById(Id))
	{
		// 떨어지면서 빙글 돈다 (데이지는 바람개비처럼 더 빨리)
		Body->W = FMath::FRandRange(1.5f, 3.f) * (FMath::RandBool() ? 1.f : -1.f) * (NextTier == 2 ? 2.f : 1.f);
		Body->Pop = 0.15f;
	}
	NextTier = QueueTier;
	QueueTier = RollNext();
	Cooldown = DropCooldown;
	++Drops;
	PlaySfx(TEXT("drop"));
}

void UFlowerMiniGameWidget::NextOrder()
{
	using namespace FlowerGame;

	Want.Reset();
	WantReady.Reset();
	ReadyIds.Reset();
	if (Mode == EMode::Practice)
	{
		return; // 연습 판 주문은 튤립 한 번뿐 (SetPracticeStep이 건다)
	}
	const int32 NumSteps = UE_ARRAY_COUNT(OrderSteps);
	const int32 Index = Orders < NumSteps ? Orders : OrderRepeatFrom + (Orders - NumSteps) % (NumSteps - OrderRepeatFrom);
	for (const int32 Tier : OrderSteps[Index].Tiers)
	{
		if (Tier != INDEX_NONE)
		{
			Want.Add(Tier);
		}
	}
}

void UFlowerMiniGameWidget::Fulfill()
{
	using namespace FlowerGame;

	// 주문 칸마다 작업대 꽃을 하나씩 짝짓는다 (한 송이를 두 칸에 쓰지 않는다). 떨어뜨린 꽃도 인정.
	WantReady.Init(false, Want.Num());
	ReadyIds.Reset();
	TArray<int32, TInlineAllocator<MaxOrderFlowers>> Picks;
	for (int32 w = 0; w < Want.Num(); ++w)
	{
		for (int32 i = 0; i < World->Count; ++i)
		{
			const FlowerPhysics::FBody& Body = World->Bodies[i];
			if (Body.Tier == Want[w] && !ReadyIds.Contains(Body.Id))
			{
				WantReady[w] = true;
				ReadyIds.Add(Body.Id);
				Picks.Add(i);
				break;
			}
		}
	}
	if (Want.Num() == 0 || Picks.Num() < Want.Num())
	{
		return;
	}

	// 묶음이 작업대에 동시에 다 있다 → 한꺼번에 포장 (뒤 번호부터 지워야 앞 번호가 안 밀린다)
	Picks.Sort([](const int32& A, const int32& B) { return A > B; });
	for (const int32 Index : Picks)
	{
		const FlowerPhysics::FBody Body = World->Bodies[Index];
		const float R = World->Shapes[Body.Tier].EquivR;
		Spray(Body.Tier, Body.X, Body.Y, 24, 320.f, 200.f);
		AddRing(Body.X, Body.Y, R * 0.5f, 700.f, 0.25f, 0.f, Hex(TEXT("fff3c4")), true);
		AddPop(Body.X, Body.Y, TEXT("포장!"), Hex(TEXT("ffd18d")));
		World->RemoveAt(Index);
	}
	++Orders;
	AddPop(420.f, 620.f, FString::Printf(TEXT("주문 완료! %d건째"), Orders), Hex(TEXT("ffd18d")));
	AddShake(4.f);
	PlaySfx(TEXT("order"));
	NextOrder();
}

void UFlowerMiniGameWidget::ProcessEvents()
{
	using namespace FlowerGame;

	for (int32 e = 0; e < World->NumEvents; ++e)
	{
		const FlowerPhysics::FEvent Event = World->Events[e];
		if (Event.Type == FlowerPhysics::EEventType::Landed)
		{
			if (FlowerPhysics::FBody* Body = World->FindById(Event.Id))
			{
				OnLanded(*Body, Event.Impact);
			}
			continue;
		}

		// 합성
		const int32 NewTier = Event.Tier;
		BestTier = FMath::Max(BestTier, FMath::Min(MaxTier, NewTier));
		MergeScore += 30 * NewTier * NewTier;
		Chain = ChainTimer > 0.f ? Chain + 1 : 1;
		ChainTimer = ChainWindow;
		BestChain = FMath::Max(BestChain, Chain);
		++Merges;

		if (NewTier > MaxTier)
		{
			// 꽃다발 둘 → 특대 꽃다발. HTML은 700원 보너스였는데 돈이 고정이라 주문 1건으로 쳐 준다.
			++Bouquets;
			++Orders;
			Spray(MaxTier, Event.X, Event.Y, 90, 700.f, 320.f);
			AddRing(Event.X, Event.Y, 60.f, 1400.f, 0.3f, 0.f, FLinearColor::White, true);
			AddRing(Event.X, Event.Y, 120.f, 900.f, 0.7f, 18.f, Hex(TEXT("ffd18d")));
			World->Push(Event.X, Event.Y, 420.f, 600.f, INDEX_NONE);
			AddShake(14.f);
			AddPop(Event.X, Event.Y, TEXT("특대 꽃다발! 주문 +1"), Hex(TEXT("ffd18d")));
			PlaySfx(TEXT("order"));
			continue;
		}

		if (FlowerPhysics::FBody* Born = World->FindById(Event.Id))
		{
			// 크게 부풀었다가 돌아온다
			Born->Pop = 0.35f;
			if (NewTier == 1)
			{
				Born->Wobble = WobbleSeconds; // 새싹은 돋아나며 흔들흔들
			}
			const FlowerPhysics::FBody Copy = *Born; // OnBorn이 다른 꽃을 밀어도 안전하게
			OnBorn(Copy);
		}
		if (Chain > 1)
		{
			AddPop(Event.X, Event.Y + 40.f, FString::Printf(TEXT("%d연쇄!"), Chain), Hex(TEXT("b8e2ae")));
		}
		PlaySfx(TEXT("merge"));
	}
	World->NumEvents = 0;
}

void UFlowerMiniGameWidget::UpdateBodyFx(float Dt)
{
	for (int32 i = 0; i < World->Count; ++i)
	{
		FlowerPhysics::FBody& Body = World->Bodies[i];
		Body.Pop = FMath::Max(0.f, Body.Pop - Dt);
		Body.Squash = FMath::Max(0.f, Body.Squash - Dt);
		Body.Wobble = FMath::Max(0.f, Body.Wobble - Dt);
		EmitTrail(Body, Dt);
	}
}

// ---------------------------------------------------------------------------
// 단계별 효과
//   떨어질 때: 빙글 돌며 단계별 꼬리 (씨앗 흙 · 새싹 잎 · 데이지 꽃잎 …)
//   착지할 때: 찌그러짐 + 단계별 바운스 + 주변 밀쳐내기 + 조각 (큰 꽃은 화면이 흔들린다)
//   피어날 때: 번쩍임 + 크게 부풀기 + 고리 + 조각 + 단계 이름 외침 + 주변 밀어내기. 해바라기부터 쿵!
// ---------------------------------------------------------------------------

void UFlowerMiniGameWidget::OnLanded(FlowerPhysics::FBody& Body, float Impact)
{
	using namespace FlowerGame;

	const int32 Tier = Body.Tier;
	const float R = World->Shapes[Tier].EquivR;
	const float Strength = FMath::Clamp(Impact / 1000.f, 0.3f, 1.f);
	Body.Squash = SquashSeconds * Strength;

	// 통 튀고, 부딪힌 자리 주변 꽃을 밀쳐낸다
	Body.VY = -Impact * LandHop[FMath::Clamp(Tier, 0, MaxTier)];
	World->Push(Body.X, Body.Y + R * 0.5f, R * 1.8f, Impact * LandPush * (1.f + Tier * 0.15f), Body.Id);

	const float BottomY = Body.Y + R * 0.8f;
	switch (Tier)
	{
	case 0:
		// 씨앗: 제일 높이 톡 튀며 흙이 튄다
		Spray(0, Body.X, BottomY, 18, 220.f, 320.f * Strength);
		AddRing(Body.X, BottomY, R * 0.4f, 260.f, 0.3f, 8.f, Hex(TierFx[0].Accent));
		break;
	case 1:
		// 새싹: 잎이 살랑 떨어지고 흔들흔들
		Body.Wobble = WobbleSeconds;
		Spray(1, Body.X, Body.Y - R * 0.5f, 12, 160.f, 180.f);
		break;
	case 2:
		// 데이지: 꽃잎이 동그랗게 퍼진다
		Spray(2, Body.X, Body.Y, 20, 360.f * Strength + 100.f, 80.f);
		AddRing(Body.X, Body.Y, R * 0.8f, 340.f, 0.4f, 11.f, Hex(TierFx[2].Accent));
		break;
	default:
		// 튤립 · 장미가 떨어지면 묵직하게 쿵
		Spray(Tier, Body.X, BottomY, 14 + Tier * 3, 300.f, 220.f);
		AddRing(Body.X, BottomY, R * 0.6f, 420.f, 0.45f, 13.f, Hex(TierFx[Tier].Color));
		AddShake(Tier * 2.2f * Strength);
		break;
	}
}

void UFlowerMiniGameWidget::OnBorn(const FlowerPhysics::FBody& Body)
{
	using namespace FlowerGame;

	const int32 Tier = Body.Tier;
	const FTierFx& Fx = TierFx[Tier];
	const float R = World->Shapes[Tier].EquivR;

	// 번쩍 + 단계 색 고리 + 조각
	AddRing(Body.X, Body.Y, R * 0.4f, 900.f + Tier * 120.f, 0.22f, 0.f, FLinearColor::White, true);
	AddRing(Body.X, Body.Y, R * 0.6f, 320.f + Tier * 60.f, 0.5f, 10.f + Tier * 1.5f, Hex(Fx.Color));
	Spray(Tier, Body.X, Body.Y, 18 + Tier * 6, 260.f + Tier * 40.f, Tier >= 3 ? 260.f : 60.f);

	if (Tier < 5)
	{
		// 피어나며 주변 꽃을 밀어낸다 (단계가 오를수록 세게)
		World->Push(Body.X, Body.Y, R * 2.f, 140.f + Tier * 40.f, Body.Id);
	}
	else
	{
		// 해바라기부터는 쿵! 크게 밀려난다 (빈자리가 생겨 연쇄가 이어지기도 한다)
		World->Push(Body.X, Body.Y, R * 2.6f, 300.f + (Tier - 5) * 140.f, Body.Id);
		AddRing(Body.X, Body.Y, R, 640.f, 0.6f, 16.f, Hex(Fx.Accent));
	}
	if (Tier >= 3)
	{
		AddShake(Tier * 2.5f - 5.f);
	}
	AddPop(Body.X, Body.Y - R - 30.f, BornCheers[Tier], Hex(Fx.Color));
}

void UFlowerMiniGameWidget::EmitTrail(FlowerPhysics::FBody& Body, float Dt)
{
	using namespace FlowerGame;

	if (Body.bLanded || Body.VY < 150.f)
	{
		return;
	}
	Body.TrailTimer -= Dt;
	if (Body.TrailTimer > 0.f)
	{
		return;
	}
	Body.TrailTimer = TrailInterval;
	const float R = World->Shapes[Body.Tier].EquivR;
	const float Ox = FMath::FRandRange(-0.6f, 0.6f) * R;
	Spray(Body.Tier, Body.X + Ox, Body.Y - R * 0.7f, 2, 50.f, -Body.VY * 0.15f);
}

void UFlowerMiniGameWidget::Spray(int32 Tier, float InX, float InY, int32 Count, float Speed, float Upward)
{
	using namespace FlowerGame;

	const FTierFx& Fx = TierFx[FMath::Clamp(Tier, 0, MaxTier)];
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float V = Speed * FMath::FRandRange(0.4f, 1.f);
		FParticle& Part = Particles.AddDefaulted_GetRef();
		Part.Pos = FVector2f(InX, InY);
		Part.Vel = FVector2f(FMath::Cos(Angle) * V, FMath::Sin(Angle) * V - Upward * FMath::FRandRange(0.6f, 1.f));
		Part.Shape = static_cast<EParticleShape>(Fx.Shape);
		Part.Size = Fx.Size * ParticleScale * FMath::FRandRange(0.7f, 1.2f);
		Part.Gravity = Fx.Gravity;
		Part.Sway = Fx.Sway;
		Part.Angle = FMath::FRand() * UE_TWO_PI;
		Part.Spin = FMath::FRandRange(-8.f, 8.f);
		Part.MaxLife = Part.Life = Fx.Life * FMath::FRandRange(0.8f, 1.3f);
		if (Tier >= MaxTier)
		{
			Part.Color = Hex(ConfettiColors[FMath::RandRange(0, UE_ARRAY_COUNT(ConfettiColors) - 1)]);
		}
		else
		{
			Part.Color = Hex(FMath::FRand() < 0.7f ? Fx.Color : Fx.Accent);
		}
	}

	// 오래된 조각부터 버려 개수를 묶어 둔다
	if (Particles.Num() > MaxParticles)
	{
		Particles.RemoveAt(0, Particles.Num() - MaxParticles, EAllowShrinking::No);
	}
}

void UFlowerMiniGameWidget::AddRing(float InX, float InY, float StartRadius, float Speed, float Life, float DotSize, const FLinearColor& Color, bool bFlash)
{
	FRing& Ring = Rings.AddDefaulted_GetRef();
	Ring.Center = FVector2f(InX, InY);
	Ring.Radius = StartRadius;
	Ring.Speed = Speed;
	Ring.MaxLife = Ring.Life = Life;
	Ring.DotSize = DotSize;
	Ring.Color = Color;
	Ring.bFlash = bFlash;
}

void UFlowerMiniGameWidget::AddShake(float Amount)
{
	Shake = FMath::Min(14.f, FMath::Max(Shake, Amount));
}

void UFlowerMiniGameWidget::UpdateDanger(float Dt)
{
	using namespace FlowerGame;

	// 방금 놓은 꽃은 1.3초 동안 위험선 위에 있어도 봐준다
	bool bBad = false;
	for (int32 i = 0; i < World->Count && !bBad; ++i)
	{
		const FlowerPhysics::FBody& Body = World->Bodies[i];
		bBad = Body.Age > DangerGrace && World->TopY(Body) < DangerLine;
	}
	Over = bBad ? Over + Dt : FMath::Max(0.f, Over - Dt * 2.f);
	if (Over >= OverflowSeconds)
	{
		EndRound(true);
	}
}

void UFlowerMiniGameWidget::Update(float Dt)
{
	using namespace FlowerGame;

	const bool bPractice = Mode == EMode::Practice;
	if (bPractice)
	{
		// 연습 판은 시간 제한이 없다
		T += Dt;
	}
	else
	{
		T = FMath::Min(Duration, T + Dt);
		if (T >= Duration)
		{
			EndRound(false);
			return;
		}
		if (!bWarningAnnounced && T >= Duration - WarningSeconds)
		{
			bWarningAnnounced = true;
			PlaySfx(TEXT("warning"));
		}
	}

	Cooldown = FMath::Max(0.f, Cooldown - Dt);
	ChainTimer = FMath::Max(0.f, ChainTimer - Dt);

	const int32 Dir = (bRightHeld ? 1 : 0) - (bLeftHeld ? 1 : 0);
	Cursor = FMath::Clamp(Cursor + Dir * CursorSpeed * Dt, Left + CursorMargin, Right - CursorMargin);

	// 고정 간격 물리 (1/120초 단위로 나눠 돌린다)
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Dt / PhysicsStep));
	for (int32 i = 0; i < Steps; ++i)
	{
		World->Step(Dt / Steps);
		ProcessEvents();
		Fulfill();
	}
	UpdateBodyFx(Dt);

	if (bPractice)
	{
		UpdatePractice(Dt);
	}
	else
	{
		UpdateDanger(Dt);
		if (Mode != EMode::Play)
		{
			return; // 넘쳐서 끝났다
		}
	}

	UpdateEffects(Dt);
}

void UFlowerMiniGameWidget::UpdateEffects(float Dt)
{
	for (FPopText& Pop : Pops)
	{
		Pop.Life -= Dt;
		Pop.Pos.Y -= 55.f * Dt;
	}
	Pops.RemoveAll([](const FPopText& Pop) { return Pop.Life <= 0.f; });

	// 꽃잎 · 잎은 공기 저항으로 천천히, 좌우로 살랑이며 떨어진다
	const float AirDrag = FMath::Exp(-1.6f * Dt);
	for (FParticle& Part : Particles)
	{
		Part.Life -= Dt;
		Part.Vel.Y += Part.Gravity * Dt;
		if (Part.Shape != EParticleShape::Square)
		{
			Part.Vel *= AirDrag;
		}
		Part.Pos += Part.Vel * Dt;
		Part.Pos.X += FMath::Sin(Part.Life * 7.f + Part.Angle) * Part.Sway * Dt;
		Part.Angle += Part.Spin * Dt;
	}
	Particles.RemoveAll([](const FParticle& Part) { return Part.Life <= 0.f; });

	for (FRing& Ring : Rings)
	{
		Ring.Life -= Dt;
		Ring.Radius += Ring.Speed * Dt;
	}
	Rings.RemoveAll([](const FRing& Ring) { return Ring.Life <= 0.f; });

	// 흔들림은 빠르게 잦아든다
	Shake *= FMath::Exp(-7.f * Dt);
	if (Shake < 0.3f)
	{
		Shake = 0.f;
	}
	const double Now = FPlatformTime::Seconds();
	ShakeOffset = FVector2f(static_cast<float>(FMath::Sin(Now * 95.0)), static_cast<float>(FMath::Cos(Now * 83.0))) * Shake;
}

// ---------------------------------------------------------------------------
// 입력
// ---------------------------------------------------------------------------

void UFlowerMiniGameWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 프레임이 튀어도 꽃이 바닥을 뚫지 않게 한 번에 0.05초까지만 진행한다 (HTML 그대로)
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

FReply UFlowerMiniGameWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
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
		if (Mode == EMode::Practice && PracticeStep == FlowerGame::DangerExplainStep)
		{
			PressPrimaryButton(); // 위험선 설명 단계에서는 "알겠어요"
		}
		else if (Mode == EMode::Play || Mode == EMode::Practice)
		{
			Drop();
		}
		else
		{
			PressPrimaryButton();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UFlowerMiniGameWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
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

bool UFlowerMiniGameWidget::AimAt(const FVector2f& ScreenPos)
{
	using namespace FlowerGame;

	const float LocalX = ScreenPos.X - PlayX;
	if (LocalX < 0.f || LocalX > CanvasW || ScreenPos.Y < 0.f || ScreenPos.Y > CanvasH)
	{
		return false;
	}
	Cursor = FMath::Clamp(LocalX, Left + CursorMargin, Right - CursorMargin);
	return true;
}

FReply UFlowerMiniGameWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2f Pos = ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition());
		PressedButton = FindButtonAt(Pos);

		// 버튼이 아닌 플레이 영역을 누르면 그 위치에 꽃을 떨어뜨린다 (HTML의 pointerdown)
		const bool bPlaying = Mode == EMode::Play || (Mode == EMode::Practice && PracticeStep != FlowerGame::DangerExplainStep);
		if (PressedButton == INDEX_NONE && bPlaying && AimAt(Pos))
		{
			Drop();
		}
	}
	// 클릭해도 키보드 포커스를 잃지 않게 붙잡아 둔다
	return FReply::Handled().SetUserFocus(TakeWidget(), EFocusCause::Mouse);
}

FReply UFlowerMiniGameWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
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

FReply UFlowerMiniGameWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2f Pos = ToCanvas(InGeometry, InMouseEvent.GetScreenSpacePosition());
	HoveredButton = FindButtonAt(Pos);
	if (Mode == EMode::Play || Mode == EMode::Practice)
	{
		AimAt(Pos);
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void UFlowerMiniGameWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
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

void UFlowerMiniGameWidget::GetCanvasTransform(const FVector2f& LocalSize, float& OutScale, FVector2f& OutOffset)
{
	using namespace FlowerGame;
	OutScale = FMath::Max(0.01f, FMath::Min(LocalSize.Y / ScreenH, LocalSize.X / ScreenW));
	OutOffset = (LocalSize - FVector2f(ScreenW, ScreenH) * OutScale) * 0.5f;
}

FVector2f UFlowerMiniGameWidget::ToCanvas(const FGeometry& Geometry, const FVector2D& ScreenPos)
{
	float Scale;
	FVector2f Offset;
	const FVector2f LocalSize = Geometry.GetLocalSize();
	GetCanvasTransform(LocalSize, Scale, Offset);
	const FVector2f Local = Geometry.AbsoluteToLocal(ScreenPos);
	return (Local - Offset) / Scale;
}

TArray<UFlowerMiniGameWidget::FOverlayButton> UFlowerMiniGameWidget::GetOverlayButtons() const
{
	using namespace FlowerGame;

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
			Add(CX, 880.f, ButtonW, TEXT("꽃집 출근하기"), EButtonAction::Start, true);
		}
		else
		{
			// 한 번 해 본 뒤에는 건너뛰기 · 연습 · 설명을 연다
			Add(CX, 800.f, ButtonW, TEXT("꽃집 출근하기"), EButtonAction::Start, true);
			Add(CX, 900.f, 300.f, TEXT("대충 일하고 퇴근"), EButtonAction::SkipShift, false);
			Add(CX - 125.f, 980.f, 230.f, TEXT("연습하기"), EButtonAction::StartPractice, false);
			Add(CX + 125.f, 980.f, 230.f, TEXT("설명 보기"), EButtonAction::ShowCards, false);
		}
		break;
	case EMode::Practice:
		if (PracticeStep == DangerExplainStep)
		{
			Add(CX, 820.f, ButtonW, TEXT("알겠어요"), EButtonAction::NextPage, true);
		}
		// 연습 중에는 작업대 아래쪽에 작게
		Add(CX, 1190.f, 240.f, TEXT("연습 건너뛰기"), EButtonAction::SkipPractice, false);
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

int32 UFlowerMiniGameWidget::FindButtonAt(const FVector2f& ScreenPos) const
{
	using namespace FlowerGame;

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

int32 UFlowerMiniGameWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace FlowerGame;

	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	float Scale;
	FVector2f Offset;
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	GetCanvasTransform(LocalSize, Scale, Offset);

	// 가로형 화면 전체 (좌우 패널) → 가운데 플레이 영역 순서로 그린다
	FFlowerPainter Screen{ OutDrawElements, AllottedGeometry.MakeChild(FVector2f(ScreenW, ScreenH), FSlateLayoutTransform(Scale, Offset)), BaseLayer, &SolidBrush };
	PaintSidePanels(Screen);

	FFlowerPainter P{ OutDrawElements, Screen.Canvas.MakeChild(FVector2f(CanvasW, CanvasH), FSlateLayoutTransform(FVector2f(PlayX, 0.f) + ShakeOffset)), Screen.Layer, &SolidBrush };
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

void UFlowerMiniGameWidget::PaintFlower(FFlowerPainter& P, float InX, float InY, float Angle, int32 Tier, float Scale, float Alpha, float Squash) const
{
	using namespace FlowerGame;

	// 무게중심 → 그림 가운데 (돌린 만큼 같이 돈다)
	const FlowerPhysics::FTierShape& Shape = World->Shapes[FMath::Clamp(Tier, 0, MaxTier)];
	const float Cs = FMath::Cos(Angle);
	const float Sn = FMath::Sin(Angle);
	const float Size = Shape.Size * Scale;
	const float W = Size * (1.f + Squash);
	const float H = Size * (1.f - Squash);
	const float Cx = InX + (Shape.ArtX * Cs - Shape.ArtY * Sn) * Scale;
	// 찌그러질 때 바닥 쪽은 그대로 두고 위가 눌리게
	const float Cy = InY + (Shape.ArtX * Sn + Shape.ArtY * Cs) * Scale + Size * Squash * 0.4f;

	// 그림자 (같은 그림을 어둡게 살짝 아래로) → 꽃
	P.RotatedBrush(FlowerBrush(Tier), Cx - W * 0.5f + 5.f, Cy - H * 0.5f + 8.f, W, H, Angle, FLinearColor(0.25f, 0.2f, 0.15f, 0.28f * Alpha));
	P.RotatedBrush(FlowerBrush(Tier), Cx - W * 0.5f, Cy - H * 0.5f, W, H, Angle, WithAlpha(FLinearColor::White, Alpha));
}

void UFlowerMiniGameWidget::PaintEffects(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	// 충격파 고리: 점을 둥글게 늘어놓는다
	for (const FRing& Ring : Rings)
	{
		const float Fade = FMath::Clamp(Ring.Life / Ring.MaxLife, 0.f, 1.f);
		if (Ring.bFlash)
		{
			// 합성 순간 번쩍: 꽉 찬 원이 빠르게 커지며 사라진다
			P.Brush(&CircleBrush, Ring.Center.X - Ring.Radius, Ring.Center.Y - Ring.Radius, Ring.Radius * 2.f, Ring.Radius * 2.f, WithAlpha(Ring.Color, Fade * 0.75f));
			continue;
		}
		const float Dot = Ring.DotSize * (0.5f + 0.5f * Fade);
		constexpr int32 Dots = 24;
		for (int32 i = 0; i < Dots; ++i)
		{
			const float A = UE_TWO_PI * i / Dots;
			P.Brush(&CircleBrush, Ring.Center.X + FMath::Cos(A) * Ring.Radius - Dot * 0.5f, Ring.Center.Y + FMath::Sin(A) * Ring.Radius - Dot * 0.5f,
				Dot, Dot, WithAlpha(Ring.Color, Fade * 0.9f));
		}
	}

	for (const FParticle& Part : Particles)
	{
		const float Fade = FMath::Clamp(Part.Life / FMath::Max(0.01f, Part.MaxLife), 0.f, 1.f);
		const FLinearColor Color = WithAlpha(Part.Color, FMath::Min(1.f, Fade * 1.5f));
		switch (Part.Shape)
		{
		case EParticleShape::Petal:
			P.RotatedBrush(&CircleBrush, Part.Pos.X - Part.Size * 0.5f, Part.Pos.Y - Part.Size * 0.28f, Part.Size, Part.Size * 0.55f, Part.Angle, Color);
			break;
		case EParticleShape::Dot:
			P.Brush(&CircleBrush, Part.Pos.X - Part.Size * 0.5f, Part.Pos.Y - Part.Size * 0.5f, Part.Size, Part.Size, Color);
			break;
		default:
			P.RotatedBrush(&SolidBrush, Part.Pos.X - Part.Size * 0.5f, Part.Pos.Y - Part.Size * 0.5f, Part.Size, Part.Size, Part.Angle, Color);
			break;
		}
	}
}

void UFlowerMiniGameWidget::PaintGame(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	const bool bRush = Mode != EMode::Practice && T >= Duration - WarningSeconds;

	// 배경과 작업대 (위험선 위쪽도 작업대 안 - 꽃이 위험선을 넘어 쌓이는 게 보이게)
	constexpr float WorkTop = 330.f;
	P.Brush(FindBrush(TEXT("shop")), 0.f, 0.f, CanvasW, CanvasH);
	P.Brush(&WorkAreaBrush, Left - 7.f, WorkTop, Right - Left + 14.f, Floor - WorkTop + 10.f);
	for (float Y = DangerLine + 30.f; Y < Floor; Y += 70.f)
	{
		P.Box(Left + 10.f, Y, Right - Left - 20.f, 1.f, WithAlpha(Hex(TEXT("c8c5ac")), 0.3f));
	}

	// 위험선 (점선)
	const FLinearColor LineColor = Over > 0.f ? Hex(TEXT("bd6579")) : Hex(TEXT("bba293"));
	for (float X = Left; X < Right; X += 24.f)
	{
		P.Box(X, DangerLine - 1.5f, FMath::Min(12.f, Right - X), 3.f, LineColor);
	}

	// 놓을 위치 안내선과 손에 든 꽃
	const float HalfW = World->Shapes[NextTier].HalfWidth;
	const float AimX = FMath::Clamp(Cursor, Left + HalfW, Right - HalfW);
	if (Mode != EMode::Result)
	{
		for (float Y = DropY + 60.f; Y < Floor; Y += 18.f)
		{
			P.Box(AimX - 1.5f, Y, 3.f, FMath::Min(4.f, Floor - Y), Hex(TEXT("a6b698")));
		}
		PaintFlower(P, AimX, DropY, 0.f, NextTier, 1.f, Cooldown > 0.f ? 0.4f : 1.f);
	}

	for (int32 i = 0; i < World->Count; ++i)
	{
		const FlowerPhysics::FBody& Body = World->Bodies[i];
		// 착지 직후엔 납작하게, 빠르게 떨어지는 중엔 길쭉하게. 새싹은 흔들흔들. 피어날 땐 크게 부풀었다 돌아온다.
		float Squash = 0.f;
		if (Body.Squash > 0.f)
		{
			Squash = 0.22f * FMath::Sin(Body.Squash / SquashSeconds * UE_PI);
		}
		else if (!Body.bLanded)
		{
			Squash = -FMath::Min(0.12f, FMath::Max(0.f, Body.VY) / 9000.f);
		}
		const float WobbleAngle = Body.Wobble > 0.f ? FMath::Sin(Body.Wobble * 22.f) * 0.35f * (Body.Wobble / WobbleSeconds) : 0.f;
		const float Scale = 1.f + Body.Pop * 1.1f * FMath::Sin(Body.Pop / 0.35f * UE_PI);
		PaintFlower(P, Body.X, Body.Y, Body.A + WobbleAngle, Body.Tier, Scale, 1.f, Squash);
	}

	// 주문에 잡혀 있는 꽃은 둥근 테두리로 - 합쳐 버리지 않게 지켜야 할 꽃
	if (ReadyIds.Num() > 0 && Mode != EMode::Result)
	{
		const float Pulse = 0.55f + 0.35f * FMath::Sin(T * 5.f);
		for (int32 i = 0; i < World->Count; ++i)
		{
			const FlowerPhysics::FBody& Body = World->Bodies[i];
			if (ReadyIds.Contains(Body.Id))
			{
				const float Size = World->Shapes[Body.Tier].EquivR * 2.3f + 16.f;
				P.Brush(&HighlightBrush, Body.X - Size * 0.5f, Body.Y - Size * 0.5f, Size, Size, WithAlpha(FLinearColor::White, Pulse));
			}
		}
	}

	if (Over > 0.f)
	{
		P.Text(FString::Printf(TEXT("넘치기까지 %.1f초"), FMath::Max(0.f, OverflowSeconds - Over)), 420.f, DangerLine + 25.f, 24.f, Hex(TEXT("b95170")), EAlign::Center, true, 2);
	}
	else
	{
		P.Text(TEXT("이 선 위에 2초 쌓이면 종료"), 420.f, DangerLine + 25.f, 21.f, Hex(TEXT("8b8574")), EAlign::Center);
	}
	if (bRush)
	{
		P.Text(TEXT("마감 준비 · 마지막 꽃을 정리해요"), 420.f, 190.f, 24.f, Rose(), EAlign::Center, true, 2);
	}

	// 효과
	PaintEffects(P);
	for (const FPopText& Pop : Pops)
	{
		// HTML 색은 어두운 배경용이라 밝은 작업대 위에서는 외곽선을 둘러 읽히게 한다
		P.Text(Pop.Text, FMath::Clamp(Pop.Pos.X, 160.f, 680.f), Pop.Pos.Y, 28.f, WithAlpha(Pop.Color, Pop.Life), EAlign::Center, false, 2);
	}
}

void UFlowerMiniGameWidget::PaintSidePanels(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	const bool bRush = Mode != EMode::Practice && T >= Duration - WarningSeconds;
	const FLinearColor Line = Hex(TEXT("cfd3bb"));

	// ---- 왼쪽: 시간 · 주문 · 놓을 꽃 · 조작 ----
	{
		const float L = LeftPanelX + 44.f;
		const float LeftEdge = LeftPanelX + PanelW - 44.f;
		P.Brush(&PanelBrush, LeftPanelX, 40.f, PanelW, 1160.f);
		P.Text(TEXT("PART-TIME / LITTLE FLOWER SHOP"), L, 100.f, 20.f, Tag());
		P.Text(TEXT("꽃이 피는 오후"), L, 150.f, 44.f, Hex(TEXT("4d684b")), EAlign::Left, true);

		P.Text(TEXT("남은 시간"), L, 240.f, 22.f, Muted());
		if (Mode == EMode::Practice)
		{
			P.Text(TEXT("연습"), L, 310.f, 88.f, Leaf(), EAlign::Left, true);
		}
		else
		{
			P.Text(FString::Printf(TEXT("%d초"), FMath::CeilToInt(Duration - T)), L, 310.f, 88.f, bRush ? Rose() : Ink(), EAlign::Left, true);
		}
		P.Text(FString::Printf(TEXT("주문 %d건  ·  합성 %s점"), Orders, *FString::FormatAsNumber(MergeScore)), L, 400.f, 30.f, Ink());

		// 주문판 (원본 792×176 비율)
		P.Brush(FindBrush(TEXT("order-panel")), LeftPanelX + 24.f, 455.f, 552.f, 123.f);
		P.Text(TEXT("오늘의 주문"), L, 492.f, 20.f, Muted());
		// 주문 이름: 같은 꽃은 묶어서 "튤립 ×2 + 장미"
		FString OrderName;
		TArray<int32> Listed;
		for (const int32 Tier : Want)
		{
			if (Listed.Contains(Tier))
			{
				continue;
			}
			Listed.Add(Tier);
			int32 Count = 0;
			for (const int32 Other : Want)
			{
				Count += Other == Tier ? 1 : 0;
			}
			OrderName += (OrderName.IsEmpty() ? TEXT("") : TEXT(" + ")) + FString(Tiers[Tier].Name);
			if (Count > 1)
			{
				OrderName += FString::Printf(TEXT(" ×%d"), Count);
			}
		}
		if (Want.Num() == 0)
		{
			OrderName = Mode == EMode::Practice ? TEXT("연습 중 - 주문 없음") : TEXT("다음 주문 준비 중");
		}
		P.Text(OrderName, L, 535.f, 34.f, Ink(), EAlign::Left, true);

		// 칸마다 지금 작업대에 있는지 실시간으로 (합쳐져 사라지면 다시 빈칸)
		for (int32 j = 0; j < Want.Num(); ++j)
		{
			const bool bReady = WantReady.IsValidIndex(j) && WantReady[j];
			const float X = L + j * 168.f;
			P.Brush(bReady ? &SlotDoneBrush : &SlotWaitBrush, X, 605.f, 152.f, 125.f);
			P.ArtCentered(FlowerBrush(Want[j]), X + 76.f, 650.f, 80.f, bReady ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f));
			P.Text(bReady ? TEXT("있어요!") : Tiers[Want[j]].Name, X + 76.f, 708.f, 20.f, bReady ? Leaf() : Hex(TEXT("6c6952")), EAlign::Center, bReady);
		}
		P.Text(TEXT("작업대에 모두 함께 있으면 한꺼번에 포장돼요"), L, 760.f, 20.f, Muted());
		P.Text(TEXT("같은 꽃끼리 닿으면 합쳐지니 떨어뜨려 두세요"), L, 790.f, 18.f, Rose());

		// 놓을 꽃 · 다음 꽃
		P.Text(TEXT("놓을 꽃"), L, 850.f, 22.f, Ink(), EAlign::Left, true);
		P.ArtCentered(FlowerBrush(NextTier), L + 150.f, 850.f, 60.f);
		P.Text(TEXT("다음"), L + 250.f, 850.f, 22.f, Muted());
		P.ArtCentered(FlowerBrush(QueueTier), L + 330.f, 850.f, 52.f);

		P.Box(L, 890.f, LeftEdge - L, 2.f, Line);
		P.Text(TEXT("조작"), L, 930.f, 24.f, Ink(), EAlign::Left, true);
		P.Text(TEXT("마우스 이동 / ← → · A D"), L, 980.f, 22.f, Ink());
		P.Text(TEXT("위치 고르기"), LeftEdge, 980.f, 22.f, Muted(), EAlign::Right);
		P.Text(TEXT("클릭 / Space"), L, 1025.f, 22.f, Ink());
		P.Text(TEXT("떨어뜨리기"), LeftEdge, 1025.f, 22.f, Muted(), EAlign::Right);
		P.Text(TEXT("P / Esc"), L, 1070.f, 22.f, Ink());
		P.Text(TEXT("일시정지"), LeftEdge, 1070.f, 22.f, Muted(), EAlign::Right);
	}

	// ---- 오른쪽: 상태 · 보상 · 위험도 · 기록 · 꽃 단계표 ----
	{
		const float R = RightPanelX + 44.f;
		const float RightEdge = RightPanelX + PanelW - 44.f;
		P.Brush(&PanelBrush, RightPanelX, 40.f, PanelW, 1160.f);
		P.Text(TEXT("오늘의 근무"), R, 110.f, 30.f, Ink(), EAlign::Left, true);

		// 상태판 (원본 800×100 비율)
		P.Brush(FindBrush(TEXT("status-panel")), RightPanelX + 24.f, 160.f, 552.f, 69.f);
		const FString Status = Mode == EMode::Practice ? TEXT("연습 중 - 시간 제한 없어요")
			: Mode != EMode::Play ? TEXT("근무 준비 중")
			: Over > 0.f ? TEXT("넘치기 직전! 합치거나 포장해서 비워요")
			: ChainTimer > 0.f && Chain > 1 ? FString::Printf(TEXT("%d연쇄 중!"), Chain)
			: TEXT("같은 꽃을 붙여 주문 꽃을 피워요");
		P.Text(Status, RightPanelX + PanelW * 0.5f, 194.f, 22.f, Ink(), EAlign::Center);

		// 예상 보상 + 다음 단계까지 몇 건 남았는지
		const FMiniGameReward Preview = PreviewReward(Orders);
		const FString Stat = StatLabel(Preview.Stat);
		P.Text(TEXT("예상 보상"), R, 300.f, 22.f, Muted());
		P.Text(FString::Printf(TEXT("%s +%d"), *Stat, Preview.StatAmount), R, 362.f, 48.f, Leaf(), EAlign::Left, true);
		P.Text(FString::Printf(TEXT("급여 %s원 (고정)"), *FString::FormatAsNumber(Preview.Money)), R, 428.f, 26.f, Ink());
		for (int32 More = 1; More <= 20; ++More)
		{
			if (PreviewReward(Orders + More).StatAmount > Preview.StatAmount)
			{
				P.Text(FString::Printf(TEXT("주문 %d건 더 채우면 +%d"), More, Preview.StatAmount + 1), R, 470.f, 22.f, Muted());
				break;
			}
		}

		// 위험선 게이지
		const float Danger = FMath::Clamp(Over / OverflowSeconds, 0.f, 1.f);
		P.Text(TEXT("위험선"), R, 560.f, 24.f, Ink(), EAlign::Left, true);
		P.Text(Danger > 0.f ? FString::Printf(TEXT("넘치기까지 %.1f초"), OverflowSeconds - Over) : FString(TEXT("여유 있어요")),
			RightEdge, 560.f, 22.f, Danger > 0.f ? Rose() : Muted(), EAlign::Right);
		P.Brush(&BarBackBrush, R, 592.f, RightEdge - R, 24.f);
		if (Danger > 0.f)
		{
			P.Brush(&DangerFillBrush, R, 592.f, (RightEdge - R) * Danger, 24.f);
		}
		P.Text(TEXT("방금 놓은 꽃은 1.3초 동안 봐줘요"), R, 650.f, 22.f, Muted());

		// 기록
		P.Box(R, 710.f, RightEdge - R, 2.f, Line);
		P.Text(TEXT("최고 단계"), R, 755.f, 24.f, Ink());
		P.ArtCentered(FlowerBrush(BestTier), RightEdge - 150.f, 755.f, 44.f);
		P.Text(Tiers[BestTier].Name, RightEdge, 755.f, 24.f, Ink(), EAlign::Right, true);
		P.Text(TEXT("최고 연쇄"), R, 805.f, 24.f, Ink());
		P.Text(FString::Printf(TEXT("%d연쇄"), BestChain), RightEdge, 805.f, 24.f, Ink(), EAlign::Right, true);
		P.Text(TEXT("특대 꽃다발"), R, 855.f, 24.f, Ink());
		P.Text(FString::Printf(TEXT("%d개"), Bouquets), RightEdge, 855.f, 24.f, Ink(), EAlign::Right, true);

		// 꽃 단계표 (HTML 맨 아래 줄을 4개씩 두 줄로)
		P.Box(R, 905.f, RightEdge - R, 2.f, Line);
		P.Text(TEXT("같은 꽃 둘 → 다음 단계"), R, 945.f, 22.f, Ink(), EAlign::Left, true);
		for (int32 i = 0; i < NumTiers; ++i)
		{
			const float X = R + 56.f + (i % 4) * 128.f;
			const float Y = 1010.f + (i / 4) * 100.f;
			P.ArtCentered(FlowerBrush(i), X, Y, 58.f, i <= BestTier ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f));
			P.Text(Tiers[i].Name, X, Y + 42.f, 16.f, Hex(TEXT("6c755b")), EAlign::Center);
			if (i % 4 != 3)
			{
				P.Text(TEXT("›"), X + 64.f, Y, 26.f, Muted(), EAlign::Center);
			}
		}
	}
}

void UFlowerMiniGameWidget::PaintOverlay(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	P.Brush(&OverlayBrush, 0.f, 0.f, CanvasW, CanvasH);

	if (Mode == EMode::Title)
	{
		const FMiniGameReward Reward = PreviewReward(0);
		P.ArtCentered(FindBrush(TEXT("tulip")), 420.f, 210.f, 120.f);
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 300.f, 19.f, Tag(), EAlign::Center);
		P.Text(TEXT("꽃이 피는 작업대"), 420.f, 370.f, 42.f, Ink(), EAlign::Center, true);
		P.Text(TEXT("마우스로 위치를 고르고 클릭해서 꽃을 떨어뜨려요."), 420.f, 460.f, 26.f, Ink(), EAlign::Center);
		P.Text(TEXT("같은 꽃 둘이 닿으면 다음 단계로 피어나요."), 420.f, 510.f, 26.f, Ink(), EAlign::Center);
		P.Text(TEXT("주문 꽃이 모두 작업대에 있으면 포장 · 위험선 2초면 끝"), 420.f, 560.f, 26.f, Ink(), EAlign::Center);
		P.Text(FString::Printf(TEXT("%d초 근무 · P / Esc 일시정지"), FMath::RoundToInt(Duration)), 420.f, 610.f, 26.f, Ink(), EAlign::Center);
		P.Text(FString::Printf(TEXT("급여 %s원 · 주문을 많이 채울수록 %s이 올라요"), *FString::FormatAsNumber(Reward.Money), *StatLabel(Reward.Stat)),
			420.f, 690.f, 24.f, Leaf(), EAlign::Center);

		if (PlayCount == 0)
		{
			P.Text(TEXT("처음이라 출근 전에 짧게 연습해 볼게요."), 420.f, 770.f, 22.f, Muted(), EAlign::Center);
		}
		else
		{
			P.Text(FString::Printf(TEXT("대충 일하면 급여는 같고 %s은 +1만 올라요."), *StatLabel(Reward.Stat)), 420.f, 1060.f, 20.f, Muted(), EAlign::Center);
		}
	}
	else if (Mode == EMode::Tutorial)
	{
		PaintTutorialPage(P);
	}
	else if (Mode == EMode::Pause)
	{
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 470.f, 19.f, Tag(), EAlign::Center);
		P.Text(TEXT("잠깐, 쉬는 시간"), 420.f, 540.f, 42.f, Ink(), EAlign::Center, true);
		P.Text(TEXT("시간이 멈췄습니다."), 420.f, 610.f, 26.f, Ink(), EAlign::Center);
	}
	else if (Mode == EMode::Result)
	{
		P.Text(TEXT("PART-TIME ARCADE"), 420.f, 270.f, 19.f, Tag(), EAlign::Center);
		if (bSkipped)
		{
			P.Text(TEXT("대충 일하고 퇴근했어요."), 420.f, 340.f, 42.f, Ink(), EAlign::Center, true);
			P.Text(TEXT("오늘은 물만 주다 왔어요."), 420.f, 420.f, 26.f, Ink(), EAlign::Center);
			P.Text(FString::Printf(TEXT("직접 일하면 %s을 더 올릴 수 있어요."), *StatLabel(FinalReward.Stat)), 420.f, 470.f, 26.f, Muted(), EAlign::Center);
		}
		else
		{
			P.Text(bOverflowed ? TEXT("작업대가 가득 찼어요.") : TEXT("꽃집 마감! 오늘도 향긋하게."), 420.f, 340.f, 42.f, Ink(), EAlign::Center, true);
			P.Text(FString::Printf(TEXT("주문 %d건 · 합성 %s점"), Orders, *FString::FormatAsNumber(MergeScore)), 420.f, 420.f, 26.f, Ink(), EAlign::Center);
			P.Text(FString::Printf(TEXT("최고 단계 %s · 최고 연쇄 %d"), Tiers[BestTier].Name, BestChain), 420.f, 470.f, 26.f, Ink(), EAlign::Center);
		}

		// 영수증
		P.Brush(&ReceiptBrush, 170.f, 540.f, 500.f, 300.f);
		P.Text(TEXT("급여"), 210.f, 600.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), 630.f, 600.f, 26.f, Ink(), EAlign::Right, true);
		P.Text(StatLabel(FinalReward.Stat), 210.f, 670.f, 26.f, Ink());
		P.Text(FString::Printf(TEXT("+%d"), FinalReward.StatAmount), 630.f, 670.f, 26.f, Ink(), EAlign::Right, true);
		P.Box(210.f, 720.f, 420.f, 2.f, Hex(TEXT("a9b893")));
		P.Text(TEXT("받은 돈"), 210.f, 780.f, 28.f, Ink(), EAlign::Left, true);
		P.Text(FString::Printf(TEXT("%s원"), *FString::FormatAsNumber(FinalReward.Money)), 630.f, 780.f, 30.f, Leaf(), EAlign::Right, true);
		if (!bSkipped)
		{
			P.Text(TEXT("미완성 주문은 치지 않아요"), 420.f, 870.f, 20.f, Muted(), EAlign::Center);
		}
	}

	PaintButtons(P);
}

void UFlowerMiniGameWidget::PaintButtons(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	// 주 버튼은 그림, 보조 버튼은 테두리만 있는 작은 버튼
	const TArray<FOverlayButton> Buttons = GetOverlayButtons();
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FOverlayButton& B = Buttons[i];
		const bool bPressed = PressedButton == i;
		const bool bHovered = HoveredButton == i;
		const float Press = bPressed ? 2.f : 0.f;
		const float BoxLeft = B.X - B.W * 0.5f;
		const float Top = B.Y - B.H * 0.5f + Press;

		if (B.bPrimary)
		{
			const TCHAR* ButtonArt = bPressed ? TEXT("button-pressed") : bHovered ? TEXT("button-hover") : TEXT("button-normal");
			P.Brush(FindBrush(ButtonArt), BoxLeft, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 26.f, FLinearColor::White, EAlign::Center, true, 2);
		}
		else
		{
			P.Brush(bHovered || bPressed ? &SecondaryHoverBrush : &SecondaryBrush, BoxLeft, Top, B.W, B.H);
			P.Text(B.Label, B.X, B.Y + Press, 22.f, Leaf(), EAlign::Center, true);
		}
	}
}

void UFlowerMiniGameWidget::PaintTutorialPage(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	// 설명 카드 문구. 기획 확정 전 초안 - 문구만 바꾸면 된다.
	// (Live Coding 크래시를 피하려고 함수 안 static 배열은 쓰지 않는다)
	const FMiniGameReward Reward = PreviewReward(0);
	const FString Stat = StatLabel(Reward.Stat);
	const FString Titles[] = { TEXT("꽃 떨어뜨리기"), TEXT("같은 꽃 합치기"), TEXT("주문과 위험선") };
	const FString Lines[3][3] =
	{
		{
			TEXT("마우스로 위치를 고르고 클릭하면 꽃이 떨어져요."),
			TEXT("← → · A D로 옮기고 Space로 떨어뜨려도 돼요."),
			TEXT("놓을 꽃과 다음 꽃은 왼쪽 패널에서 미리 볼 수 있어요."),
		},
		{
			TEXT("같은 꽃 둘이 닿으면 다음 단계로 피어나요."),
			TEXT("해바라기부터는 피어날 때 쿵! 주변 꽃을 밀어내요."),
			TEXT("튤립 ×2 주문이면 튤립끼리 닿지 않게 떨어뜨려 두세요."),
		},
		{
			TEXT("주문 꽃이 작업대에 모두 함께 있으면 한꺼번에 포장돼요."),
			TEXT("꽃이 위험선 위로 2초 넘게 쌓이면 근무가 끝나요."),
			FString::Printf(TEXT("주문을 많이 채울수록 %s이 더 올라요 (급여는 고정)."), *Stat),
		},
	};

	const int32 Page = FMath::Clamp(TutorialPage, 0, 2);
	P.Text(FString::Printf(TEXT("일하는 법  %d / 3"), Page + 1), 420.f, 250.f, 20.f, Tag(), EAlign::Center);
	P.Text(Titles[Page], 420.f, 320.f, 42.f, Ink(), EAlign::Center, true);

	// 그림
	if (Page == 0)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			PaintFlower(P, 420.f + (i - 1) * 190.f, 540.f, 0.f, i);
		}
	}
	else if (Page == 1)
	{
		// 씨앗 + 씨앗 → 새싹 → … 단계표
		for (int32 i = 0; i < NumTiers; ++i)
		{
			const float X = 150.f + (i % 4) * 180.f;
			const float Y = 470.f + (i / 4) * 140.f;
			P.ArtCentered(FlowerBrush(i), X, Y, 92.f);
			P.Text(Tiers[i].Name, X, Y + 58.f, 18.f, Hex(TEXT("6c755b")), EAlign::Center);
			if (i % 4 != 3)
			{
				P.Text(TEXT("›"), X + 90.f, Y, 34.f, Muted(), EAlign::Center);
			}
		}
	}
	else
	{
		PaintFlower(P, 290.f, 560.f, 0.f, 3, 0.8f);
		P.Text(TEXT("포장 완료!"), 300.f, 470.f, 26.f, Leaf(), EAlign::Center, true);
		// 위험선 미리보기
		for (float X = 450.f; X < 690.f; X += 24.f)
		{
			P.Box(X, 520.f, 12.f, 3.f, Hex(TEXT("bd6579")));
		}
		PaintFlower(P, 580.f, 545.f, 0.f, 4, 0.7f);
		P.Text(TEXT("2초면 끝"), 570.f, 640.f, 24.f, Rose(), EAlign::Center, true);
	}

	for (int32 i = 0; i < 3; ++i)
	{
		P.Text(Lines[Page][i], 420.f, 730.f + i * 50.f, 26.f, i == 2 ? Leaf() : Ink(), EAlign::Center);
	}

	// 몇 번째 카드인지 점으로
	for (int32 i = 0; i < 3; ++i)
	{
		const float X = 420.f + (i - 1) * 30.f;
		P.Brush(i == Page ? &BarFillBrush : &BarBackBrush, X - 7.f, 913.f, 14.f, 14.f);
	}
}

void UFlowerMiniGameWidget::PaintPractice(FFlowerPainter& P) const
{
	using namespace FlowerGame;

	// 단계마다 한 줄 안내 + 한 줄 보충. 글을 길게 읽게 하지 않고 직접 해 보면서 익히게 한다.
	// (Live Coding 크래시를 피하려고 함수 안 static 배열은 쓰지 않는다)
	const TCHAR* const Prompts[PracticeSteps][2] =
	{
		{ TEXT("마우스로 위치를 고르고 클릭해 씨앗을 떨어뜨려요"), TEXT("← → · A D로 옮기고 Space로 떨어뜨려도 돼요") },
		{ TEXT("방금 씨앗 위에 씨앗을 하나 더 떨어뜨려요"), TEXT("같은 꽃 둘이 닿으면 다음 단계로 피어나요") },
		{ TEXT("이번엔 새싹끼리 붙여 데이지를 피워요"), TEXT("단계가 오를수록 꽃이 커져요") },
		{ TEXT("데이지끼리 합쳐 주문한 튤립을 피워요"), TEXT("주문 꽃이 작업대에 있으면 바로 포장돼요") },
		{ TEXT("위험선을 조심해요!"), TEXT("작업대가 넘치면 근무가 바로 끝나요") },
		{ TEXT("준비 끝! 곧 출근해요"), TEXT("") },
	};

	const int32 Step = FMath::Clamp(PracticeStep, 0, PracticeSteps - 1);
	const float Pulse = 0.6f + 0.4f * FMath::Sin(PracticeStepTime * 6.f);

	// 안내판 (놓을 꽃이 보이는 y 265 위쪽에)
	P.Brush(&PanelBrush, 50.f, 30.f, CanvasW - 100.f, 150.f);
	P.Text(FString::Printf(TEXT("연습  %d / %d"), FMath::Min(Step + 1, PracticeSteps - 1), PracticeSteps - 1), 420.f, 62.f, 20.f, Tag(), EAlign::Center);
	P.Text(Prompts[Step][0], 420.f, 108.f, 30.f, Ink(), EAlign::Center, true);
	P.Text(Prompts[Step][1], 420.f, 152.f, 22.f, Muted(), EAlign::Center);

	// 처음엔 "클릭 / Space"를 크게 깜빡인다
	if (Step == PracticeDropStep)
	{
		P.Brush(&SecondaryBrush, 245.f, 620.f, 350.f, 90.f, WithAlpha(FLinearColor::White, Pulse));
		P.Text(TEXT("클릭 / Space"), 420.f, 665.f, 40.f, WithAlpha(Leaf(), Pulse), EAlign::Center, true);
	}

	// 합칠 짝이 되는 꽃(지금 손에 든 꽃과 같은 단계)을 둥근 테두리로 표시
	if (Step >= PracticeMergeStep && Step <= PracticeOrderStep)
	{
		for (int32 i = 0; i < World->Count; ++i)
		{
			const FlowerPhysics::FBody& Body = World->Bodies[i];
			if (Body.Tier == NextTier)
			{
				const float Size = World->Shapes[Body.Tier].EquivR * 2.4f + 30.f;
				P.Brush(&HighlightBrush, Body.X - Size * 0.5f, Body.Y - Size * 0.5f, Size, Size, WithAlpha(FLinearColor::White, Pulse));
			}
		}
	}

	// 튤립 주문 단계: 무엇을 만들어야 하는지 작업대 안에 크게
	if (Step == PracticeOrderStep)
	{
		P.Text(TEXT("데이지 + 데이지 = 튤립"), 420.f, 420.f, 28.f, WithAlpha(Rose(), Pulse), EAlign::Center, true, 2);
	}

	// 위험선 설명: 선을 깜빡이고 규칙을 항목별로 짚어 준다
	if (Step == DangerExplainStep)
	{
		const FLinearColor Bad = Hex(TEXT("bd6579"));
		P.Box(Left, DangerLine - 4.f, Right - Left, 8.f, WithAlpha(Bad, Pulse));
		P.Brush(&ReceiptBrush, 90.f, 400.f, CanvasW - 180.f, 340.f);
		P.Text(TEXT("위험선 규칙"), 130.f, 450.f, 30.f, Bad, EAlign::Left, true);

		struct FRow { const TCHAR* Label; const TCHAR* Value; };
		const FRow Rows[] =
		{
			{ TEXT("넘치면"), TEXT("선 위로 2초 넘게 쌓이면 종료") },
			{ TEXT("방금 놓은 꽃"), TEXT("1.3초 동안은 괜찮아요") },
			{ TEXT("자리 만들기"), TEXT("합치거나 주문으로 포장") },
			{ TEXT("미리 보기"), TEXT("오른쪽 위험선 게이지") },
		};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Rows); ++i)
		{
			const float Y = 520.f + i * 52.f;
			P.Text(Rows[i].Label, 130.f, Y, 24.f, Ink(), EAlign::Left, true);
			P.Text(Rows[i].Value, CanvasW - 130.f, Y, 24.f, Bad, EAlign::Right, true);
		}
		P.Text(TEXT("Space 또는 \"알겠어요\"를 눌러 계속"), 420.f, 890.f, 20.f, Muted(), EAlign::Center);
	}

	PaintButtons(P);
}
