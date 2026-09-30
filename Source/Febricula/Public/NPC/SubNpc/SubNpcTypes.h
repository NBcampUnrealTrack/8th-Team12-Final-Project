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

	/** 선택 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

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

	/** 피하기 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction", meta = (ClampMin = "0.0"))
	float DodgeWeight = 3.f;

	/** 화내기 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction", meta = (ClampMin = "0.0"))
	float AngryWeight = 1.f;

	/** 놀라기 가중치 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction", meta = (ClampMin = "0.0"))
	float SurprisedWeight = 2.f;

	/** 반응별 애니메이션 (AnimLibrary 키). 비우면 애니메이션 없이 대사만 나온다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName DodgeAnimId = TEXT("Dodge");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName AngryAnimId = TEXT("Angry");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName SurprisedAnimId = TEXT("Surprised");

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
	// 비워두면 위의 DodgeAnimId로 대체된다 (하나만 만들어도 동작한다).

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

	/**
	 * 천천히 부딪혔을 때 (플레이어가 살살 밀거나, 행인이 걸어오다 플레이어와 닿았을 때).
	 * 화내거나 놀랄 정도는 아니고 "어, 죄송" 하고 멈칫하는 동작. 비워두면 SurprisedAnimId로 대체된다.
	 *
	 * 기본값을 채워둔 이유: 아키타입 테이블이 없으면 이 구조체의 기본값이 그대로 쓰인다.
	 * NPC 블루프린트의 AnimLibrary에 "LightBump" 키만 등록하면 테이블 없이도 재생된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Archetype|Reaction")
	FName LightBumpAnimId = TEXT("LightBump");
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
