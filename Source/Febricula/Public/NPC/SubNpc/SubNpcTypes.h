// Fill out your copyright notice in the Description page of Project Settings.
//
// 서브 NPC(행인) 군중 시스템의 공용 타입.
//
// 메인 NPC와 완전히 다른 설계를 쓴다:
//   - 메인 NPC : 소수, NavMesh + BehaviorTree, 대화 이벤트 트리
//   - 서브 NPC : 다수, 스플라인 추종(길찾기 없음), 풀링, 거리 기반 LOD
//
// 서브 NPC는 수십~수백 마리가 동시에 존재하므로 "한 마리당 비용"이 전부다.
// 그래서 NavMesh 질의도, BehaviorTree도, 개별 Tick도 쓰지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SubNpcTypes.generated.h"

class ANpcCrowdPath;
class ASubNpcCharacter;

/**
 * 거리/가시성에 따른 상세도 단계. USubNpcDirectorSubsystem이 주기적으로 갱신한다.
 *
 * 메타휴먼은 한 마리만으로도 무거워서, "안 보이면 아무것도 하지 않는다"를
 * 확실히 보장하는 게 이 시스템에서 제일 중요하다.
 */
UENUM(BlueprintType)
enum class ESubNpcLodBand : uint8
{
	/** 가까움: 풀 스킨드 메시 + 매 프레임 애니메이션 + 그림자 */
	Near		UMETA(DisplayName = "Near (풀 디테일)"),

	/** 중간: LOD 강제 + 애니메이션 업데이트 레이트 감소 + 그림자 끔 */
	Mid			UMETA(DisplayName = "Mid (저해상도)"),

	/** 멀다: 최저 LOD + 렌더링될 때만 포즈 계산 + 이동은 단순 보간 */
	Far			UMETA(DisplayName = "Far (최저 LOD)"),

	/** 시야 밖이거나 너무 멂: 숨김 + 틱 정지. 더 멀면 풀로 회수된다 */
	Culled		UMETA(DisplayName = "Culled (숨김)"),
};

/** 서브 NPC가 지금 뭘 하고 있는가 */
UENUM(BlueprintType)
enum class ESubNpcActivity : uint8
{
	/** 경로를 따라 걷는 중 (기본) */
	Walking		UMETA(DisplayName = "이동 중"),

	/** 활동 스팟에서 멈춰 제스처 재생 중 */
	AtSpot		UMETA(DisplayName = "스팟 활동 중"),

	/** 플레이어와 부딪혀 반응 중 */
	Reacting	UMETA(DisplayName = "반응 중"),

	/** 풀에 들어가 있음 (비활성) */
	Pooled		UMETA(DisplayName = "풀 대기"),
};

/** 플레이어와 부딪혔을 때의 반응 종류 */
UENUM(BlueprintType)
enum class ESubNpcReaction : uint8
{
	/** 아무 일 없음 */
	None		UMETA(DisplayName = "무반응"),

	/** 옆으로 비킨다 */
	Dodge		UMETA(DisplayName = "피하기"),

	/** 멈춰서 화낸다 */
	Angry		UMETA(DisplayName = "화내기"),

	/** 놀라서 뒷걸음 */
	Surprised	UMETA(DisplayName = "놀라기"),

	/** 천천히 부딪혀서 잠깐 멈칫한다 (걷다가 사람과 어깨가 닿은 정도) */
	LightBump	UMETA(DisplayName = "살짝 부딪힘"),

	/** 플레이어와 대화 중. 대화창이 닫힐 때까지 멈춰 서 있는다 */
	Talking		UMETA(DisplayName = "대화 중"),

	/** 일행끼리 길에서 멈춰 서로 마주 보고 이야기한다 */
	GroupChat	UMETA(DisplayName = "일행과 대화"),
};

/**
 * 부딪힘 속도 구간 하나. 플레이어 속도가 MinSpeed 이상이면 이 반응이 후보가 된다.
 * 후보 중 MinSpeed가 가장 큰 구간 하나만 쓰인다.
 */
USTRUCT(BlueprintType)
struct FSubNpcBumpTier
{
	GENERATED_BODY()

	/** 이 속도(cm/s) 이상으로 부딪히면 이 구간. 플레이어 걷기 500, 달리기 1000 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bump", meta = (ClampMin = "0.0", Units = "CentimetersPerSecond"))
	float MinSpeed = 0.f;

	/** 반응 종류. 피하기는 옆으로 비키며 걷고, 나머지는 멈춰서 플레이어를 쳐다본다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bump")
	ESubNpcReaction Reaction = ESubNpcReaction::LightBump;

	/** 재생할 애니메이션 (NPC 블루프린트 AnimLibrary 키). 비우면 애니메이션 없이 반응만 한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bump")
	FName AnimId;

	FSubNpcBumpTier() = default;
	FSubNpcBumpTier(float InMinSpeed, ESubNpcReaction InReaction, FName InAnimId)
		: MinSpeed(InMinSpeed), Reaction(InReaction), AnimId(InAnimId) {}
};

/**
 * 경로 하나의 출구. 갈림길은 이 배열을 여러 개 두는 것으로 표현한다.
 * Weight가 높을수록 자주 선택된다 (가중 랜덤).
 */
USTRUCT(BlueprintType)
struct FNpcPathExit
{
	GENERATED_BODY()

