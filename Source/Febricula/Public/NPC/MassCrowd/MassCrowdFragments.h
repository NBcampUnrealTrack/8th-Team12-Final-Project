// Fill out your copyright notice in the Description page of Project Settings.
//
// MassEntity 기반 군중의 데이터 조각(Fragment)들.
//
// [액터 버전과의 차이]
//   액터 버전(ASubNpcCharacter)은 한 클래스 안에 상태·이동·판단이 전부 들어 있다.
//   Mass는 그걸 조각으로 쪼개서 배열로 모은다. 프로세서는 "필요한 조각을 가진
//   엔티티 전부"를 한꺼번에 훑기 때문에, 마릿수가 늘어도 캐시 효율이 유지되고
//   워커 스레드로 잘라 돌릴 수 있다.
//
// [조각을 나누는 기준]
//   같이 읽고 쓰이는 값끼리 묶는다. 예를 들어 경로 진행값과 차선 오프셋은
//   이동 프로세서가 항상 같이 만지므로 한 조각에 둔다. 반대로 스팟 상태는
//   걷는 개체에는 아예 없어야 하므로 따로 뺀다 (없는 조각은 메모리도 안 먹는다).

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "MassCrowdFragments.generated.h"

/**
 * 경로 추종 상태.
 *
 * 액터 버전의 PathDistance / LaneOffset / bReversedOnPath / WalkSpeed에 대응한다.
 * 위치를 FVector로 들고 있지 않는 것이 핵심이다 - 경로 인덱스와 진행 거리
 * 두 값이면 위치가 결정되므로, 이동은 float 하나를 더하는 것으로 끝난다.
 */
USTRUCT()
struct FEBRICULA_API FMassCrowdPathFragment : public FMassFragment
{
	GENERATED_BODY()

	/** UMassCrowdPathSubsystem이 구워둔 경로 배열의 인덱스. INDEX_NONE이면 경로 없음 */
	int32 PathIndex = INDEX_NONE;

	/** 경로 위 진행 거리 (cm) */
	float Distance = 0.f;

	/** 좌우 차선 오프셋 (cm) */
	float LaneOffset = 0.f;

	/** 이 개체의 걷기 속도 (cm/s) */
	float Speed = 160.f;

	/** 역방향(끝 -> 시작)으로 타는 중인가 */
	bool bReversed = false;
};

/**
 * 이번 프레임에 이웃들이 밀어낸 양.
 *
 * 회피 프로세서가 쓰고 이동 프로세서가 읽어서 0으로 되돌린다.
 * 액터 버전에서 개체가 서로의 상태를 직접 읽던 것을, "쓰는 쪽과 읽는 쪽을
 * 프로세서 단계로 분리"해서 해결한 형태다. 병렬 실행에서 레이스가 나지 않는다.
 */
USTRUCT()
struct FEBRICULA_API FMassCrowdAvoidanceFragment : public FMassFragment
{
	GENERATED_BODY()

	float PendingNudge = 0.f;
};

/** 활동 스팟에서 뭔가 하는 중인 개체만 갖는 조각 */
USTRUCT()
struct FEBRICULA_API FMassCrowdSpotFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 점유 중인 스팟 인덱스 */
	int32 SpotIndex = INDEX_NONE;

	/** 몇 번째 단계인가. -1 = 첫 동작 */
	int32 StepIndex = -1;

	/** 이 단계가 끝나는 시각 (월드 시간) */
	float EndTime = 0.f;
};

/** 플레이어와 부딪힌 기록. 반응 판정과 대사 분기에 쓴다 */
USTRUCT()
struct FEBRICULA_API FMassCrowdBumpFragment : public FMassFragment
{
	GENERATED_BODY()

	float LastBumpTime = -1000.f;
	float LastReactionTime = -1000.f;

	/** 반응이 끝나는 시각 */
	float ReactionEndTime = 0.f;

	/** ESubNpcReaction을 uint8로. 조각은 가볍게 유지한다 */
	uint8 ReactionType = 0;
};

/**
 * 아키타입별로 공유되는 값.
 *
 * 개체마다 복사하지 않고 한 벌만 두고 공유한다. 속도 범위나 반응 가중치처럼
 * "같은 유형이면 전부 같은 값"인 것들이 여기 들어간다. 1000명이면 1000벌이 아니라
 * 유형 수만큼만 존재한다.
 */
USTRUCT()
struct FEBRICULA_API FMassCrowdSharedConfig : public FMassSharedFragment
{
	GENERATED_BODY()

	float WalkSpeedMin = 140.f;
	float WalkSpeedMax = 200.f;

	float DodgeWeight = 3.f;
	float AngryWeight = 1.f;
	float SurprisedWeight = 2.f;

	/** 이 속도 이상으로 부딪혀야 반응한다 */
	float BumpSpeedThreshold = 300.f;

	/** 반응 쿨다운 / 지속 시간 */
	float ReactionCooldown = 4.f;
	float ReactionDuration = 2.f;

	/** 소프트 회피 */
	float AvoidDistance = 110.f;
	float AvoidStrength = 0.6f;
	float MaxAvoidOffset = 60.f;
};

// ---------------------------------------------------------------------------
// 태그 - 값이 없고 "이 상태인가"만 표시한다.
//
// 태그로 상태를 나누면 프로세서 쿼리가 그 상태의 엔티티만 훑는다.
// 액터 버전에서 매 틱 switch로 갈라내던 것을, 아예 순회 대상에서 빼는 것으로 바꾼 셈이다.
// enum 하나를 검사하는 비용도 없어진다.
// ---------------------------------------------------------------------------

/** 경로를 걷는 중 */
USTRUCT()
struct FEBRICULA_API FMassCrowdWalkingTag : public FMassTag
{
	GENERATED_BODY()
};

/** 스팟에서 활동 중 */
USTRUCT()
struct FEBRICULA_API FMassCrowdAtSpotTag : public FMassTag
{
	GENERATED_BODY()
};

/** 플레이어와 부딪혀 반응 중 */
USTRUCT()
struct FEBRICULA_API FMassCrowdReactingTag : public FMassTag
{
	GENERATED_BODY()
};

/** 경로 끝(퇴장구)에 닿아 이번 프레임에 제거될 예정 */
USTRUCT()
struct FEBRICULA_API FMassCrowdPendingDespawnTag : public FMassTag
{
	GENERATED_BODY()
};
