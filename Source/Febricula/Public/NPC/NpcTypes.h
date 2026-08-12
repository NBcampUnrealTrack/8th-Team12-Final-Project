// Fill out your copyright notice in the Description page of Project Settings.
//
// 프로젝트 내 모든 NPC(메인/서브)가 공유하는 공용 열거형 모음.
// Main/Sub 구분은 클래스 상속(ABaseNpcCharacter -> AMainNpcCharacter / ASubNpcCharacter)으로 하므로
// 여기엔 별도의 "카테고리" enum을 두지 않는다.

#pragma once

#include "CoreMinimal.h"
#include "NpcTypes.generated.h"

/**
 * NPC의 현재 이동/행동 상태.
 * 애니메이션 블루프린트, BehaviorTree/Blackboard가 이 상태를 참조해서 분기한다.
 * - Idle / Walk    : 모든 NPC 공통 (서브 NPC는 여기까지만 사용)
 * - Follow / Talk  : 메인 NPC 전용 (추적 동행, 대화 중 이동 정지) - AMainNpcCharacter에서만 세팅
 * - Interacting    : 상호작용 모션(문 따기, 책 정리 등) 재생 중
 */
UENUM(BlueprintType)
enum class ENpcMovementState : uint8
{
	Idle			UMETA(DisplayName = "Idle"),
	Walk			UMETA(DisplayName = "Walk"),
	Follow			UMETA(DisplayName = "Follow (Main Only)"),
	Talk			UMETA(DisplayName = "Talk (Main Only)"),
	Interacting		UMETA(DisplayName = "Interacting"),
};

/**
 * 호감도(친밀도) 단계. 실제 수치는 UNpcRelationshipSubsystem이 소유하며,
 * 이 enum은 이벤트 트리의 전제조건("호의 이상", "신뢰 이상" 등)을 코드/블루프린트에서
 * 일관되게 비교하기 위한 표현일 뿐이다.
 * 정의 순서 = 서열이므로 정수 비교(>=)로 "이상/이하" 조건을 그대로 검사할 수 있다.
 */
UENUM(BlueprintType)
enum class ENpcRelationshipLevel : uint8
{
	Wary		UMETA(DisplayName = "경계"),
	Neutral		UMETA(DisplayName = "평범"),
	Favor		UMETA(DisplayName = "호의"),
	Trust		UMETA(DisplayName = "신뢰"),
	Intimate	UMETA(DisplayName = "친밀"),
};

/**
 * 기본 스폰 방식. 실제 스폰/디스폰은 별도 스포너가 담당하고,
 * NPC 쪽에는 참조 정보로만 들고 있는다 (디자이너가 데이터를 한눈에 보기 위함).
 */
UENUM(BlueprintType)
enum class ENpcSpawnMode : uint8
{
	FixedInLevel		UMETA(DisplayName = "Level Fixed Spawn"),
	RandomEncounter		UMETA(DisplayName = "Random Encounter"),
};

/**
 * 메인 NPC의 자율 AI 행동 상태. BehaviorTree 최상위 분기(Selector)가 이 값을 기준으로 갈라진다.
 * ENpcMovementState(애니메이션/스크립트 상태)와는 별개의 축이다:
 *   - ENpcAIState      = "지금 AI가 스스로 뭘 하려고 하는가" (Idle/Patrol/Chase)
 *   - ENpcMovementState = "지금 캐릭터가 어떤 모션을 재생해야 하는가" (Idle/Walk/Talk/...)
 * Talk나 Follow처럼 게임플레이가 직접 개입하는 상황에서는 BehaviorTree 실행 자체를
 * 일시정지(PauseLogic)하므로, 그 사이엔 이 상태값이 그대로 있어도 실제로는 아무것도 하지 않는다.
 */
UENUM(BlueprintType)
enum class ENpcAIState : uint8
{
	Idle	UMETA(DisplayName = "Idle"),
	Patrol	UMETA(DisplayName = "Patrol"),
	Chase	UMETA(DisplayName = "Chase"),
};