	/** 이어질 다음 경로 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TObjectPtr<ANpcCrowdPath> NextPath;

	/** 선택 가중치. 0 이하면 절대 선택되지 않는다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/**
	 * 다음 경로를 역방향(끝 → 시작)으로 탈지.
	 * 스플라인 하나를 양방향 인도로 쓸 때 켠다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bEnterReversed = false;
};

/**
 * 활동 스팟 행동의 한 단계.
 *
 * [왜 단계가 필요한가]
 *   행동이 애니메이션 하나로 끝나면 "자판기 앞에 서서 3초 있다 걸어간다"밖에 안 된다.
 *   실제 사람은 "동전을 넣고 -> 버튼을 누르고 -> 음료를 집는다"처럼 여러 동작을 잇는다.
 *   상태(enum)를 늘리지 않고 데이터만 늘려서 이걸 표현하려고 단계 배열을 둔다.
 */
USTRUCT(BlueprintType)
struct FNpcActivityStep
{
	GENERATED_BODY()

	/** 재생할 애니메이션 (AnimLibrary 키). 비우면 애니메이션 없이 그 시간만큼 서 있는다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FName AnimId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float MinDuration = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float MaxDuration = 4.f;

	/** 지속 포즈면 true, 한 번 재생하고 끝나는 동작이면 false */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	bool bLoop = true;
};

/**
 * 활동 스팟에서 재생할 행동 하나.
 * "키오스크 앞에서 주문하거나 / 메뉴를 고민하거나" 같은 선택지를 데이터로 표현한다.
 */
USTRUCT(BlueprintType)
struct FNpcActivityOption
{
	GENERATED_BODY()

	/** 재생할 애니메이션 (NPC의 AnimLibrary 키) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FName AnimId;

	/** 선택 가중치. Chance가 0인 행동끼리만 비교한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/**
	 * 고정 확률 (0~1). 0보다 크면 가중치 대신 **정확히 이 확률**로 이 행동이 뽑힌다.
	 * 예) 0.1 = 이 스팟에 들른 행인의 10%가 이 행동을 한다.
	 *
	 * 고정 확률 행동이 하나도 안 뽑히면 나머지(Chance 0) 행동을 가중치로 고른다.
	 * 나머지가 없으면 그 행인은 스팟에 들르지 않고 지나간다.
	 * 한 스팟의 Chance 합이 1을 넘으면 뒤쪽 행동은 그만큼 덜 뽑힌다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 0.f;

	/**
	 * 이 스팟에서 한 번에 한 명만 할 수 있는 행동인가.
	 * 누군가 이미 이 행동(또는 같은 AnimId의 행동)을 하는 중이면 다른 행인은 고르지 못한다.
	 * 벤치에 둘이 같은 포즈로 나란히 앉는 식의 "복제" 그림을 막는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	bool bExclusive = false;

	/** 이 행동을 지속할 시간 (초). Min~Max 사이 랜덤 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float MinDuration = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float MaxDuration = 6.f;

	/** 지속 포즈(서서 구경)면 true, 한 번 재생하고 끝나는 동작이면 false */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	bool bLoop = true;

	/**
	 * 이 행동을 할 때 바라볼 지점 (스팟 액터 로컬 기준).
	 * 키오스크 화면, 진열장, 거울 등 "무엇을 보고 있는가"가 연출의 절반이다.
	 * 0,0,0이면 스팟 액터의 정면을 본다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	FVector LookAtOffset = FVector::ZeroVector;

	/**
	 * 첫 동작(위 AnimId) 다음에 이어서 재생할 단계들. 비워두면 한 동작으로 끝난다.
	 * 순서대로 재생하고 마지막 단계가 끝나면 다시 걷기 시작한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity")
	TArray<FNpcActivityStep> ExtraSteps;
};

/**
 * 행인 유형 한 개 = 아키타입 DataTable 한 행.
 *
 * 메타휴먼 애셋을 여러 개 만들 여유가 없으므로, 같은 메시라도 걸음 속도·성격·대사를
 * 다르게 줘서 "다른 사람처럼" 보이게 하는 게 목적이다.
 */
USTRUCT(BlueprintType)
struct FSubNpcArchetypeRow : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 외형 변형 태그. 이 값을 블루프린트 훅(OnArchetypeApplied)에서 읽어
	 * 메시·머티리얼·의상을 바꾸면 된다.
	 *
	 * 클래스를 유형별로 나누지 않는 이유: 풀링은 "같은 클래스를 재활용"하는 구조라
	 * 유형마다 클래스가 다르면 풀을 유형 수만큼 따로 만들어야 하고 그만큼 메모리가 늘어난다.
	 * 한 클래스로 만들어두고 겉모습만 갈아끼우는 게 메타휴먼에서 훨씬 싸다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype")
	FName AppearanceTag;

	/** 이 유형이 뽑힐 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype", meta = (ClampMin = "0.0"))
	float SpawnWeight = 1.f;

	/** 걷기 속도 범위 (cm/s). 개체마다 이 사이에서 랜덤 - 같은 속도로 걸으면 로봇처럼 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Movement")
	float WalkSpeedMin = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Movement")
	float WalkSpeedMax = 200.f;

	/** 체격 랜덤화 범위 (액터 스케일). 1.0 고정이면 복제인간처럼 보인다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Movement")
	float ScaleMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Movement")
	float ScaleMax = 1.05f;

	// ---- 플레이어와 부딪혔을 때 ----

	/**
	 * 부딪힌 속도 구간별 반응. **플레이어 속도(cm/s)만으로** 고른다 (랜덤·무게 없음).
	 *
	 * 부딪힌 순간 플레이어 수평 속도 이하에서 MinSpeed가 가장 큰 구간이 뽑힌다.
	 * 예) 0 → 살짝 부딪힘, 700 → 놀라기  이면
	 *     걷기(500)로 닿으면 살짝 부딪힘, 달리기(1000)로 닿으면 놀라기.
	 * 순서는 상관없다 (코드가 MinSpeed로 고른다). 구간을 늘리려면 행만 추가하면 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction", meta = (TitleProperty = "{MinSpeed} → {Reaction} ({AnimId})"))
	TArray<FSubNpcBumpTier> BumpTiers;

	/** 속도에 맞는 구간을 찾는다. 해당 구간이 없으면 nullptr */
	const FSubNpcBumpTier* FindBumpTier(float Speed) const
	{
		const FSubNpcBumpTier* Best = nullptr;
		for (const FSubNpcBumpTier& Tier : BumpTiers)
		{
			if (Speed >= Tier.MinSpeed && (!Best || Tier.MinSpeed > Best->MinSpeed))
			{
				Best = &Tier;
			}
		}
		return Best;
	}

	FSubNpcArchetypeRow()
	{
		// 테이블에 새 행을 만들었을 때 바로 동작하도록 기본 구간을 깔아둔다.
		// 700은 플레이어 걷기(500)와 달리기(1000) 사이다.
		BumpTiers.Add(FSubNpcBumpTier(0.f, ESubNpcReaction::LightBump, TEXT("LightBump")));
		BumpTiers.Add(FSubNpcBumpTier(700.f, ESubNpcReaction::Surprised, TEXT("HardBump")));
	}

	// ---- 대사 ----

	/** 평소 말 걸었을 때 나오는 한 줄 대사 후보. 매번 랜덤으로 하나 고른다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Bark")
	TArray<FText> IdleBarks;

	/**
	 * 플레이어에게 부딪힌 직후 말을 걸면 나오는 대사.
	 * 같은 NPC가 상황에 따라 다른 말을 하면 "기억하고 있다"는 느낌이 크게 산다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Bark")
	TArray<FText> BumpedBarks;

	/** 부딪힌 순간 머리 위로 튀어나오는 짧은 반응 (대화창 없이). 비워도 됨 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Bark")
	TArray<FText> BumpShouts;

	/** 화면에 표시할 이름. 비우면 "행인" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Bark")
	FText DisplayName;

	// ---- 부딪힘 반응: 방향별 애니메이션 -------------------------------------
	//
	// 오른쪽에서 부딪히면 왼쪽으로 밀리고, 왼쪽에서 부딪히면 오른쪽으로 밀린다.
	// 걷는 중이면 "비켜서는" 동작이, 서 있었으면 "밀리는" 동작이 자연스럽다.
	// 속도 구간(BumpTiers)의 반응이 "피하기"일 때만 쓰인다.
	// 비워두면 그 구간의 AnimId로 대체된다 (하나만 만들어도 동작한다).

	/** 걷는 중 왼쪽으로 비킬 때 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName DodgeLeftAnimId;

	/** 걷는 중 오른쪽으로 비킬 때 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName DodgeRightAnimId;

	/** 서 있다가 왼쪽으로 밀릴 때 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName ShoveLeftAnimId;

	/** 서 있다가 오른쪽으로 밀릴 때 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName ShoveRightAnimId;
};

/** 가중 랜덤 선택 헬퍼. 어디서나 같은 규칙을 쓰기 위해 한 곳에 모아둔다 */
namespace NpcWeightedPick
{
	/**
	 * Weights 배열에서 가중치에 비례해 인덱스 하나를 고른다.
	 * 전부 0 이하이거나 배열이 비면 INDEX_NONE.
	 */
	FEBRICULA_API int32 PickIndex(const TArray<float>& Weights);
}
