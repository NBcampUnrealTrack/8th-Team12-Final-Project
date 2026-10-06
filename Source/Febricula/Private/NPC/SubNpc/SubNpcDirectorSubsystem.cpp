// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
// TActorIterator를 쓰려면 반드시 필요하다. 없으면 "정의되지 않은 class TActorIterator" 에러가 난다.
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameMode/NPCSetting.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcMassProcessors.h"
#include "NPC/SubNpc/NpcCrowdSpawner.h"
#include "NPC/SubNpc/SubNpcCharacter.h"
#include "NPC/SubNpc/SubNpcProfiling.h"
#include "ProfilingDebugging/MiscTrace.h"

// ---------------------------------------------------------------------------
// 측정용 콘솔 변수
//
// Director는 월드 서브시스템이라 디테일 패널이 없다. 그래서 LOD나 회피를
// 에디터에서 껐다 켤 수가 없고, 성능 비교를 하려면 값을 고쳐 다시 빌드해야 했다.
// 재생 중에 한 줄로 토글할 수 있게 콘솔 변수로 뺀다.
//
//   crowd.LOD 0          거리 LOD를 끈다 (전원 최고 상세도) - LOD가 벌어주는 양 측정
//   crowd.Avoidance 0    소프트 회피를 끈다               - 회피 비용 측정
//   crowd.Stats 1        1초마다 활성/풀 인원을 로그로 찍는다
//
// 되돌릴 때는 값을 1로 주면 된다. ECVF_Cheat이라 배포 빌드에는 안 들어간다.
// ---------------------------------------------------------------------------
namespace SubNpcCrowdCVars
{
	static int32 EnableLod = 1;
	static FAutoConsoleVariableRef CVarEnableLod(
		TEXT("crowd.LOD"),
		EnableLod,
		TEXT("서브 NPC 거리 LOD. 0이면 전원을 Near로 고정한다 (측정용)."),
		ECVF_Cheat);

	static int32 EnableAvoidance = 1;
	static FAutoConsoleVariableRef CVarEnableAvoidance(
		TEXT("crowd.Avoidance"),
		EnableAvoidance,
		TEXT("서브 NPC 소프트 회피. 0이면 끈다 (측정용)."),
		ECVF_Cheat);

	static int32 AvoidPairsOverride = -1;
	static FAutoConsoleVariableRef CVarAvoidPairs(
		TEXT("crowd.AvoidPairs"),
		AvoidPairsOverride,
		TEXT("회피 계산에 참여시킬 최대 인원. -1이면 코드 기본값."),
		ECVF_Cheat);

	static float SpawnSpacingOverride = -1.f;
	static FAutoConsoleVariableRef CVarSpawnSpacing(
		TEXT("crowd.SpawnSpacing"),
		SpawnSpacingOverride,
		TEXT("등장 간격(cm) 덮어쓰기. -1이면 코드 기본값, 0이면 간격 없이 등장."),
		ECVF_Cheat);

	static int32 EnableMeshCollision = 0;
	static FAutoConsoleVariableRef CVarMeshCollision(
		TEXT("crowd.MeshCollision"),
		EnableMeshCollision,
		TEXT("서브 NPC 메시의 물리 콜리전. 1이면 켠다 (UpdateKinematicBonesToAnim 비용 비교용)."),
		ECVF_Cheat);

	/**
	 * LOD 밴드 경계의 히스테리시스 폭 (cm).
	 *
	 * 경계에 걸친 개체는 조금만 움직여도 밴드가 오락가락하는데, 밴드가 바뀌면
	 * 강제 LOD·그림자·애니메이션 주기가 전부 다시 적용되어 그 자체로 깜빡여 보인다.
	 * 이 값만큼 "지금 밴드에 머무는 쪽"으로 경계를 밀어서 진동을 없앤다.
	 *
	 * [주의] 클래스 멤버가 아니라 파일 스코프 상수다.
	 * USubNpcDirectorSubsystem에 UPROPERTY를 추가하면 그 뒤의 ActiveNpcs / PooledNpcs
	 * 오프셋이 밀린다. 빌드가 조금이라도 섞이면 배열 포인터를 엉뚱한 자리에서 읽어
	 * 해제 시점에 접근 위반으로 죽는다. 클래스 크기를 건드리지 않는 게 안전하다.
	 */
	constexpr float LodHysteresis = 500.f;

	/**
	 * 앞사람과 이 거리(cm) 안으로 붙으면 속도를 줄인다.
	 *
	 * 좌우로만 밀어내면 앞사람이 느릴 때 뒷사람이 그대로 통과해버린다.
	 * 위와 같은 이유로 클래스 멤버가 아니라 파일 스코프 상수로 둔다.
	 */
	constexpr float FollowSlowDistance = 200.f;

	/** 이 거리(cm) 안으로 붙으면 완전히 멈춘다. 캡슐 지름(약 68cm)보다 커야 겹치지 않는다 */
	constexpr float FollowStopDistance = 85.f;

	/** 차선이 이만큼(cm) 안으로 겹칠 때만 "앞이 막혔다"로 본다 */
	constexpr float FollowLaneWidth = 70.f;

	/** 추월할 때 앞사람과 벌릴 차선 간격 (cm). FollowLaneWidth보다 커야 제동이 풀린다 */
	constexpr float PassLaneGap = 80.f;

	/**
	 * 비켜 가는 동안의 최소 전진 배율. 옆 이동이 전진 거리에 묶여 있으므로
	 * 너무 낮으면 옆으로도 못 빠지고 앞사람 뒤에서 머뭇거린다.
	 */
	constexpr float PassMinScale = 0.55f;

	/** 추월 요청 유지 시간 (초). 회피 갱신 주기보다 길어야 끊기지 않는다 */
	constexpr float PassHoldTime = 0.4f;

	/**
	 * 앞사람 실제 속도가 내 걸음의 이 비율보다 느리면 추월한다.
	 * 0.95면 앞사람 때문에 5%만 느려져도 비켜 간다. 1에 바짝 붙이면 거의 같은 속도끼리도 차선을 바꿔 흔들린다.
	 */
	constexpr float OvertakeSlowerRatio = 0.95f;

	/**
	 * 1이면 앞사람 때문에 속도를 줄인 행인마다, 누구 때문에 줄였는지 선을 긋는다.
	 *   빨강  = 빈 차선이 없어 뒤에서 기다리는 중 (병목)
	 *   노랑  = 비켜서 추월하는 중
	 *   흰색  = 앞사람이 충분히 빨라 그냥 따라가는 중
	 *   하늘  = 같은 일행이라 대열 유지
	 *   자홍  = 오래 막혀서 천천히 스치며 지나가는 중 (정체 해소)
	 * 선 끝이 플레이어면 플레이어가 길을 막고 있는 것이다.
	 */
	/**
	 * 1이면 PIE·게임 중에도 행인 경로를 그린다. 경로 스플라인과 방향 화살표는 에디터 전용이라
	 * 게임 중에는 숨겨져서, 행인이 왜 그쪽으로 걷는지·길 폭이 어디까지인지 볼 수 없었다.
	 *   흰색 = 경로 중심선, 초록 = 걸을 수 있는 폭(LaneHalfWidth), 회색 = 추월할 때 비집을 수 있는 최대 폭
	 *   하늘 화살표 = 진행 방향, 빨간 X = 퇴장구(여기서 사라진다)
	 */
	static int32 DrawPaths = 0;
	static FAutoConsoleVariableRef CVarDrawPaths(
		TEXT("crowd.DrawPaths"),
		DrawPaths,
		TEXT("PIE·게임 중에도 행인 경로를 그린다. 흰색=중심선, 초록=걷는 폭, 회색=추월 최대 폭, 하늘=방향, 빨간 X=퇴장구."),
		ECVF_Cheat);

	/** 1이면 2초마다 정체 요약(감속 이유별 인원, 가장 많이 막는 상대)을 로그로 남긴다. 튜닝 중이라 기본으로 켜 둔다 */
	static int32 LogBlock = 1;
	static FAutoConsoleVariableRef CVarLogBlock(
		TEXT("crowd.LogBlock"),
		LogBlock,
		TEXT("2초마다 정체 요약을 로그로 남긴다 (감속 이유별 인원, 가장 많이 막는 상대). 0이면 끈다."),
		ECVF_Cheat);

	static int32 DebugBlock = 0;
	static FAutoConsoleVariableRef CVarDebugBlock(
		TEXT("crowd.DebugBlock"),
		DebugBlock,
		TEXT("행인이 누구 때문에 느려졌는지 선으로 보여준다. 빨강=막혀서 대기, 노랑=추월 중, 흰색=따라감, 하늘=일행, 자홍=정체라 스치며 통과."),
		ECVF_Cheat);

	/**
	 * 마주 오는 사람을 이 거리(cm) 앞에서부터 알아보고 각자 오른쪽으로 비킨다.
	 * 옆 이동이 전진 거리에 묶여 있으므로, 붙은 뒤에 보면 비킬 틈이 없다.
	 */
	constexpr float HeadOnLookAhead = 400.f;

	/**
	 * 두 사람이 나란히 설 수 있는 최소 차선 간격 (cm). 캡슐 지름(약 68cm)보다 약간 크다.
	 * 이보다 가까운 차선은 "같은 줄"로 보고, 비켜 갈 차선도 이만큼 떨어진 곳을 찾는다.
	 */
	constexpr float LaneClearance = 75.f;

	/**
	 * 비켜 갈 차선이 비었는지 볼 때, 내 바로 뒤(cm)에 있는 사람도 센다.
	 * 옆이나 살짝 뒤에 붙어 걷는 사람 자리로 끼어들면 그 사람 몸을 뚫는다.
	 */
	constexpr float PassBehindWindow = 80.f;

	/**
	 * 병목에서 비집고 지나갈 때의 최소 차선 간격 (cm). 캡슐 지름(약 68cm)을 살짝 넘겨
	 * 어깨가 닿을 듯 말 듯한 정도다. 이보다 좁으면 몸이 겹쳐 보인다.
	 */
	// [튜닝] 64 → 72. 캡슐 지름(약 68cm)보다 작으면 비집는 순간 몸이 겹쳤다. 겹침은 허용하지 않는다.
	constexpr float SqueezeClearance = 72.f;

	/** 비집고 지나갈 때 길 가장자리(LaneHalfWidth)를 넘어가도 되는 거리 (cm) */
	constexpr float SqueezeOverflow = 45.f;

	/** 플레이어와 몸이 닿았다고 보는 여유 (cm). 캡슐 반지름 합에 이만큼 더한 차선 간격 안이면 막힌 것 */
	constexpr float PlayerBodyMargin = 8.f;

	/** 이만큼(초) 거의 멈춰 서 있었으면 비킬 차선을 더 좁게(StallClearance)까지 찾는다 */
	constexpr float StallRelaxTime = 1.5f;

	/** 오래 막혔을 때 허용하는 최소 차선 간격 (cm) */
	// [튜닝] 52 → 70. 오래 막혀도 몸이 겹치는 차선으로는 들어가지 않는다 (겹침 금지).
	constexpr float StallClearance = 70.f;

	/** 이만큼(초) 서 있었는데도 비킬 곳이 없으면 행인끼리는 천천히 스치며 지나간다 */
	// [튜닝] 4 → 2. 4초씩 서 있으면 군중이 아니라 멈춘 것처럼 보였다.
	constexpr float StallGiveUpTime = 2.f;

	/** 그때의 걸음 배율. 빠르게 뚫으면 티가 나므로 아주 천천히 비집는다 */
	constexpr float StallCreepScale = 0.3f;

	/**
	 * 다른 경로의 사람이 내 진행 방향과 이 내적보다 다르게 향하면(약 60도 이상) "가로질러 가는 사람"으로 보고
	 * 바로 앞에서 부딪히기 직전일 때만 피한다. 모서리에서 꺾어 나가는 사람이 여기에 해당한다.
	 */
	constexpr float CrossingHeadingDot = 0.5f;

	/**
	 * 길로 합류할 때 비어 있어야 하는 구간 (cm). 합류 지점 기준으로,
	 * 다가오는 사람이 이만큼 뒤(MergeClearAhead)에 있거나 이만큼 앞(MergeClearBehind)에 있으면 기다린다.
	 */
	constexpr float MergeClearAhead = 220.f;
	constexpr float MergeClearBehind = 90.f;

	/** 몸이 길 가장자리에서 이 거리(cm) 안으로 들어왔으면 이미 길 위로 보고 합류를 기다리지 않는다 */
	constexpr float MergeOnPathTolerance = 25.f;

	/** 길 밖에서 움직이는 사람이 이 거리(cm) 안, 앞쪽에 움직이는 사람이 있으면 잠깐 양보한다 */
	constexpr float OffLaneYieldDistance = 85.f;

	/** "앞쪽"으로 보는 각도 (진행 방향과의 내적). 0.4면 약 66도 안쪽 */
	constexpr float OffLaneYieldFrontDot = 0.4f;

	/** 일행 대화를 굴려보는 간격 (초). 리더마다 이 사이에서 랜덤 */
	// [튜닝] 대화가 잘 안 보인다는 피드백으로 크게 늘렸다. 걸으면서 하므로 길을 막지 않는다.
	constexpr float GroupChatIntervalMin = 2.f;
	constexpr float GroupChatIntervalMax = 5.f;

	/** 굴렸을 때 실제로 이야기를 시작할 확률 */
	constexpr float GroupChatChance = 1.f;

	/** 이야기하는 시간 (초) */
	constexpr float GroupChatDurationMin = 6.f;
	constexpr float GroupChatDurationMax = 10.f;

	/** 리더에게서 이 거리(cm) 안에 있는 일행만 대화에 낀다. 떨어져 있는 사람까지 멈추면 어색하다 */
	constexpr float GroupChatRadius = 300.f;

	/**
	 * 대화 손짓 애니메이션 (NPC 블루프린트 AnimLibrary 키). 상체 슬롯에만 얹으므로
	 * 전신 동작이라도 걸으면서 팔·몸통만 움직인다.
	 */
	static const FName GroupTalkAnimId(TEXT("Group_Talk"));

	/** 한 사람이 말하는(손짓하는) 시간 (초). 이만큼씩 번갈아 말한다 */
	constexpr float GroupChatTurnLength = 2.5f;

	/** 일행이 리더 옆에 서는 거리 (cm, 리더 차선 기준 좌우). 캡슐 지름(약 68cm)보다 작으면 어깨가 겹친다 */
	// [튜닝] 45~55 → 72~80. 캡슐 지름보다 작아서 일행끼리 어깨가 파묻혀 보였다.
	constexpr float GroupSideMin = 72.f;
	constexpr float GroupSideMax = 80.f;

	/** 첫 번째 일행의 앞뒤 간격 (cm). 0이면 리더와 완전히 나란히 걷는다 */
	constexpr float GroupAbreastGapMax = 30.f;

	/** 두 번째 이후 일행의 앞뒤 간격 (cm, 한 사람당) */
	// [튜닝] 50~80 → 80~100. 같은 쪽 앞사람과 앞뒤로 겹치지 않게 캡슐 지름보다 크게 둔다.
	constexpr float GroupBehindGapMin = 80.f;
	constexpr float GroupBehindGapMax = 100.f;

	/** 같은 일행 앞사람이 이 거리(cm) 안이면 몸이 닿는 것으로 보고 살짝 늦춘다. 대열 간격(50~80)보다 작아야 한다 */
	constexpr float GroupBodyTouchDistance = 40.f;

	/** 몸이 닿을 때의 걸음 배율. 세우지 않고 늦추기만 해서 대열이 끊기지 않게 한다 */
	constexpr float GroupTouchSpeedScale = 0.6f;

	/** 일행이 대열 간격보다 이만큼(cm) 더 뒤처지면 리더가 멈춰서 기다린다 */
	constexpr float GroupWaitStartLag = 300.f;

	/** 이만큼(cm) 안으로 따라오면 다시 걷는다. 시작 기준보다 작아야 서다 가다를 반복하지 않는다 */
	constexpr float GroupWaitResumeLag = 100.f;

	/** 이보다 오래(초) 기다렸는데도 안 오면 포기하고 걷는다 (일행이 스팟에 오래 머무는 경우 등) */
	constexpr float GroupWaitMaxTime = 8.f;

	/** 포기한 뒤 이만큼(초)은 다시 기다리지 않는다 */
	constexpr float GroupWaitCooldown = 6.f;

	/**
	 * 기다리는 동안 리더의 걸음 배율. 0이면 길 한가운데 서서 정체를 만든다.
	 * [튜닝] 0.2 → 0.6. 너무 느리면 뒤 인파가 리더 뒤로 몰려 정체가 번졌다. 늦춰 걷는 정도로만 기다린다.
	 */
	constexpr float GroupWaitWalkScale = 0.6f;

	/**
	 * 플레이어를 이 거리(cm) 안에서 인지하고 피한다.
	 *
	 * [왜 따로 필요한가] 회피 계산은 ASubNpcCharacter끼리만 돈다. 플레이어는
	 * 그 목록에 없으므로 행인 입장에서는 **존재하지 않는 것과 같다.**
	 * 게다가 행인 이동은 bSweep=false라 캡슐로 막히지도 않는다 (플레이어만 막힌다).
	 * 그래서 플레이어를 명시적으로 회피 대상에 넣어준다.
	 */
	constexpr float PlayerAvoidDistance = 200.f;

	/** 플레이어를 피해 옆으로 비키는 세기. 행인끼리보다 세게 민다 */
	constexpr float PlayerAvoidStrength = 1.6f;

	/** 이 비율 안쪽으로 정면에서 막히면 속도를 줄인다 */
	constexpr float PlayerBlockRatio = 0.65f;

	/**
	 * 캡슐 반지름 합에 이만큼(cm) 더한 거리 안이면 "몸이 닿았다"로 본다.
	 * 회피 갱신이 0.1초 간격이라, 딱 맞닿는 순간을 놓치지 않게 약간 여유를 둔다.
	 */
	constexpr float LightBumpContactSlack = 12.f;

	static int32 TraceSpawn = 0;
	static FAutoConsoleVariableRef CVarTraceSpawn(
		TEXT("crowd.TraceSpawn"),
		TraceSpawn,
		TEXT("보충이 실패할 때 이유를 로그로 찍는다. 회수 후 재등장이 안 될 때 쓴다."),
		ECVF_Cheat);

	static int32 DebugDraw = 0;
	static FAutoConsoleVariableRef CVarDebugDraw(
		TEXT("crowd.Debug"),
		DebugDraw,
		TEXT("활성 서브 NPC 위에 디버그 표식을 그린다. 메시가 보이든 안 보이든 항상 그린다.\n")
		TEXT("표식은 있는데 사람이 안 보이면 = 렌더링 문제.\n")
		TEXT("표식까지 같이 사라지면 = 풀로 회수된 것(게임플레이 문제)."),
		ECVF_Cheat);

	static int32 PrintStats = 0;
	static FAutoConsoleVariableRef CVarPrintStats(
		TEXT("crowd.Stats"),
		PrintStats,
		TEXT("1이면 1초마다 활성/풀 인원을 로그로 찍는다."),
		ECVF_Cheat);
}

/**
 * 몸 겹침 금지 (최종 안전장치).
 *
 * 차선 계획은 "앞이 막히면 기다리거나 비켜 간다"를 정할 뿐이고, 정체 해소(스치며 통과)·비집기·
 * 일행 대열은 일부러 캡슐 지름(약 68cm)보다 가까이 붙는 걸 허용했다. 오르막처럼 다 같이
 * 느려지는 곳에서 이게 한꺼번에 발동해 사람들이 서로 몸을 뚫고 뭉쳐 보였다.
 *
 * 여기서는 이유를 따지지 않는다. 두 몸이 닿을 거리면 경로상 뒤에 있는 쪽을 세운다.
 * 옆으로 비켜서는 것(추월 차선·밀어내기)은 그대로 허용하므로 비킬 자리가 있으면 비켜서 빠진다.
 * 감속 요청은 최솟값을 따르므로 다른 어떤 요청(스치며 통과 등)보다 이 정지가 우선한다.
 */
/**
 * 행인 액터 ↔ Mass 엔티티 연결 (Mass 1단계).
 *
 * 행인마다 엔티티 하나를 붙인다. 엔티티는 액터를 가리키기만 하고(FSubNpcAgentFragment),
 * 이동 계산은 UMassSubNpcPathFollowProcessor가 맡는다. 풀에 들어간 행인도 엔티티는 그대로 둔다 -
 * 프로세서가 걷는 중인 행인만 고르므로 비용이 없고, 풀에서 꺼낼 때마다 만들었다 지우는 것보다 싸다.
 */
namespace SubNpcMassBinding
{
	static void Bind(UWorld* World, ASubNpcCharacter* Npc)
	{
		if (!World || !IsValid(Npc) || Npc->GetMassEntity().IsSet())
		{
			return;
		}
		UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>();
		if (!MassSubsystem)
		{
			return;
		}
		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		if (EntityManager.IsProcessing())
		{
			return;   // Mass가 도는 중에는 엔티티를 만들 수 없다. 다음 Director 틱에 다시 본다.
		}

		const UScriptStruct* Composition[] = { FSubNpcAgentFragment::StaticStruct() };
		const FMassArchetypeHandle Archetype = EntityManager.CreateArchetype(MakeArrayView(Composition));
		const FMassEntityHandle Entity = EntityManager.CreateEntity(Archetype);
		EntityManager.GetFragmentDataChecked<FSubNpcAgentFragment>(Entity).Actor = Npc;
		Npc->SetMassEntity(Entity);
	}

	static void Unbind(UWorld* World, ASubNpcCharacter* Npc)
	{
		if (!Npc || !Npc->GetMassEntity().IsSet())
		{
			return;
		}
		const FMassEntityHandle Entity = Npc->GetMassEntity();
		Npc->SetMassEntity(FMassEntityHandle());

		// 월드가 정리되는 중이면 Mass도 같이 정리된다. 손대지 않는다.
		if (!World || World->bIsTearingDown)
		{
			return;
		}
		if (UMassEntitySubsystem* MassSubsystem = World->GetSubsystem<UMassEntitySubsystem>())
		{
			FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
			if (EntityManager.IsEntityValid(Entity))
			{
				if (EntityManager.IsProcessing())
				{
					EntityManager.Defer().DestroyEntity(Entity);
				}
				else
				{
					EntityManager.DestroyEntity(Entity);
				}
			}
		}
	}
}

/**
 * 겹침 검사 (crowd.CheckOverlap).
 *
 * 몸 겹침 금지가 제대로 동작하는지 눈 대신 숫자로 확인한다. 인원을 0 → 100으로 늘리면서
 * 가까운(Near/Mid) 행인·플레이어끼리 캡슐이 실제로 파고든 쌍을 세고, 2초마다 로그로 남긴다.
 * 걷는 사람만이 아니라 스팟에 서 있는 사람·반응 중인 사람도 센다 - 어디서 겹치든 화면에서는 똑같이 보인다.
 *
 *   [CrowdOverlap] 활성 60명 | 2초 중 12% 프레임에서 겹침, 최대 동시 2쌍 | 가장 깊은 겹침 18cm: A[...] <-> B[...]
 *
 * crowd.DebugOverlap 1이면 겹친 쌍을 빨간 선으로 그린다.
 */
namespace SubNpcOverlapCheck
{
	static int32 Enabled = 1;
	static FAutoConsoleVariableRef CVarCheckOverlap(
		TEXT("crowd.CheckOverlap"),
		Enabled,
		TEXT("행인끼리(플레이어 포함) 몸이 겹치는지 매 프레임 검사해서 2초마다 [CrowdOverlap] 로그로 남긴다. 0이면 끈다."),
		ECVF_Cheat);

	static int32 DebugDraw = 0;
	static FAutoConsoleVariableRef CVarDebugOverlap(
		TEXT("crowd.DebugOverlap"),
		DebugDraw,
		TEXT("겹친 쌍을 빨간 선으로 그린다."),
		ECVF_Cheat);

	/** 이만큼(cm) 이상 파고들어야 겹침으로 센다. 프레임 사이 오차로 살짝 닿는 건 보이지 않는다 */
	constexpr float Tolerance = 3.f;

	constexpr float LogInterval = 2.f;

	struct FWindow
	{
		float StartTime = -1.f;
		int32 Frames = 0;
		int32 OverlapFrames = 0;
		int32 MaxPairs = 0;
		float WorstDepth = 0.f;
		FString WorstDesc;
	};
	static FWindow Window;

	/** crowd.Bench 단계 요약용 (측정 구간 동안 누적) */
	struct FTotals
	{
		int32 Frames = 0;
		int32 OverlapFrames = 0;
		int32 MaxPairs = 0;
		float WorstDepth = 0.f;
	};
	static FTotals Totals;

	static FString Describe(const ASubNpcCharacter* Npc)
	{
		if (!Npc)
		{
			return TEXT("플레이어");
		}
		const TCHAR* State =
			Npc->IsRejoiningPath() ? TEXT("길로합류중") :
			Npc->IsWalkingOnPath() ? TEXT("걷기") :
			Npc->IsMovingOffLane() ? TEXT("스팟·줄로이동") :
			Npc->GetActivity() == ESubNpcActivity::AtSpot ? TEXT("스팟") :
			Npc->GetActivity() == ESubNpcActivity::Reacting ? TEXT("반응") : TEXT("기타");
		return FString::Printf(TEXT("%s[%s %s%s 차선%.0f 속도%.0f%%%s]"),
			*GetNameSafe(Npc), State, *GetNameSafe(Npc->GetCurrentPath()).Right(10),
			Npc->IsReversedOnPath() ? TEXT("/역") : TEXT(""), Npc->GetLaneOffset(),
			Npc->GetCurrentMoveSpeed() / FMath::Max(1.f, Npc->GetWalkSpeed()) * 100.f,
			Npc->GetGroupLeader() ? TEXT("/일행") : TEXT(""));
	}

	static void Run(const UWorld* World, const TArray<TObjectPtr<ASubNpcCharacter>>& Npcs)
	{
		if (Enabled == 0 || !World)
		{
			return;
		}
		const float Now = World->GetTimeSeconds();
		if (Window.StartTime < 0.f || Now < Window.StartTime)
		{
			Window = FWindow();
			Window.StartTime = Now;
		}

		struct FBody
		{
			const ASubNpcCharacter* Npc = nullptr;
			FVector Loc = FVector::ZeroVector;
			float Radius = 0.f;
		};
		TArray<FBody, TInlineAllocator<128>> Bodies;
		for (const ASubNpcCharacter* Npc : Npcs)
		{
			if (!IsValid(Npc) || Npc->IsPooled() || Npc->IsHidden())
			{
				continue;
			}
			const ESubNpcLodBand Band = Npc->GetLodBand();
			if (Band != ESubNpcLodBand::Near && Band != ESubNpcLodBand::Mid)
			{
				continue;
			}
			Bodies.Add({ Npc, Npc->GetActorLocation(), Npc->GetSimpleCollisionRadius() });
		}
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Player = PC->GetPawn())
			{
				Bodies.Add({ nullptr, Player->GetActorLocation(), Player->GetSimpleCollisionRadius() });
			}
		}

		int32 Pairs = 0;
		for (int32 i = 0; i < Bodies.Num(); ++i)
		{
			for (int32 j = i + 1; j < Bodies.Num(); ++j)
			{
				const FBody& A = Bodies[i];
				const FBody& B = Bodies[j];
				if (FMath::Abs(A.Loc.Z - B.Loc.Z) > 150.f)
				{
					continue;   // 경사로 위·아래, 다른 층
				}
				const float Depth = A.Radius + B.Radius - FVector::Dist2D(A.Loc, B.Loc);
				if (Depth < Tolerance)
				{
					continue;
				}
				++Pairs;
				if (Depth > Window.WorstDepth)
				{
					Window.WorstDepth = Depth;
					Window.WorstDesc = FString::Printf(TEXT("%s <-> %s @(%.0f,%.0f,%.0f)"),
						*Describe(A.Npc), *Describe(B.Npc), A.Loc.X, A.Loc.Y, A.Loc.Z);
				}
				Totals.WorstDepth = FMath::Max(Totals.WorstDepth, Depth);
#if !UE_BUILD_SHIPPING
				if (DebugDraw != 0)
				{
					const FVector Up(0.f, 0.f, 120.f);
					DrawDebugLine(World, A.Loc + Up, B.Loc + Up, FColor::Red, false, 0.f, 0, 4.f);
					DrawDebugSphere(World, (A.Loc + B.Loc) * 0.5f + Up, 15.f, 8, FColor::Red, false, 0.f);
				}
#endif
			}
		}

		++Window.Frames;
		++Totals.Frames;
		if (Pairs > 0)
		{
			++Window.OverlapFrames;
			++Totals.OverlapFrames;
		}
		Window.MaxPairs = FMath::Max(Window.MaxPairs, Pairs);
		Totals.MaxPairs = FMath::Max(Totals.MaxPairs, Pairs);

		if (Now - Window.StartTime >= LogInterval)
		{
			// 높이 검사: 발이 바닥에서 떠 있거나 묻힌 행인 (머리 위로 걷기, 하늘 걷기, 바닥에 꺼짐).
			// 2초에 한 번만 본다 (행인마다 트레이스 한 번).
			int32 BadHeight = 0;
			float WorstGap = 0.f;
			FString WorstHeightDesc;
			for (const FBody& Body : Bodies)
			{
				if (!Body.Npc)
				{
					continue;
				}
				const UCapsuleComponent* Capsule = Body.Npc->GetCapsuleComponent();
				const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
				const FVector FootLoc = Body.Loc - FVector(0.f, 0.f, HalfHeight);

				FCollisionObjectQueryParams Objects;
				Objects.AddObjectTypesToQuery(ECC_WorldStatic);
				Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SubNpcHeightCheck), false, Body.Npc);
				TArray<FHitResult> Hits;
				World->LineTraceMultiByObjectType(Hits, FootLoc + FVector(0.f, 0.f, 60.f), FootLoc - FVector(0.f, 0.f, 500.f), Objects, Params);
				for (const FHitResult& Hit : Hits)
				{
					const UPrimitiveComponent* Comp = Hit.GetComponent();
					if (!Comp || Comp->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block || Cast<APawn>(Hit.GetActor()))
					{
						continue;
					}
					const float Gap = FootLoc.Z - Hit.ImpactPoint.Z;   // +면 떠 있음, -면 묻힘
					if (FMath::Abs(Gap) > 25.f)
					{
						++BadHeight;
						if (FMath::Abs(Gap) > FMath::Abs(WorstGap))
						{
							WorstGap = Gap;
							WorstHeightDesc = FString::Printf(TEXT("%s @(%.0f,%.0f,%.0f)"), *Describe(Body.Npc), Body.Loc.X, Body.Loc.Y, Body.Loc.Z);
						}
					}
					break;
				}
			}
			if (BadHeight > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CrowdHeight] 발이 바닥에서 25cm 넘게 %s 행인 %d명 | 가장 심한 %+.0fcm: %s"),
					WorstGap > 0.f ? TEXT("뜬") : TEXT("묻힌"), BadHeight, WorstGap, *WorstHeightDesc);
			}

			if (Window.OverlapFrames > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CrowdOverlap] 활성 %d명(가까움 %d) | %.1f초 중 %.0f%% 프레임에서 겹침, 최대 동시 %d쌍 | 가장 깊은 겹침 %.0fcm: %s"),
					Npcs.Num(), Bodies.Num(), Now - Window.StartTime,
					100.f * Window.OverlapFrames / FMath::Max(1, Window.Frames), Window.MaxPairs,
					Window.WorstDepth, *Window.WorstDesc);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[CrowdOverlap] 활성 %d명(가까움 %d) | 겹침 없음"), Npcs.Num(), Bodies.Num());
			}
			Window = FWindow();
			Window.StartTime = Now;
		}
	}
}

namespace SubNpcBodySeparation
{
	/** 캡슐 반지름 합에 더하는 여유 (cm) */
	constexpr float Margin = 4.f;

	/** 내 진행 방향 기준 이 내적보다 앞에 있으면 "앞사람"이다 (약 72도 안쪽) */
	constexpr float FrontDot = 0.3f;

	/** 같은 길에서 겹쳤을 때 옆으로 떼어내는 세기 (cm, 회피 밀어내기에 더한다) */
	constexpr float AbreastPush = 30.f;

	static void Enforce(const UWorld* World, const TArray<TObjectPtr<ASubNpcCharacter>>& Npcs)
	{
		struct FBody
		{
			ASubNpcCharacter* Npc = nullptr;   // nullptr이면 플레이어
			FVector Loc = FVector::ZeroVector;
			FVector Fwd = FVector::ZeroVector;
			float Radius = 0.f;
			bool bCanMove = false;
		};

		TArray<FBody, TInlineAllocator<64>> Bodies;
		for (ASubNpcCharacter* Npc : Npcs)
		{
			if (!IsValid(Npc) || !(Npc->IsBlockingOnPath() || Npc->IsMovingOffLane()))
			{
				continue;
			}
			const ESubNpcLodBand Band = Npc->GetLodBand();
			if (Band != ESubNpcLodBand::Near && Band != ESubNpcLodBand::Mid)
			{
				continue;
			}
			FBody& Body = Bodies.AddDefaulted_GetRef();
			Body.Npc = Npc;
			Body.Loc = Npc->GetActorLocation();
			Body.Fwd = Npc->GetActorForwardVector().GetSafeNormal2D();
			Body.Radius = Npc->GetSimpleCollisionRadius();
			Body.bCanMove = Npc->IsWalkingOnPath();
		}

		if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			if (const APawn* Player = PC->GetPawn())
			{
				FBody& Body = Bodies.AddDefaulted_GetRef();
				Body.Loc = Player->GetActorLocation();
				Body.Radius = Player->GetSimpleCollisionRadius();
			}
		}

		for (int32 i = 0; i < Bodies.Num(); ++i)
		{
			const FBody& A = Bodies[i];
			for (int32 j = i + 1; j < Bodies.Num(); ++j)
			{
				const FBody& B = Bodies[j];
				const float MinSep = A.Radius + B.Radius + Margin;

				// 높이 차가 크면(경사로 위·아래) 다른 층이다.
				if (FMath::Abs(A.Loc.Z - B.Loc.Z) > 150.f)
				{
					continue;
				}
				const FVector AB = FVector(B.Loc.X - A.Loc.X, B.Loc.Y - A.Loc.Y, 0.f);
				const float DistSq = AB.SizeSquared();
				if (DistSq >= FMath::Square(MinSep) || DistSq < KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const FVector Dir = AB / FMath::Sqrt(DistSq);

				// 누가 양보할지는 "경로상 누가 뒤에 있나"로 정한다.
				// [버그 수정] 예전에는 몸이 향한 방향으로 앞사람을 정했다. 굽은 곳에서는 서로 몸이 향한 쪽에
				// 다른 사람이 있어 A→B→C→A 순환으로 다 같이 멈췄고, 영영 풀리지 않았다
				// (로그: C 경로 굽은 곳에서 50명 넘게 10~28초 정지, 이유는 "정체스침"인데 속도 0%).
				// 경로 진행 순서는 순환이 없다 - 맨 앞사람은 절대 멈추지 않으니 줄은 반드시 풀린다.
				int32 Order = 0;   // -1: A가 뒤 (A 양보), +1: B가 뒤 (B 양보), 0: 순서를 정할 수 없음
				if (A.Npc && B.Npc && A.bCanMove && B.bCanMove)
				{
					const ANpcCrowdPath* PathA = A.Npc->GetCurrentPath();
					const ANpcCrowdPath* PathB = B.Npc->GetCurrentPath();
					auto FeedsInto = [](const ANpcCrowdPath* From, const ANpcCrowdPath* To)
					{
						for (const FNpcPathExit& Exit : From->GetExits())
						{
							if (Exit.NextPath == To)
							{
								return true;
							}
						}
						return false;
					};
					if (PathA && PathA == PathB && A.Npc->IsReversedOnPath() == B.Npc->IsReversedOnPath())
					{
						const float Along = A.Npc->IsReversedOnPath() ? -1.f : 1.f;
						const float Lead = (B.Npc->GetPathDistance() - A.Npc->GetPathDistance()) * Along;
						Order = (FMath::Abs(Lead) > KINDA_SMALL_NUMBER)
							? (Lead > 0.f ? -1 : 1)
							: ((GetTypeHash(A.Npc->GetFName()) < GetTypeHash(B.Npc->GetFName())) ? -1 : 1);
					}
					else if (PathA && PathB && FeedsInto(PathA, PathB))
					{
						Order = -1;
					}
					else if (PathA && PathB && FeedsInto(PathB, PathA))
					{
						Order = 1;
					}
				}

				// [모서리] 경로 순서상 앞인 사람이 실제로는 뒤쪽 사람 몸을 향해 걸어가고 있으면 앞사람이 선다.
				// B→C 모서리에서 아직 B에 있는 사람(순서상 뒤)이 C 입구 쪽에 서 있으면, 이미 C로 넘어간 사람이
				// 순서만 믿고 그 몸을 뚫고 지나갔다 (로그: C 행인 속도 96~100% ↔ B 행인 속도 0%, 33~65cm 겹침).
				// 이때는 앞사람이 서고 뒤 사람이 먼저 지나간다. 둘 다 서로를 향하면 원래 순서를 따른다 (둘 다 서면 교착).
				if (Order != 0)
				{
					const FBody& Front = (Order < 0) ? B : A;
					const FBody& Rear = (Order < 0) ? A : B;
					const FVector FrontToRear = (Order < 0) ? -Dir : Dir;
					const bool bFrontWalksIntoRear = FVector::DotProduct(Front.Fwd, FrontToRear) > FrontDot;
					const bool bRearWalksIntoFront = FVector::DotProduct(Rear.Fwd, -FrontToRear) > FrontDot;
					if (bFrontWalksIntoRear && !bRearWalksIntoFront)
					{
						Order = -Order;
					}
				}

				if (Order != 0)
				{
					// 일행이 길 폭에 눌려 나란히 겹쳤을 때도 이 규칙으로 뒤쪽이 물러나 한 줄이 된다.
					(Order < 0 ? A.Npc : B.Npc)->RequestSlowDown(0.f);
				}
				else
				{
					// 순서를 정할 수 없는 상대(플레이어, 마주 오는 사람, 스팟으로 가는 사람)는
					// 그쪽을 향해 걷고 있는 쪽이 선다. 마주 오면 둘 다 서서 옆으로 비켜 간다.
					if (A.bCanMove && FVector::DotProduct(A.Fwd, Dir) > FrontDot)
					{
						A.Npc->RequestSlowDown(0.f);
					}
					if (B.bCanMove && FVector::DotProduct(B.Fwd, -Dir) > FrontDot)
					{
						B.Npc->RequestSlowDown(0.f);
					}
				}

				if (A.Npc && B.Npc && A.Npc->GetCurrentPath() == B.Npc->GetCurrentPath() && A.Npc->GetCurrentPath())
				{
					const float Sign = (A.Npc->GetLaneOffset() <= B.Npc->GetLaneOffset()) ? -1.f : 1.f;
					A.Npc->AddAvoidanceNudge(Sign * AbreastPush);
					B.Npc->AddAvoidanceNudge(-Sign * AbreastPush);
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// 성능 측정 (crowd.Bench)
//
// Mass로 옮기기 전후를 같은 방식으로 비교하려고 만든 자동 측정이다.
// 인원을 단계별로 바꿔 가며, 각 단계마다 안정될 때까지 기다린 뒤 일정 시간 동안
//   - 프레임 시간 (평균 / p95 / p99)
//   - 군중 구간별 시간 (SubNpcProfiling의 구간들)
// 을 모아서 요약 CSV와 로그로 남긴다. 단계마다 CSV 프로파일러 캡처도 따로 남기므로
// 게임/렌더/GPU 스레드 시간은 그 파일에서 본다 (Saved/Profiling/CSV).
//
//   crowd.Bench 0 30 60 100 150          인원별로 차례대로 측정
//   crowd.Bench 60 100 secs=30 warmup=8 label=LodOff
//   crowd.BenchStop                      중단 (인원 설정을 원래대로 되돌린다)
//
// [측정할 때] PIE보다 Standalone(-game)이 정확하다. 카메라를 고정하고, stat 명령은 끈다
// (stat 표시 자체가 비용이라 결과를 흐린다).
// ---------------------------------------------------------------------------

CSV_DEFINE_CATEGORY(Crowd, true);

namespace SubNpcProfiling
{
	bool bRecording = false;
	int64 SectionCycles[static_cast<int32>(ESection::Count)] = {};

	const TCHAR* GetSectionName(ESection Section)
	{
		switch (Section)
		{
		case ESection::PathFollow:     return TEXT("PathFollow");
		case ESection::PathApply:      return TEXT("PathApply");
		case ESection::MassPathFollow: return TEXT("MassPathFollow");
		case ESection::GroundSnap:     return TEXT("GroundSnap");
		case ESection::WorldBlock:     return TEXT("WorldBlock");
		case ESection::Avoidance:      return TEXT("Avoidance");
		case ESection::BodySeparation: return TEXT("BodySeparation");
		case ESection::GroupLogic:     return TEXT("GroupLogic");
		case ESection::LodSlice:       return TEXT("LodSlice");
		default:                       return TEXT("?");
		}
	}
}

/** 측정 중 스포너 목표 인원 덮어쓰기. -1이면 스포너 설정대로 (NpcCrowdSpawner.cpp가 읽는다) */
int32 GSubNpcBenchTargetOverride = -1;

namespace SubNpcBench
{
	using SubNpcProfiling::ESection;
	constexpr int32 NumSections = static_cast<int32>(ESection::Count);

	enum class EPhase : uint8 { Idle, Settling, Recording };

	struct FResult
	{
		int32 Requested = 0;
		float AvgActive = 0.f;
		float AvgNear = 0.f;
		int32 Frames = 0;
		float AvgMs = 0.f;
		float P95Ms = 0.f;
		float P99Ms = 0.f;
		float SectionMs[NumSections] = {};
	};

	struct FState
	{
		EPhase Phase = EPhase::Idle;
		TWeakObjectPtr<UWorld> World;
		TArray<int32> Counts;
		int32 Step = -1;
		float PhaseTime = 0.f;
		float LastApplyTime = 0.f;
		float WarmupSeconds = 5.f;
		float RecordSeconds = 20.f;
		FString Label = TEXT("Base");
		FString Stamp;
		int32 SavedMaxActive = -1;

		/** 측정 동안 끈 프레임 제한 (끝나면 되돌린다) */
		FString SavedMaxFPS;
		FString SavedVSync;
		FString SavedVSyncEditor;
		FString SavedSmoothFrameRate;

		TArray<float> FrameMs;
		double ActiveSum = 0.0;
		double NearSum = 0.0;
		TArray<FResult> Results;
	};
	static FState State;

	static float Percentile(TArray<float> Values, float P)
	{
		if (Values.Num() == 0)
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt(P * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	}

	/**
	 * 프레임 제한을 끈다. 첫 측정에서 100명일 때 프레임이 정확히 16.67ms(60 FPS)로 나왔다 -
	 * 수직동기·프레임 제한에 걸리면 여유가 얼마나 남았는지 알 수 없다.
	 */
	static void DisableFrameLimit()
	{
		IConsoleManager& Console = IConsoleManager::Get();
		auto Override = [&Console](const TCHAR* Name, FString& OutSaved, const TCHAR* Value)
		{
			if (IConsoleVariable* Var = Console.FindConsoleVariable(Name))
			{
				OutSaved = Var->GetString();
				Var->Set(Value, ECVF_SetByConsole);
			}
		};
		Override(TEXT("t.MaxFPS"), State.SavedMaxFPS, TEXT("0"));
		Override(TEXT("r.VSync"), State.SavedVSync, TEXT("0"));
		// PIE는 에디터 뷰포트에서 돌아서 r.VSync가 아니라 이 값을 따른다 (두 번째 측정도 60 FPS에 묶였다).
		Override(TEXT("r.VSyncEditor"), State.SavedVSyncEditor, TEXT("0"));
		Override(TEXT("r.SmoothFrameRate"), State.SavedSmoothFrameRate, TEXT("0"));
	}

	static void RestoreFrameLimit()
	{
		IConsoleManager& Console = IConsoleManager::Get();
		auto Restore = [&Console](const TCHAR* Name, const FString& Saved)
		{
			IConsoleVariable* Var = Console.FindConsoleVariable(Name);
			if (Var && !Saved.IsEmpty())
			{
				Var->Set(*Saved, ECVF_SetByConsole);
			}
		};
		Restore(TEXT("t.MaxFPS"), State.SavedMaxFPS);
		Restore(TEXT("r.VSync"), State.SavedVSync);
		Restore(TEXT("r.VSyncEditor"), State.SavedVSyncEditor);
		Restore(TEXT("r.SmoothFrameRate"), State.SavedSmoothFrameRate);
	}

	/** 인원을 N명으로 맞춘다. 풀이 모자라면 늘리고, 넘치면 바로 회수하고, 모자라면 바로 채운다 */
	static void ApplyCount(USubNpcDirectorSubsystem* Director, UWorld* World, int32 N)
	{
		GSubNpcBenchTargetOverride = N;
		Director->SetMaxActiveNpcs(N);

		TArray<ASubNpcCharacter*> Active;
		UClass* NpcClass = nullptr;
		for (TActorIterator<ASubNpcCharacter> It(World); It; ++It)
		{
			NpcClass = NpcClass ? NpcClass : It->GetClass();
			if (!It->IsPooled())
			{
				Active.Add(*It);
			}
		}

		const int32 Have = Director->GetActiveCount() + Director->GetPooledCount();
		if (Have < N && NpcClass)
		{
			Director->PrewarmPool(NpcClass, N - Have);
		}

		for (int32 i = N; i < Active.Num(); ++i)
		{
			Director->ReturnToPool(Active[i]);
		}

		// 모자란 인원은 모든 경로에 길이 비율로 고르게 뿌린다.
		// [변경] 예전에는 스포너의 FillImmediately를 불렀는데, 입구 경로(15m) 한 곳에만 넣어서
		// 겹치지 않게 세우는 규칙에 막혀 100명을 요청해도 48명밖에 못 채웠다.
		TArray<ANpcCrowdPath*> Paths;
		float TotalLength = 0.f;
		for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
		{
			if (It->GetPathLength() >= 200.f)
			{
				Paths.Add(*It);
				TotalLength += It->GetPathLength();
			}
		}
		int32 Deficit = N - Director->GetActiveCount();
		for (int32 Attempt = 0; Deficit > 0 && TotalLength > 0.f && Attempt < Deficit * 4 + 20; ++Attempt)
		{
			float Pick = FMath::FRandRange(0.f, TotalLength);
			ANpcCrowdPath* Path = Paths.Last();
			for (ANpcCrowdPath* Candidate : Paths)
			{
				Pick -= Candidate->GetPathLength();
				if (Pick <= 0.f)
				{
					Path = Candidate;
					break;
				}
			}
			const bool bReversed = Path->IsSpawnReversed();
			if (Director->SpawnOnPath(Path, FMath::FRandRange(0.f, Path->GetPathLength()), bReversed))
			{
				--Deficit;
			}
		}
	}

	static void BeginRecord(int32 N)
	{
		State.FrameMs.Reset();
		State.ActiveSum = 0.0;
		State.NearSum = 0.0;
		FMemory::Memzero(SubNpcProfiling::SectionCycles, sizeof(SubNpcProfiling::SectionCycles));
		SubNpcOverlapCheck::Totals = SubNpcOverlapCheck::FTotals();
		SubNpcProfiling::bRecording = true;

		TRACE_BOOKMARK(TEXT("CrowdBench %s N=%d 시작"), *State.Label, N);
		CSV_EVENT(Crowd, TEXT("Bench N=%d"), N);
#if CSV_PROFILER
		FCsvProfiler::Get()->BeginCapture(-1, FString(),
			FString::Printf(TEXT("CrowdBench_%s_%s_N%03d.csv"), *State.Label, *State.Stamp, N));
#endif
	}

	static void EndRecord(int32 N)
	{
		// 캡처는 여기서 끝낸다. Finish가 다시 끝내지 않게 단계를 먼저 바꿔 둔다.
		State.Phase = EPhase::Settling;
		SubNpcProfiling::bRecording = false;
#if CSV_PROFILER
		FCsvProfiler::Get()->EndCapture();
#endif
		TRACE_BOOKMARK(TEXT("CrowdBench %s N=%d 끝"), *State.Label, N);

		FResult& R = State.Results.AddDefaulted_GetRef();
		R.Requested = N;
		R.Frames = State.FrameMs.Num();
		if (R.Frames > 0)
		{
			float Sum = 0.f;
			for (float Ms : State.FrameMs)
			{
				Sum += Ms;
			}
			R.AvgMs = Sum / R.Frames;
			R.P95Ms = Percentile(State.FrameMs, 0.95f);
			R.P99Ms = Percentile(State.FrameMs, 0.99f);
			R.AvgActive = static_cast<float>(State.ActiveSum / R.Frames);
			R.AvgNear = static_cast<float>(State.NearSum / R.Frames);
			for (int32 s = 0; s < NumSections; ++s)
			{
				R.SectionMs[s] = static_cast<float>(FPlatformTime::ToMilliseconds64(static_cast<uint64>(SubNpcProfiling::SectionCycles[s])) / R.Frames);
			}
		}

		// 에디터 창이 포커스를 잃으면 "백그라운드에서 CPU 덜 쓰기"로 3 FPS(333ms)에 묶인다.
		// 첫 측정의 0명·30명 단계가 이것 때문에 쓸 수 없는 값이 됐다.
		if (R.P95Ms > 250.f)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdBench] N=%d 측정 중 프레임이 3 FPS 근처로 묶였습니다 - 창이 포커스를 잃은 것 같습니다. ")
				TEXT("이 단계 값은 버리고, 측정 중에는 게임 창을 클릭해 둔 채 두세요 ")
				TEXT("(또는 에디터 환경설정 > 일반 > 성능 > '백그라운드에 있을 때 CPU 사용량 줄이기'를 끄세요)."), N);
		}

		FString Sections;
		for (int32 s = 0; s < NumSections; ++s)
		{
			Sections += FString::Printf(TEXT(" %s=%.3f"), SubNpcProfiling::GetSectionName(static_cast<ESection>(s)), R.SectionMs[s]);
		}
		UE_LOG(LogTemp, Log, TEXT("[CrowdBench] %s N=%d (실제 평균 %.0f명, 가까움 %.0f명, %d프레임) | 프레임 평균 %.2fms p95 %.2fms p99 %.2fms (%.0f FPS) | 군중 구간(ms/프레임):%s"),
			*State.Label, N, R.AvgActive, R.AvgNear, R.Frames, R.AvgMs, R.P95Ms, R.P99Ms,
			R.AvgMs > 0.f ? 1000.f / R.AvgMs : 0.f, *Sections);

		const SubNpcOverlapCheck::FTotals& Overlap = SubNpcOverlapCheck::Totals;
		if (SubNpcOverlapCheck::Enabled != 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[CrowdBench] %s N=%d 겹침: %s (겹친 프레임 %.1f%%, 최대 동시 %d쌍, 가장 깊은 %.0fcm)"),
				*State.Label, N, Overlap.OverlapFrames > 0 ? TEXT("있음") : TEXT("없음"),
				100.f * Overlap.OverlapFrames / FMath::Max(1, Overlap.Frames), Overlap.MaxPairs, Overlap.WorstDepth);
		}
	}

	static void Finish(USubNpcDirectorSubsystem* Director, bool bAborted)
	{
		if (State.Phase == EPhase::Recording)
		{
			SubNpcProfiling::bRecording = false;
#if CSV_PROFILER
			FCsvProfiler::Get()->EndCapture();
#endif
		}

		GSubNpcBenchTargetOverride = -1;
		if (Director && State.SavedMaxActive >= 0)
		{
			Director->SetMaxActiveNpcs(State.SavedMaxActive);
		}

		if (State.Results.Num() > 0)
		{
			FString Csv = TEXT("Label,N,AvgActive,AvgNear,Frames,AvgMs,P95Ms,P99Ms,FPS");
			for (int32 s = 0; s < NumSections; ++s)
			{
				Csv += FString::Printf(TEXT(",%sMs"), SubNpcProfiling::GetSectionName(static_cast<ESection>(s)));
			}
			Csv += TEXT("\n");

			UE_LOG(LogTemp, Log, TEXT("[CrowdBench] ===== 요약 (%s) ====="), *State.Label);
			UE_LOG(LogTemp, Log, TEXT("[CrowdBench]    N | 평균ms |  p95ms |  p99ms |   FPS | 군중합계ms(GT) | 경로이동(GT) | 회피 | 겹침금지"));
			for (const FResult& R : State.Results)
			{
				Csv += FString::Printf(TEXT("%s,%d,%.1f,%.1f,%d,%.3f,%.3f,%.3f,%.1f"),
					*State.Label, R.Requested, R.AvgActive, R.AvgNear, R.Frames, R.AvgMs, R.P95Ms, R.P99Ms,
					R.AvgMs > 0.f ? 1000.f / R.AvgMs : 0.f);
				for (int32 s = 0; s < NumSections; ++s)
				{
					Csv += FString::Printf(TEXT(",%.4f"), R.SectionMs[s]);
				}
				Csv += TEXT("\n");

				// "게임 스레드가 실제로 붙잡힌 시간"으로 합친다.
				// Mass를 쓰면 경로 이동은 MassPathFollow(병렬 계산 + 적용의 벽시계 시간)이고,
				// PathFollow는 워커 스레드들의 CPU 시간 합이라 게임 스레드 비용이 아니다.
				// 액터 틱으로 돌면 PathFollow(+ PathApply)가 게임 스레드 비용이다.
				// GroundSnap·WorldBlock은 PathFollow 안에 포함돼 있어 따로 더하지 않는다.
				const float MassMs = R.SectionMs[static_cast<int32>(ESection::MassPathFollow)];
				const float PathGameThreadMs = (MassMs > 0.f)
					? MassMs
					: R.SectionMs[static_cast<int32>(ESection::PathFollow)] + R.SectionMs[static_cast<int32>(ESection::PathApply)];
				const float CrowdTotal = PathGameThreadMs
					+ R.SectionMs[static_cast<int32>(ESection::Avoidance)]
					+ R.SectionMs[static_cast<int32>(ESection::BodySeparation)]
					+ R.SectionMs[static_cast<int32>(ESection::GroupLogic)]
					+ R.SectionMs[static_cast<int32>(ESection::LodSlice)];
				UE_LOG(LogTemp, Log, TEXT("[CrowdBench] %4d | %6.2f | %6.2f | %6.2f | %5.0f | %10.3f | %8.3f | %4.3f | %8.3f"),
					R.Requested, R.AvgMs, R.P95Ms, R.P99Ms, R.AvgMs > 0.f ? 1000.f / R.AvgMs : 0.f, CrowdTotal,
					PathGameThreadMs,
					R.SectionMs[static_cast<int32>(ESection::Avoidance)],
					R.SectionMs[static_cast<int32>(ESection::BodySeparation)]);
			}

			const FString Path = FPaths::ProjectSavedDir() / TEXT("Profiling/CrowdBench")
				/ FString::Printf(TEXT("Summary_%s_%s.csv"), *State.Label, *State.Stamp);
			FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
			UE_LOG(LogTemp, Log, TEXT("[CrowdBench] 요약 파일: %s"), *FPaths::ConvertRelativePathToFull(Path));
			UE_LOG(LogTemp, Log, TEXT("[CrowdBench] 단계별 CSV 캡처(게임/렌더/GPU 스레드 포함): %s"),
				*FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir() / TEXT("CSV")));
		}

		UE_LOG(LogTemp, Log, TEXT("[CrowdBench] %s"), bAborted ? TEXT("중단했습니다.") : TEXT("측정을 마쳤습니다."));
		RestoreFrameLimit();
		State = FState();
	}

	/** Director가 매 프레임 부른다 */
	static void Tick(USubNpcDirectorSubsystem* Director, const TArray<TObjectPtr<ASubNpcCharacter>>& ActiveNpcs)
	{
		if (State.Phase == EPhase::Idle)
		{
			return;
		}
		UWorld* World = State.World.Get();
		if (!World || World != Director->GetWorld())
		{
			return;
		}

		// 실제 프레임 간격 (타임 딜레이션·일시정지와 무관하게 벽시계 기준).
		const float FrameSeconds = static_cast<float>(FApp::GetDeltaTime());
		State.PhaseTime += FrameSeconds;
		const int32 N = State.Counts[State.Step];

		if (State.Phase == EPhase::Settling)
		{
			// 회수가 늦게 끝나거나 풀에서 덜 나왔으면 다시 맞춘다 (스포너는 타이머로만 채운다).
			// 매 프레임 하면 그 자체가 비용이라 0.5초에 한 번만 본다.
			if (Director->GetActiveCount() != N && State.PhaseTime - State.LastApplyTime >= 0.5f)
			{
				State.LastApplyTime = State.PhaseTime;
				ApplyCount(Director, World, N);
			}
			if (State.PhaseTime >= State.WarmupSeconds)
			{
				State.Phase = EPhase::Recording;
				State.PhaseTime = 0.f;
				BeginRecord(N);
			}
			return;
		}

		State.FrameMs.Add(FrameSeconds * 1000.f);
		State.ActiveSum += ActiveNpcs.Num();
		int32 Near = 0;
		for (const ASubNpcCharacter* Npc : ActiveNpcs)
		{
			Near += (IsValid(Npc) && Npc->GetLodBand() == ESubNpcLodBand::Near) ? 1 : 0;
		}
		State.NearSum += Near;

		if (State.PhaseTime >= State.RecordSeconds)
		{
			EndRecord(N);
			if (++State.Step >= State.Counts.Num())
			{
				Finish(Director, false);
				return;
			}
			State.Phase = EPhase::Settling;
			State.PhaseTime = 0.f;
			State.LastApplyTime = 0.f;
			ApplyCount(Director, World, State.Counts[State.Step]);
		}
	}

	static void Start(const TArray<FString>& Args, UWorld* World)
	{
		USubNpcDirectorSubsystem* Director = World ? World->GetSubsystem<USubNpcDirectorSubsystem>() : nullptr;
		if (!Director)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdBench] 게임 월드에서 실행하세요 (PIE 또는 Standalone)."));
			return;
		}
		if (State.Phase != EPhase::Idle)
		{
			UE_LOG(LogTemp, Warning, TEXT("[CrowdBench] 이미 측정 중입니다. crowd.BenchStop으로 멈춘 뒤 다시 실행하세요."));
			return;
		}

		FState NewState;
		for (const FString& Arg : Args)
		{
			FString Key, Value;
			if (Arg.Split(TEXT("="), &Key, &Value))
			{
				if (Key.Equals(TEXT("secs"), ESearchCase::IgnoreCase))
				{
					NewState.RecordSeconds = FMath::Max(1.f, FCString::Atof(*Value));
				}
				else if (Key.Equals(TEXT("warmup"), ESearchCase::IgnoreCase))
				{
					NewState.WarmupSeconds = FMath::Max(0.f, FCString::Atof(*Value));
				}
				else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
				{
					NewState.Label = Value;
				}
			}
			else if (Arg.IsNumeric())
			{
				NewState.Counts.Add(FMath::Max(0, FCString::Atoi(*Arg)));
			}
		}
		if (NewState.Counts.Num() == 0)
		{
			NewState.Counts = { 0, 30, 60, 100 };
		}

		NewState.World = World;
		NewState.Stamp = FDateTime::Now().ToString(TEXT("%m%d_%H%M%S"));
		NewState.SavedMaxActive = Director->GetMaxActiveNpcs();
		NewState.Step = 0;
		NewState.Phase = EPhase::Settling;
		State = MoveTemp(NewState);
		DisableFrameLimit();

		FString CountList;
		for (int32 C : State.Counts)
		{
			CountList += FString::Printf(TEXT(" %d"), C);
		}
		UE_LOG(LogTemp, Log, TEXT("[CrowdBench] 시작 (%s): 인원%s / 단계마다 안정화 %.0f초 + 측정 %.0f초. 카메라를 움직이지 마세요."),
			*State.Label, *CountList, State.WarmupSeconds, State.RecordSeconds);

		ApplyCount(Director, World, State.Counts[0]);
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdBench(
		TEXT("crowd.Bench"),
		TEXT("군중 성능을 인원별로 자동 측정한다. 예) crowd.Bench 0 30 60 100 secs=20 warmup=5 label=Base\n")
		TEXT("결과: 로그 [CrowdBench], Saved/Profiling/CrowdBench/Summary_*.csv, 단계별 CSV 캡처(Saved/Profiling/CSV)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			Start(Args, World);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdCount(
		TEXT("crowd.Count"),
		TEXT("행인 수를 바로 N명으로 맞추고 유지한다 (측정 없이 눈으로 볼 때). crowd.Count -1이면 스포너 설정으로 되돌린다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			USubNpcDirectorSubsystem* Director = World ? World->GetSubsystem<USubNpcDirectorSubsystem>() : nullptr;
			if (!Director || Args.Num() == 0 || State.Phase != EPhase::Idle)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Crowd] crowd.Count <인원> - 게임 중에, crowd.Bench가 돌고 있지 않을 때 쓰세요."));
				return;
			}
			const int32 N = FCString::Atoi(*Args[0]);
			if (N < 0)
			{
				GSubNpcBenchTargetOverride = -1;
				Director->SetMaxActiveNpcs(Director->GetActiveCount() + Director->GetPooledCount());
				UE_LOG(LogTemp, Log, TEXT("[Crowd] 인원 덮어쓰기 해제 - 스포너 설정을 따릅니다."));
				return;
			}
			ApplyCount(Director, World, N);
			UE_LOG(LogTemp, Log, TEXT("[Crowd] 인원을 %d명으로 맞춥니다 (해제: crowd.Count -1)."), N);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBenchStop(
		TEXT("crowd.BenchStop"),
		TEXT("crowd.Bench 측정을 멈추고 인원 설정을 되돌린다. 그때까지 측정한 단계는 요약에 남긴다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (State.Phase == EPhase::Idle)
			{
				return;
			}
			UWorld* BenchWorld = State.World.Get();
			Finish(BenchWorld ? BenchWorld->GetSubsystem<USubNpcDirectorSubsystem>() : nullptr, true);
		}));
}

#if !UE_BUILD_SHIPPING
namespace SubNpcPathDebug
{
	/** crowd.DrawPaths: 레벨의 모든 행인 경로를 이번 프레임에만 그린다 (그리는 방식은 경로 액터가 갖고 있다) */
	static void DrawAllPaths(UWorld* World)
	{
		for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
		{
			It->DrawLaneDebug(SubNpcCrowdCVars::SqueezeOverflow);
		}
	}
}
#endif


void USubNpcDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// crowd.Count로 정한 인원은 전역이라 PIE를 다시 켜도 남는다. 새 판은 스포너 설정으로 시작한다.
	// 미리보기 월드 같은 다른 월드가 생길 때 돌고 있는 측정을 끊지 않도록 게임 월드에서만 한다.
	if (const UWorld* World = GetWorld(); World && World->IsGameWorld() && SubNpcBench::State.Phase == SubNpcBench::EPhase::Idle)
	{
		GSubNpcBenchTargetOverride = -1;
	}

	LodCursor = 0;
	LodTimer = 0.f;
}

void USubNpcDirectorSubsystem::Deinitialize()
{
	// [크래시 대응] 예전에는 여기서 풀과 활성 목록을 순회하며 Destroy()를 불렀다.
	//
	// 그런데 Destroy()는 그 자리에서 EndPlay를 부르고, ASubNpcCharacter::EndPlay는
	// Director->UnregisterNpc(this)를 호출한다. 그 함수가 하는 일이
	// ActiveNpcs.RemoveSingleSwap(this) 다 - 지금 순회 중인 바로 그 배열이다.
	// 순회 중 배열이 줄어들면 유효하지 않은 메모리를 읽게 되고, 인원이 많을수록
	// 반드시 터진다. ("사람이 적으면 안 터진다"의 정체)
	//
	// 애초에 여기서 Destroy를 부를 이유가 없다. 이 시점은 월드가 정리되는 중이고,
	// 월드는 자기 액터를 **전부 스스로 파괴한다.** 우리가 먼저 손대면 순서만 꼬인다.
	// 그래서 참조만 끊고 물러난다. 액터 수명은 월드에 맡긴다.
	//
	// 레벨 전환 후 메모리가 남는 걱정은 없다. 풀 액터는 RF_Transient로 스폰되고
	// PIE/레벨 월드와 함께 사라진다.
	ActiveNpcs.Reset();
	PooledNpcs.Reset();
	ArchetypeTable = nullptr;

	Super::Deinitialize();
}

TStatId USubNpcDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USubNpcDirectorSubsystem, STATGROUP_Tickables);
}

void USubNpcDirectorSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 회수 예약된 개체를 먼저 정리한다.
	// 순회 도중에 활성 배열이 바뀌지 않도록, 실제 회수는 항상 이 시점에만 일어난다.
	ProcessPendingRecycles();

	// 아직 Mass 엔티티가 없는 행인에게 붙인다 (풀에서 막 나왔거나 레벨에 직접 배치된 개체).
	// 이미 붙은 개체는 핸들 확인 한 번으로 끝난다.
	if (UWorld* World = GetWorld())
	{
		for (ASubNpcCharacter* Npc : ActiveNpcs)
		{
			SubNpcMassBinding::Bind(World, Npc);
		}
	}

#if !UE_BUILD_SHIPPING
	if (SubNpcCrowdCVars::DebugDraw != 0)
	{
		DrawDebugMarkers();
	}
	if (SubNpcCrowdCVars::DrawPaths != 0)
	{
		SubNpcPathDebug::DrawAllPaths(GetWorld());
	}
#endif

	if (SubNpcCrowdCVars::PrintStats != 0)
	{
		StatsTimer += DeltaTime;
		if (StatsTimer >= 1.f)
		{
			StatsTimer = 0.f;
			UE_LOG(LogTemp, Log, TEXT("[Crowd] 활성 %d / 풀 %d / 상한 %d"),
				ActiveNpcs.Num(), PooledNpcs.Num(), MaxActiveNpcs);
		}
	}

	if (bEnableAvoidance && SubNpcCrowdCVars::EnableAvoidance != 0)
	{
		// 몸 겹침 금지는 매 프레임 본다. 회피 계산(0.1초 간격)만으로는 그 사이에 파고든다.
		{
			SUBNPC_PROFILE_SCOPE(BodySeparation);
			SubNpcBodySeparation::Enforce(GetWorld(), ActiveNpcs);
		}

		AvoidTimer += DeltaTime;
		if (AvoidTimer >= AvoidUpdateInterval)
		{
			AvoidTimer = 0.f;
			{
				SUBNPC_PROFILE_SCOPE(Avoidance);
				UpdateAvoidance();
			}
			{
				SUBNPC_PROFILE_SCOPE(GroupLogic);
				UpdateGroupChats();
				UpdateGroupCohesion();
				UpdateOffLaneYield();
			}
		}
	}

	CSV_CUSTOM_STAT(Crowd, ActiveNpcs, ActiveNpcs.Num(), ECsvCustomStatOp::Set);
	SubNpcOverlapCheck::Run(GetWorld(), ActiveNpcs);
	SubNpcBench::Tick(this, ActiveNpcs);

	LodTimer += DeltaTime;
	if (LodTimer < LodUpdateInterval)
	{
		return;
	}
	LodTimer = 0.f;

	SUBNPC_PROFILE_SCOPE(LodSlice);
	UpdateLodSlice();
}

// ---------------------------------------------------------------------------
// 풀
// ---------------------------------------------------------------------------

void USubNpcDirectorSubsystem::PrewarmPool(TSubclassOf<ASubNpcCharacter> NpcClass, int32 Count)
{
	UWorld* World = GetWorld();
	if (!World || !NpcClass || Count <= 0)
	{
		return;
	}

	FActorSpawnParameters Params;
	// 스폰 위치가 지형에 겹쳐도 무조건 생성돼야 한다 (어차피 바로 숨길 것이므로).
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	// 화면 밖 아주 먼 곳에서 만든다.
	const FVector Nowhere(0.f, 0.f, -100000.f);

	for (int32 i = 0; i < Count; ++i)
	{
		ASubNpcCharacter* Npc = World->SpawnActor<ASubNpcCharacter>(NpcClass, Nowhere, FRotator::ZeroRotator, Params);
		if (!Npc)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Crowd] 풀 생성 실패 (%d/%d). 클래스 설정을 확인하세요."), i, Count);
			break;
		}

		// [버그 수정] 런타임 SpawnActor는 **그 자리에서 BeginPlay를 호출한다.**
		// ASubNpcCharacter::BeginPlay 는 Director->RegisterNpc(this) 를 부르므로,
		// 방금 만든 이 개체는 이미 ActiveNpcs 안에 들어가 있다.
		//
		// 그대로 PooledNpcs에 넣으면 **같은 액터가 두 배열에 동시에 존재**하게 된다.
		// 그 상태에서 벌어지는 일:
		//   - GetActiveCount()가 처음부터 풀 크기만큼 나온다 (스포너가 "이미 찼다"고 판단)
		//   - UpdateLodSlice가 풀 액터를 순회하며 ApplyLodBand로 다시 보이게 만든다
		//     → Base Target Count가 0이어도 100명이 화면에 나타난다
		//   - 회수·파괴 경로에서 같은 액터가 두 번 처리된다
		//
		// 풀에 넣기 전에 활성 목록에서 확실히 뺀다.
		ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);

		Npc->DeactivateToPool();
		PooledNpcs.AddUnique(Npc);
	}

	UE_LOG(LogTemp, Log, TEXT("[Crowd] 풀 준비 완료: %d명"), PooledNpcs.Num());
}

void USubNpcDirectorSubsystem::SetArchetypeTable(UDataTable* InTable)
{
	// 스포너에 테이블이 없으면 프로젝트 설정의 기본 테이블을 쓴다.
	// 레벨마다 스포너를 일일이 세팅하지 않아도 대사·애니메이션이 한 곳에서 관리된다.
	if (!InTable)
	{
		InTable = GetDefault<UNPCSetting>()->DefaultSubNpcArchetypeTable.LoadSynchronous();
		if (!InTable)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Crowd] 아키타입 테이블이 없습니다. 프로젝트 설정 > NPC Setting > Default Sub Npc Archetype Table을 지정하세요. ")
				TEXT("지정 전까지 행인은 기본값으로 움직이고, 대사가 없어 말 걸기가 비활성화됩니다."));
		}
	}

	ArchetypeTable = InTable;
}

bool USubNpcDirectorSubsystem::PickArchetype(FSubNpcArchetypeRow& OutRow) const
{
	if (!ArchetypeTable)
	{
		// 테이블이 없어도 기본값으로 돌아가야 한다 (세팅 안 됐다고 군중이 통째로 사라지면 곤란).
		OutRow = FSubNpcArchetypeRow();
		return true;
	}

	TArray<float> Weights;
	TArray<const FSubNpcArchetypeRow*> Rows;

	for (const TPair<FName, uint8*>& Pair : ArchetypeTable->GetRowMap())
	{
		const FSubNpcArchetypeRow* Row = reinterpret_cast<const FSubNpcArchetypeRow*>(Pair.Value);
		if (Row && Row->SpawnWeight > 0.f)
		{
			Rows.Add(Row);
			Weights.Add(Row->SpawnWeight);
		}
	}

	const int32 Picked = NpcWeightedPick::PickIndex(Weights);
	if (Picked == INDEX_NONE)
	{
		OutRow = FSubNpcArchetypeRow();
		return true;
	}

	OutRow = *Rows[Picked];
	return true;
}

ASubNpcCharacter* USubNpcDirectorSubsystem::SpawnOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed)
{
	if (!Path)
	{
		return nullptr;
	}

	// 하드 캡. 이 한 줄이 메모리·GPU 예산을 지키는 마지막 방어선이다.
	if (ActiveNpcs.Num() >= MaxActiveNpcs || PooledNpcs.Num() == 0)
	{
		if (SubNpcCrowdCVars::TraceSpawn != 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Crowd] 등장 실패: %s (활성 %d / 상한 %d / 풀 %d)"),
				(PooledNpcs.Num() == 0) ? TEXT("풀이 비었다") : TEXT("정원이 찼다"),
				ActiveNpcs.Num(), MaxActiveNpcs, PooledNpcs.Num());
		}
		return nullptr;
	}

	ASubNpcCharacter* Npc = PooledNpcs.Pop(EAllowShrinking::No);

	// [버그 수정] 예전에는 여기서 그냥 return nullptr 했다. 그러면 방금 꺼낸 개체가
	// 풀에도 활성 목록에도 없는 상태로 **영원히 사라진다.** 죽은 개체가 몇 번 섞이면
	// 풀이 조금씩 말라서 "한참 뒤부터 사람이 안 나온다"가 된다.
	// 유효하지 않은 항목은 버리고 다음 것으로 계속 시도한다.
	while (!IsValid(Npc) && PooledNpcs.Num() > 0)
	{
		Npc = PooledNpcs.Pop(EAllowShrinking::No);
	}

	if (!IsValid(Npc))
	{
		if (SubNpcCrowdCVars::TraceSpawn != 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Crowd] 등장 실패: 풀에 유효한 개체가 없다."));
		}
		return nullptr;
	}

	// [개선] 등장 지점이 이미 붐비면 경로를 따라 조금 밀어서 세운다.
	// 같은 자리에 겹쳐 등장하면 한 덩어리로 몰려다니고, 보기에도 바로 티가 난다.
	const float EffectiveSpacing = (SubNpcCrowdCVars::SpawnSpacingOverride >= 0.f)
		? SubNpcCrowdCVars::SpawnSpacingOverride
		: SpawnSpacing;

	if (EffectiveSpacing > 0.f)
	{
		const float PathLength = Path->GetPathLength();
		const float Step = EffectiveSpacing;
		const float SpacingSq = FMath::Square(EffectiveSpacing);

		// 최대 8칸까지만 밀어본다. 그래도 자리가 없으면 이번 등장은 포기하고 풀에 되돌린다.
		//
		// [변경] 예전에는 자리를 못 찾아도 원래 자리에 그냥 세웠다 (입구가 차서 목표 인원에 못 닿는 걸 피하려고).
		// 그 결과 인원을 한 번에 늘리면 사람 위에 사람이 겹쳐 등장했다 (로그: 100명으로 올린 직후 44쌍 겹침).
		// 겹침은 허용하지 않는다. 자리가 없으면 스포너가 다음 주기에 다시 시도한다 - 입구가 막혀 있으면
		// 인원이 천천히 차는 게 맞는 그림이다.
		bool bFoundSpot = false;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const float Candidate = FMath::Clamp(
				StartDistance + (bReversed ? -1.f : 1.f) * Step * Attempt, 0.f, PathLength);

			const FVector CandidateLoc = Path->GetLocationAtDistance(Candidate, 0.f);

			bool bBlocked = false;
			for (const ASubNpcCharacter* Other : ActiveNpcs)
			{
				if (IsValid(Other) &&
					FVector::DistSquared(Other->GetActorLocation(), CandidateLoc) < SpacingSq)
				{
					bBlocked = true;
					break;
				}
			}

			if (!bBlocked)
			{
				StartDistance = Candidate;
				bFoundSpot = true;
				break;
			}
		}

		if (!bFoundSpot)
		{
			PooledNpcs.Add(Npc);
			if (SubNpcCrowdCVars::TraceSpawn != 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Crowd] 등장 보류: %s 근처에 빈자리가 없다 (겹쳐 세우지 않는다)."), *GetNameSafe(Path));
			}
			return nullptr;
		}
	}

	FSubNpcArchetypeRow Archetype;
	PickArchetype(Archetype);

	Npc->ActivateFromPool(Archetype, Path, StartDistance, bReversed);

	// 위의 빈자리 검사는 경로 중심선으로 봤다. 실제로 선 자리(차선)에서 누군가와 몸이 닿으면 되돌린다.
	{
		const FVector MyLoc = Npc->GetActorLocation();
		const float MyRadius = Npc->GetSimpleCollisionRadius();
		for (const ASubNpcCharacter* Other : ActiveNpcs)
		{
			if (IsValid(Other) && !Other->IsPooled()
				&& FMath::Abs(Other->GetActorLocation().Z - MyLoc.Z) < 150.f
				&& FVector::DistSquared2D(Other->GetActorLocation(), MyLoc) < FMath::Square(MyRadius + Other->GetSimpleCollisionRadius() + 4.f))
			{
				Npc->DeactivateToPool();
				PooledNpcs.Add(Npc);
				return nullptr;
			}
		}
	}

	ActiveNpcs.Add(Npc);

	return Npc;
}

ASubNpcCharacter* USubNpcDirectorSubsystem::SpawnAtNearestPath(const FVector& WorldLocation, float SearchRadius)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 레벨의 모든 경로 중 이 좌표에 가장 가까운 지점을 찾는다.
	// 경로 개수는 보통 수십 개 수준이라 매번 훑어도 문제없다
	// (매 프레임 부르는 함수가 아니라 스폰 시점에만 부르는 함수다).
	ANpcCrowdPath* BestPath = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	float BestDistanceAlong = 0.f;

	for (TActorIterator<ANpcCrowdPath> It(World); It; ++It)
	{
		float DistanceAlong = 0.f;
		const float DistSq = It->FindClosestPointOnPath(WorldLocation, DistanceAlong);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestDistanceAlong = DistanceAlong;
			BestPath = *It;
		}
	}

	if (!BestPath)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Crowd] 레벨에 NpcCrowdPath가 하나도 없어 스폰할 수 없습니다."));
		return nullptr;
	}

	if (SearchRadius > 0.f && BestDistSq > FMath::Square(SearchRadius))
	{
		// 지정한 위치에서 너무 먼 곳에 세우면 "엉뚱한 데서 튀어나온다"가 된다. 차라리 실패시킨다.
		UE_LOG(LogTemp, Verbose,
			TEXT("[Crowd] 지정 위치 %s 주변 %.0fcm 안에 경로가 없습니다."), *WorldLocation.ToString(), SearchRadius);
		return nullptr;
	}

	return SpawnOnPath(BestPath, BestDistanceAlong, /*bReversed=*/false);
}

TArray<ASubNpcCharacter*> USubNpcDirectorSubsystem::SpawnGroupOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed, int32 GroupSize)
{
	TArray<ASubNpcCharacter*> Group;
	if (!Path || GroupSize <= 0)
	{
		return Group;
	}

	ASubNpcCharacter* Leader = SpawnOnPath(Path, StartDistance, bReversed);
	if (!Leader)
	{
		return Group;
	}
	Group.Add(Leader);


	const float Length = FMath::Max(1.f, Path->GetPathLength());

	for (int32 i = 1; i < GroupSize; ++i)
	{
		// [개선] 예전에는 전원을 리더와 **똑같은 지점**에 세우고 뒤에서 간격만 벌렸다.
		// 그러면 등장하는 순간 한 점에 겹쳐 있다가 서서히 풀리는 게 눈에 보인다.
		// 처음부터 조금씩 뒤에 세운다.
		const float MemberStart = FMath::Clamp(
			StartDistance + (bReversed ? 1.f : -1.f) * FMath::FRandRange(60.f, 140.f) * i,
			0.f, Length);

		ASubNpcCharacter* Member = SpawnOnPath(Path, MemberStart, bReversed);
		if (!Member)
		{
			break;
		}

		// [그룹핑] 리더 옆에 붙어 걷는다. 좌우 위치는 리더 기준 상대값이다 (SetGroupLeader).
		// 첫 번째 일행은 거의 나란히, 두 번째 일행은 반대쪽 살짝 뒤. 완전히 일직선이면 행진처럼 보인다.
		// 예전 값(옆 50~90, 앞뒤 60~140 x 순번)은 너무 벌어져서 일행으로 안 보였다.
		const float Side = (i % 2 == 1) ? -1.f : 1.f;
		const float SideOffset = Side * FMath::FRandRange(
			SubNpcCrowdCVars::GroupSideMin, SubNpcCrowdCVars::GroupSideMax);
		const float Gap = (i == 1)
			? FMath::FRandRange(0.f, SubNpcCrowdCVars::GroupAbreastGapMax)
			: FMath::FRandRange(SubNpcCrowdCVars::GroupBehindGapMin, SubNpcCrowdCVars::GroupBehindGapMax) * (i - 1);

		Member->SetGroupLeader(Leader, Gap, SideOffset);
		Group.Add(Member);
	}

	// 일행이 생겼으면 리더로 표시한다. 리더는 스팟에 들르지 않는다 (RollForActivitySpot).
	Leader->SetHasGroupFollowers(Group.Num() > 1);

	return Group;
}

void USubNpcDirectorSubsystem::ReturnToPool(ASubNpcCharacter* Npc)
{
	if (!IsValid(Npc))
	{
		return;
	}

	ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);

	Npc->DeactivateToPool();

	// 중복 반납 방지. 같은 개체가 풀에 두 번 들어가면 두 명이 동시에 활성화된다.
	PooledNpcs.AddUnique(Npc);
}

void USubNpcDirectorSubsystem::RegisterNpc(ASubNpcCharacter* Npc)
{
	if (!IsValid(Npc) || Npc->IsPooled())
	{
		return;
	}

	// [방어] 이미 풀에 들어있는 개체는 활성으로 올리지 않는다.
	// IsPooled() 검사만으로는 부족하다 - PrewarmPool은 BeginPlay(여기) 다음에
	// DeactivateToPool을 부르므로, 그 시점에는 아직 Pooled 상태가 아니다.
	if (PooledNpcs.Contains(Npc))
	{
		return;
	}

	ActiveNpcs.AddUnique(Npc);
}

void USubNpcDirectorSubsystem::UnregisterNpc(ASubNpcCharacter* Npc)
{
	SubNpcMassBinding::Unbind(GetWorld(), Npc);

	ActiveNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);
	PooledNpcs.RemoveSingleSwap(Npc, EAllowShrinking::No);
}

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

bool USubNpcDirectorSubsystem::GetViewerLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 카메라가 아니라 폰 위치를 기준으로 삼는다. 3인칭에서 카메라가 뒤로 빠져 있어도
	// "플레이어 주변"이 기준이어야 군중 밀도가 자연스럽다.
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn())
		{
			OutLocation = Pawn->GetActorLocation();
			return true;
		}
	}
	return false;
}

#if !UE_BUILD_SHIPPING
void USubNpcDirectorSubsystem::DrawDebugMarkers() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 이 표식은 액터 위치에 직접 그린다. 스켈레탈 메시가 숨겨져 있어도,
	// LOD가 최저여도, 애니메이션이 멈춰 있어도 상관없이 그려진다.
	// 그래서 "사람이 안 보이는" 원인이 렌더링인지 회수인지 눈으로 가를 수 있다.
	for (const TObjectPtr<ASubNpcCharacter>& Npc : ActiveNpcs)
	{
		if (!IsValid(Npc))
		{
			continue;
		}

		FColor Color = FColor::White;
		const TCHAR* BandName = TEXT("?");
		switch (Npc->GetLodBand())
		{
		case ESubNpcLodBand::Near:		Color = FColor::Green;	BandName = TEXT("N"); break;
		case ESubNpcLodBand::Mid:		Color = FColor::Yellow;	BandName = TEXT("M"); break;
		case ESubNpcLodBand::Far:		Color = FColor::Orange;	BandName = TEXT("F"); break;
		case ESubNpcLodBand::Culled:	Color = FColor::Red;	BandName = TEXT("C"); break;
		default: break;
		}

		const FVector Loc = Npc->GetActorLocation();

		// 발밑 원 - 위치가 어디인지
		DrawDebugCircle(World, Loc - FVector(0, 0, 88.f), 34.f, 12, Color,
			/*bPersistentLines=*/false, /*LifeTime=*/-1.f, 0, 2.f,
			FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);

		// 머리 위 글자 - 지금 어느 LOD 밴드인지
		DrawDebugString(World, Loc + FVector(0, 0, 110.f), BandName, nullptr, Color, 0.f, true, 1.2f);
	}
}
#endif

void USubNpcDirectorSubsystem::ProcessPendingRecycles()
{
	// 뒤에서부터 순회한다. RemoveSingleSwap이 뒤 원소를 앞으로 당기므로
	// 앞에서부터 돌면 방금 당겨온 원소를 건너뛴다.
	for (int32 i = ActiveNpcs.Num() - 1; i >= 0; --i)
	{
		ASubNpcCharacter* Npc = ActiveNpcs[i];
		if (!IsValid(Npc))
		{
			ActiveNpcs.RemoveAtSwap(i, 1, EAllowShrinking::No);
			continue;
		}

		if (Npc->IsPendingRecycle())
		{
			Npc->ClearPendingRecycle();
			ReturnToPool(Npc);
		}
	}
}

void USubNpcDirectorSubsystem::UpdateAvoidance()
{
	FVector ViewerLocation;
	if (!GetViewerLocation(ViewerLocation))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// [측정 근거] stat game에서 Director 전체(TickableGameObjects Time)의
	// 0명 -> 90명 델타가 0.06 ms에 불과했다. 예전 기본값 24는 근거 없이 보수적이었고,
	// 그 탓에 90명 중 24명만 회피에 참여해 나머지가 그대로 겹쳐 보였다.
	const int32 PairBudget = (SubNpcCrowdCVars::AvoidPairsOverride >= 0)
		? SubNpcCrowdCVars::AvoidPairsOverride
		: MaxAvoidancePairs;

	// 가까이 보이는 개체만 모은다. 여기가 회피 비용을 결정한다.
	TArray<ASubNpcCharacter*> Nearby;
	Nearby.Reserve(PairBudget);

	for (ASubNpcCharacter* Npc : ActiveNpcs)
	{
		// [버그 수정] 예전에는 걷는 사람만 모았다. 플레이어에게 부딪혀 멈춘(Reacting)
		// 앞사람이 목록에서 빠지니, 뒷사람은 제동을 못 걸고 그대로 뚫고 지나갔다.
		// 멈춘 사람도 장애물로는 넣고, 감속·밀어내기는 걷는 사람에게만 건다.
		// [추가] 스팟으로 가거나 줄 자리로 옮기는 사람(길 밖으로 움직이는 사람)도 장애물로 넣는다.
		// 이들은 길을 가로질러 가는데, 예전에는 목록에서 빠져 있어서 길 위 행인이 못 보고 엉켰다.
		if (!IsValid(Npc) || !(Npc->IsBlockingOnPath() || Npc->IsMovingOffLane()))
		{
			continue;
		}

		const ESubNpcLodBand Band = Npc->GetLodBand();
		if (Band != ESubNpcLodBand::Near && Band != ESubNpcLodBand::Mid)
		{
			continue;
		}

		Nearby.Add(Npc);
		if (Nearby.Num() >= PairBudget)
		{
			break;
		}
	}

	// ---- 플레이어 접촉 ---------------------------------------------------------
	//
	// 행인은 스윕 없이 움직이므로 서 있는 플레이어에게 걸어 들어가도 NotifyHit이 안 불린다.
	// 그래서 몸이 닿았는지 여기서 거리로 직접 본다. 비켜 가는 건 아래 차선 계획이 맡는다.
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const float PlayerRadius = PlayerPawn ? PlayerPawn->GetSimpleCollisionRadius() : 0.f;

	if (PlayerPawn)
	{
		const FVector PlayerLoc = PlayerPawn->GetActorLocation();
		for (ASubNpcCharacter* Npc : Nearby)
		{
			if (!Npc->IsWalkingOnPath())
			{
				continue;
			}

			const float ContactDist = Npc->GetSimpleCollisionRadius() + PlayerRadius
				+ SubNpcCrowdCVars::LightBumpContactSlack;
			if (FVector::DistSquared2D(PlayerLoc, Npc->GetActorLocation()) >= FMath::Square(ContactDist))
			{
				continue;
			}

			// 누가 부딪혔는지 가른다. 플레이어가 밀고 들어온 경우만 반응하고,
			// 행인이 걸어와 닿은 거면 반응 없이 비켜 지나간다 (아래 차선 계획).
			if (ASubNpcCharacter::GetPlayerClosingSpeed(PlayerPawn, Npc) >= ASubNpcCharacter::PlayerBumpMinClosingSpeed)
			{
				Npc->ReactToPlayerBump(PlayerPawn, ASubNpcCharacter::GetPlayerBumpSpeed(PlayerPawn));
			}
		}
	}

	// ---- 차선 계획 (앞이 막혔을 때 기다릴지, 어느 쪽으로 비켜 갈지) --------------
	//
	// [재설계] 예전에는 행인 두 명씩 짝을 지어 판정했다. 병목에서 세 가지가 깨졌다.
	//   1) 추월할 차선이 비어 있는지 보지 않았다. 옆에 다른 사람이나 플레이어가 있어도
	//      그쪽으로 들어갔고, 추월 중인 쌍은 밀어내기도 꺼져 있어서 그대로 뚫고 지나갔다.
	//   2) 앞사람 여럿과 짝마다 서로 다른 추월 목표를 덮어써서 좌우로 왔다 갔다 했다.
	//   3) 플레이어를 피하는 방향을 "내 몸의 오른쪽" 기준으로 정해서, 비켜 가며 몸이 돌면
	//      방향이 뒤집혀 다시 반대로 갔다 (와리가리).
	//
	// 이제 행인마다 한 번만 결정한다. 같은 경로 위의 사람(+플레이어)을 차선 좌표로 모으고,
	// 내 앞을 막는 가장 가까운 상대를 찾은 뒤, 실제로 비어 있는 차선이 있을 때만 그쪽으로
	// 비켜 간다. 빈 차선이 없으면 뒤에서 줄 서서 기다린다. 방향은 경로 기준이라 몸이 돌아도
	// 뒤집히지 않고, 한번 고른 쪽은 끝까지 유지한다.
	struct FLaneAgent
	{
		ASubNpcCharacter* Npc = nullptr;   // nullptr이면 플레이어
		float Dist = 0.f;                  // 경로 진행 거리
		float Lane = 0.f;                  // 경로 오른쪽 기준 좌우 오프셋
		float Along = 1.f;                 // 진행 부호 (+1 정방향, -1 역방향, 0 = 서 있는 플레이어)
		float Speed = 0.f;                 // 실제 걷는 속도
		bool bWalking = false;
		bool bForeign = false;             // 다른 경로 위의 사람을 이 경로 좌표로 옮겨 넣은 것 (장애물로만 쓴다)
		bool bMerging = false;             // 스팟·줄에서 나와 길로 합류하는 중 (실제 위치로 넣는다)
		bool bUpstream = false;            // (bForeign) 이 경로로 들어오기 전 경로 위의 사람. 모서리에서는 이 사람이 양보한다
		float TargetLane = 0.f;            // 합류할 차선
		FVector Loc = FVector::ZeroVector; // 실제 위치. 모서리에서 앞뒤 간격을 잴 때 쓴다
		FVector Fwd = FVector::ZeroVector; // 실제로 향한 방향 (수평). 가로질러 가는 사람을 가릴 때 쓴다
	};

	TMap<ANpcCrowdPath*, TArray<FLaneAgent>> AgentsByPath;
	for (ASubNpcCharacter* Npc : Nearby)
	{
		ANpcCrowdPath* Path = Npc->GetCurrentPath();
		if (!Path)
		{
			continue;
		}

		FLaneAgent& Agent = AgentsByPath.FindOrAdd(Path).AddDefaulted_GetRef();
		Agent.Npc = Npc;
		Agent.Loc = Npc->GetActorLocation();
		Agent.Fwd = Npc->GetActorForwardVector().GetSafeNormal2D();
		Agent.Dist = Npc->GetPathDistance();
		Agent.Lane = Npc->GetLaneOffset();
		Agent.Along = Npc->IsReversedOnPath() ? -1.f : 1.f;
		Agent.Speed = Npc->IsWalkingOnPath() ? Npc->GetCurrentMoveSpeed() : 0.f;
		Agent.bWalking = Npc->IsWalkingOnPath();

		// [버그 수정] 길로 돌아오는 사람은 몸이 아직 길 밖(또는 길가)에 있다.
		// 예전에는 목표 차선 값으로 넣어서, 실제로는 3m 떨어져 걷는 사람을 길 위의 장애물로
		// 보고 뒷사람들이 섰다. 실제 위치를 경로 좌표로 옮겨 넣는다.
		// 스팟으로 가거나 줄 자리로 옮기는 사람: 실제 위치를 경로 좌표로 옮겨 장애물로만 쓴다.
		// 이 사람들의 움직임은 스팟 쪽 로직이 정하므로 차선 계획을 하지 않는다 (bForeign).
		if (Npc->IsMovingOffLane() && !Npc->IsRejoiningPath())
		{
			const FVector Loc = Npc->GetActorLocation();
			float Dist = Agent.Dist;
			Path->FindClosestPointOnPath(Loc, Dist);
			const FVector Base = Path->GetLocationAtDistance(Dist, 0.f);
			const FVector Right = (Path->GetLocationAtDistance(Dist, 1.f) - Base).GetSafeNormal2D();
			Agent.Dist = Dist;
			Agent.Lane = FVector::DotProduct(Loc - Base, Right);
			Agent.bForeign = true;
			Agent.bWalking = true;
			Agent.Speed = Npc->GetWalkSpeed();
		}

		if (Npc->IsRejoiningPath())
		{
			const FVector Loc = Npc->GetActorLocation();
			float Dist = Agent.Dist;
			Path->FindClosestPointOnPath(Loc, Dist);
			const FVector Base = Path->GetLocationAtDistance(Dist, 0.f);
			const FVector Right = (Path->GetLocationAtDistance(Dist, 1.f) - Base).GetSafeNormal2D();
			Agent.Dist = Dist;
			Agent.Lane = FVector::DotProduct(Loc - Base, Right);
			Agent.bMerging = true;
			Agent.TargetLane = Npc->GetLaneOffset();
		}
	}

	// [정체 해소] 경로 이음매 근처에서는 다른 경로 위의 사람도 이 경로의 차선 좌표로 옮겨 넣는다.
	// 예전에는 같은 경로끼리만 계획해서, 앞사람이 다음 경로로 넘어가 있으면 뒷사람은 비켜 갈
	// 생각 없이 속도만 줄였다(파란 선). 측정해 보니 정체가 거의 다 이음매에 몰려 있었다.
	// 옮겨 넣은 사람은 장애물로만 쓰고, 그 사람의 계획은 원래 경로에서 한다.
	{
		TArray<ANpcCrowdPath*> Paths;
		AgentsByPath.GetKeys(Paths);
		for (ANpcCrowdPath* Path : Paths)
		{
			const float Reach = Path->GetLaneHalfWidth() + SubNpcCrowdCVars::LaneClearance;
			for (ASubNpcCharacter* Npc : Nearby)
			{
				if (Npc->GetCurrentPath() == Path)
				{
					continue;
				}

				const FVector Loc = Npc->GetActorLocation();
				float Dist = 0.f;
				if (Path->FindClosestPointOnPath(Loc, Dist) > FMath::Square(Reach))
				{
					continue;
				}

				const FVector Base = Path->GetLocationAtDistance(Dist, 0.f);
				const FVector Right = (Path->GetLocationAtDistance(Dist, 1.f) - Base).GetSafeNormal2D();
				const FVector PathDir = Path->GetDirectionAtDistance(Dist);

				// [버그 수정] 경로 끝점 바깥에 있는 사람을 끝점에 붙여 계산하지 않는다.
				// 가장 가까운 점이 시작점·끝점에 걸리면 그 사람은 경로 바깥(이음매 너머)에 있다.
				// 예전에는 끝점 위치로 붙여서, B에서 C로 꺾어 나가 멀어지는 사람이 B 기준으로는
				// 계속 "B 끝점 바로 앞에 서 있는 사람"이 되어 B를 걷는 사람들이 이음매 앞에서 멈췄다.
				// 끝점 너머로 경로를 곧게 연장한 좌표로 계산하면, 옆으로 꺾어 나간 사람은
				// 곧 길 폭을 벗어나 장애물 계산에서 빠진다.
				const float PathLength = Path->GetPathLength();
				const bool bBeyondEnd = Dist >= PathLength - 1.f;
				const bool bBeforeStart = Dist <= 1.f;
				float AlongDist = Dist;
				if (bBeyondEnd || bBeforeStart)
				{
					AlongDist += FVector::DotProduct(Loc - Base, PathDir.GetSafeNormal2D());
				}
				const float Lane = FVector::DotProduct(Loc - Base, Right);
				if ((bBeyondEnd || bBeforeStart) && FMath::Abs(Lane) > Path->GetLaneHalfWidth() + SubNpcCrowdCVars::LaneClearance)
				{
					continue;
				}

				FLaneAgent& Agent = AgentsByPath.FindChecked(Path).AddDefaulted_GetRef();
				Agent.Npc = Npc;
				Agent.Loc = Loc;
				Agent.Fwd = Npc->GetActorForwardVector().GetSafeNormal2D();
				Agent.Dist = AlongDist;
				Agent.Lane = Lane;
				Agent.Along = (FVector::DotProduct(Npc->GetActorForwardVector(), PathDir) >= 0.f) ? 1.f : -1.f;
				Agent.Speed = Npc->IsWalkingOnPath() ? Npc->GetCurrentMoveSpeed() : 0.f;
				Agent.bWalking = Npc->IsWalkingOnPath();
				Agent.bForeign = true;
				if (const ANpcCrowdPath* OtherPath = Npc->GetCurrentPath())
				{
					for (const FNpcPathExit& Exit : OtherPath->GetExits())
					{
						if (Exit.NextPath == Path)
						{
							Agent.bUpstream = true;
							break;
						}
					}
				}
			}
		}
	}

	// 플레이어를 각 경로의 차선 좌표로 옮겨 넣는다. 길 위(차선 폭 + 여유) 안에 있을 때만.
	if (PlayerPawn)
	{
		const FVector PlayerLoc = PlayerPawn->GetActorLocation();
		for (TPair<ANpcCrowdPath*, TArray<FLaneAgent>>& Pair : AgentsByPath)
		{
			ANpcCrowdPath* Path = Pair.Key;
			float PlayerDist = 0.f;
			const float DistSq = Path->FindClosestPointOnPath(PlayerLoc, PlayerDist);
			const float Reach = Path->GetLaneHalfWidth() + PlayerRadius + SubNpcCrowdCVars::LaneClearance;
			if (DistSq > FMath::Square(Reach))
			{
				continue;
			}

			const FVector Base = Path->GetLocationAtDistance(PlayerDist, 0.f);
			const FVector Right = (Path->GetLocationAtDistance(PlayerDist, 1.f) - Base).GetSafeNormal2D();

			// 경로 끝점 바깥이면 끝점에 붙이지 않고 연장선 좌표로 계산한다 (위의 다른 경로 사람과 같은 이유).
			float AlongDist = PlayerDist;
			if (PlayerDist <= 1.f || PlayerDist >= Path->GetPathLength() - 1.f)
			{
				AlongDist += FVector::DotProduct(PlayerLoc - Base, Path->GetDirectionAtDistance(PlayerDist).GetSafeNormal2D());
			}

			FLaneAgent& Agent = Pair.Value.AddDefaulted_GetRef();
			Agent.Npc = nullptr;
			Agent.Loc = PlayerLoc;
			Agent.Dist = AlongDist;
			Agent.Lane = FVector::DotProduct(PlayerLoc - Base, Right);
			Agent.Along = 0.f;
			Agent.Speed = PlayerPawn->GetVelocity().Size2D();
			Agent.bWalking = false;
		}
	}

	const float LaneClear = SubNpcCrowdCVars::LaneClearance;
	const float PassUntil = Now + SubNpcCrowdCVars::PassHoldTime;

	// crowd.LogBlock용 집계: 감속 이유별 인원, 남을 가장 많이 막은 상대.
	TMap<FString, int32> ReasonCounts;
	TMap<const AActor*, int32> BlockerCounts;
	static TArray<FString> StallChainLog;   // 2초 로그 사이에 모은 "누가 누구 때문에 오래 서 있나"

	// crowd.DebugBlock 1: 누가 누구 때문에 느려졌는지 머리 위에 선을 긋는다. 다음 갱신까지만 보인다.
	auto DrawBlockDebug = [&](const ASubNpcCharacter* From, const ASubNpcCharacter* To, const APawn* Player, const FColor& Color)
	{
#if !UE_BUILD_SHIPPING
		if (SubNpcCrowdCVars::LogBlock != 0)
		{
			const TCHAR* Reason =
				(Color == FColor::Red) ? TEXT("대기(빈차선없음)") :
				(Color == FColor::Yellow) ? TEXT("추월중") :
				(Color == FColor::White) ? TEXT("따라감") :
				(Color == FColor::Cyan) ? TEXT("일행대열") :
				(Color == FColor::Magenta) ? TEXT("정체스침") :
				(Color == FColor::Orange) ? TEXT("합류대기") : TEXT("기타");
			ReasonCounts.FindOrAdd(Reason)++;
			if (const AActor* Target = To ? static_cast<const AActor*>(To) : static_cast<const AActor*>(Player))
			{
				BlockerCounts.FindOrAdd(Target)++;
			}

			// 오래 서 있는 사람이 누구 때문에 서 있는지 한 줄씩 남긴다 (경로·방향·차선 포함).
			// 모서리 정체가 마주 오는 사람 때문인지, 다른 경로 사람 때문인지, 같은 줄 앞사람 때문인지 가린다.
			if (From && From->GetStalledTime() > 1.5f && StallChainLog.Num() < 8)
			{
				auto Describe = [](const ASubNpcCharacter* Npc)
				{
					return FString::Printf(TEXT("%s[%s%s 차선%.0f 속도%.0f%%%s%s] @(%.0f,%.0f)"),
						*GetNameSafe(Npc), *GetNameSafe(Npc->GetCurrentPath()),
						Npc->IsReversedOnPath() ? TEXT("/역방향") : TEXT(""),
						Npc->GetLaneOffset(),
						Npc->GetCurrentMoveSpeed() / FMath::Max(1.f, Npc->GetWalkSpeed()) * 100.f,
						Npc->IsWalkingOnPath() ? TEXT("") : TEXT("/길밖"),
						Npc->GetStalledTime() > 0.5f ? *FString::Printf(TEXT("/정지%.1f초"), Npc->GetStalledTime()) : TEXT(""),
						Npc->GetActorLocation().X, Npc->GetActorLocation().Y);
				};
				const FString Other = To ? Describe(To)
					: (Player ? FString::Printf(TEXT("플레이어 @(%.0f,%.0f)"), Player->GetActorLocation().X, Player->GetActorLocation().Y) : TEXT("-"));
				StallChainLog.Add(FString::Printf(TEXT("  %s %s <- %s"), Reason, *Describe(From), *Other));
			}
		}
		if (SubNpcCrowdCVars::DebugBlock == 0 || !From)
		{
			return;
		}
		const AActor* Target = To ? static_cast<const AActor*>(To) : static_cast<const AActor*>(Player);
		if (!Target)
		{
			return;
		}
		const FVector Up(0.f, 0.f, 100.f);
		const FVector Start = From->GetActorLocation() + Up;
		const float Life = AvoidUpdateInterval + 0.05f;
		DrawDebugLine(World, Start, Target->GetActorLocation() + Up, Color, false, Life, 0, 3.f);
		DrawDebugPoint(World, Start, 12.f, Color, false, Life);
#endif
	};

	for (TPair<ANpcCrowdPath*, TArray<FLaneAgent>>& Pair : AgentsByPath)
	{
		const TArray<FLaneAgent>& Agents = Pair.Value;
		const float HalfWidth = Pair.Key->GetLaneHalfWidth();

		for (const FLaneAgent& Me : Agents)
		{
			// 다른 경로에서 옮겨 넣은 사람은 장애물로만 쓴다. 그 사람의 계획은 원래 경로에서 한다.
			if (!Me.Npc || !Me.bWalking || Me.bForeign)
			{
				continue;
			}

			// [합류] 길로 돌아오는 사람은 차가 합류하듯 틈을 본다. 합류할 차선에 사람이 있거나
			// 뒤에서 다가오고 있으면 길가에 서서 기다린다. 예전에는 곧장 걸어 들어가 몸을 뚫었다.
			if (Me.bMerging)
			{
				// [교착 방지] 몸이 이미 길 위에 들어섰으면 기다리지 않는다.
				// 길 위에 서서 기다리면 뒷사람이 이 사람 때문에 멈추고, 멈춘 사람들이 합류 자리를
				// 계속 차지해서 서로 영영 기다렸다 (로그: 합류중인 사람이 10초 넘게 같은 자리).
				bool bHold = false;
				const bool bStillOffPath = FMath::Abs(Me.Lane) > HalfWidth + SubNpcCrowdCVars::MergeOnPathTolerance;
				for (const FLaneAgent& Other : Agents)
				{
					if (&Other == &Me || Other.bMerging)
					{
						continue;
					}
					if (FMath::Abs(Other.Lane - Me.TargetLane) >= LaneClear)
					{
						continue;
					}
					// 상대가 다가오는 방향 기준으로 내가 얼마나 앞에 있나. 서 있는 상대(플레이어 등)는 앞뒤 모두 본다.
					const float OtherAlong = (Other.Along != 0.f) ? Other.Along : Me.Along;
					const float MeAheadOfOther = (Me.Dist - Other.Dist) * OtherAlong;
					if (MeAheadOfOther > -SubNpcCrowdCVars::MergeClearBehind && MeAheadOfOther < SubNpcCrowdCVars::MergeClearAhead)
					{
						bHold = true;
						break;
					}
				}
				Me.Npc->SetMergeHold(bHold && bStillOffPath);
				if (bHold)
				{
					DrawBlockDebug(Me.Npc, nullptr, nullptr, FColor::Orange);
				}
				continue;
			}
			ASubNpcCharacter* Rear = Me.Npc;

			// 1) 내 차선 앞을 막는 가장 가까운 상대.
			//
			// [버그 수정] 앞뒤·좌우 간격을 경로 중심선 좌표가 아니라 실제 위치로 잰다.
			// 직선에서는 같지만 모서리에서는 안쪽 차선 사람들이 실제로는 훨씬 가까이 붙는다.
			// 중심선 거리로 재면 모서리 안쪽에서 몸이 겹치거나 엉뚱한 사람을 막는 사람으로 봐서
			// 모서리마다 뭉쳤다 (로그: 정지 인원이 B→C 모서리와 C의 굽은 곳에 몰림).
			const FVector MyDir = (Pair.Key->GetDirectionAtDistance(Me.Dist) * Me.Along).GetSafeNormal2D();
			const FVector MyRight(-MyDir.Y, MyDir.X, 0.f);

			// [교착 방지] 이음매에서 가로질러 오는 이전 경로 사람은 막는 상대로 보지 않는다.
			// 직각 모서리(B→C)에서는 B 끝의 사람과 C 입구의 사람이 진행 방향이 90도라 둘 다 상대를
			// "내 바로 앞"으로 봤다. 서로를 기다리며 같이 멈춰 길 전체가 섰다
			// (로그: 51 <- 55, 55 <- 51 처럼 서로를 지목하고 9초 넘게 정지).
			// 이미 다음 경로로 넘어간 사람이 우선이고, 아직 넘어오지 않은 사람이 양보한다.
			// 같은 쪽을 보고 실제로 앞에 있는 이전 경로 사람은 그대로 앞사람으로 본다.
			auto IsYieldingUpstream = [&](const FLaneAgent& Other)
			{
				return Other.bUpstream
					&& FVector::DotProduct(Other.Fwd, MyDir) < SubNpcCrowdCVars::CrossingHeadingDot;
			};

			const FLaneAgent* Blocker = nullptr;
			float BlockAhead = TNumericLimits<float>::Max();
			for (const FLaneAgent& Other : Agents)
			{
				if (&Other == &Me || IsYieldingUpstream(Other))
				{
					continue;
				}

				const FVector ToOther = Other.Loc - Me.Loc;
				const float Ahead = FVector::DotProduct(ToOther, MyDir);
				const float SideGap = FMath::Abs(FVector::DotProduct(ToOther, MyRight));

				// [모서리] 다른 경로에서 내 앞을 가로질러 가는 사람(방향이 크게 다름)은 금방 지나간다.
				// 이 사람을 앞을 막는 사람으로 보면, 모서리 안쪽 행인이 지나가는 사람마다 서서
				// 끝없이 기다렸다 (로그: B→C 모서리 안쪽에서 12초 정지). 부딪히기 직전일 때만 피한다.
				if (Other.bForeign && FVector::DotProduct(Other.Fwd, MyDir) < SubNpcCrowdCVars::CrossingHeadingDot
					&& Ahead > SubNpcCrowdCVars::FollowStopDistance)
				{
					continue;
				}
				const bool bOncoming = Other.Npc && Other.Along != Me.Along;
				const float Window = bOncoming ? SubNpcCrowdCVars::HeadOnLookAhead : SubNpcCrowdCVars::FollowSlowDistance;
				if (Ahead <= 0.f || Ahead > Window)
				{
					continue;
				}
				// [변경] 플레이어는 몸이 실제로 닿을 거리일 때만 막힌 걸로 본다.
				// 예전에는 행인끼리와 같은 75cm 기준을 써서, 길 가장자리에 서 있기만 해도
				// 맨 앞 행인이 멈추고 그 뒤로 길 전체가 줄을 섰다.
				const float Clearance = Other.Npc
					? LaneClear
					: Rear->GetSimpleCollisionRadius() + PlayerRadius + SubNpcCrowdCVars::PlayerBodyMargin;
				if (SideGap >= Clearance)
				{
					continue;
				}
				if (Ahead < BlockAhead)
				{
					BlockAhead = Ahead;
					Blocker = &Other;
				}
			}

			if (!Blocker)
			{
				continue;   // 앞이 비었다
			}

			const bool bBlockerIsPlayer = (Blocker->Npc == nullptr);
			const bool bOncoming = !bBlockerIsPlayer && Blocker->Along != Me.Along;
			const bool bSameGroup = !bBlockerIsPlayer && Rear->IsSameGroup(Blocker->Npc);

			const float StopDist = bBlockerIsPlayer
				? Rear->GetSimpleCollisionRadius() + PlayerRadius + SubNpcCrowdCVars::LightBumpContactSlack
				: SubNpcCrowdCVars::FollowStopDistance;
			const float GapScale = FMath::Clamp(
				(BlockAhead - StopDist) / FMath::Max(1.f, SubNpcCrowdCVars::FollowSlowDistance - StopDist),
				0.f, 1.f);

			// 2) 비켜 갈 상대인가. 같은 일행은 대열을 지킨다.
			//
			// [변경] 예전에는 내 걸음이 앞사람 실제 속도보다 20% 이상 빨라야만 추월했다.
			// 걸음 속도가 고르게 퍼져 있어서 대부분의 짝은 그 차이가 안 났고, 가장 느린 사람
			// 뒤로 길 전체가 줄지어 천천히 걸었다. 이제 앞사람 때문에 조금이라도 느려지면 추월한다.
			const bool bBlockerSlow = bBlockerIsPlayer || bOncoming || !Blocker->bWalking
				|| (Blocker->Npc && Blocker->Npc->IsHeldUp())
				|| Blocker->Speed < Rear->GetWalkSpeed() * SubNpcCrowdCVars::OvertakeSlowerRatio;

			if (bSameGroup || !bBlockerSlow)
			{
				// 친구끼리는 대열 간격을 일행 쪽 로직(리더 진행도 + 간격)이 맞춘다. 여기서는 몸이 실제로
				// 닿을 만큼 붙었을 때만 살짝 늦춘다.
				// [버그 수정] 예전에는 행인끼리와 같은 정지 거리(85cm)를 썼는데, 대열 자체가 리더 50~80cm
				// 뒤·옆 50cm라 두 번째 일행은 매번 "정지"를 받았다. 뒤처지면 리더가 기다리느라 60%로 걸어서
				// 일행 전체가 느려졌다.
				const bool bGroupTouching = bSameGroup && BlockAhead < SubNpcCrowdCVars::GroupBodyTouchDistance;
				if (!bSameGroup || bGroupTouching)
				{
					Rear->RequestSlowDown(bSameGroup ? SubNpcCrowdCVars::GroupTouchSpeedScale : GapScale);
					DrawBlockDebug(Rear, Blocker->Npc, PlayerPawn, bSameGroup ? FColor::Cyan : FColor::White);
				}
				continue;
			}

			// 3) 빈 차선 찾기. 막는 상대 바로 옆 차선이 비어 있어야 한다.
			//    내 옆이나 바로 뒤에 있는 사람 자리로 끼어들지도 않는다.
			// [버그 수정] 빈 차선 판정도 실제 위치로 한다. 상대의 차선을 "내 자리에서 본 옆 간격"으로
			// 바꿔서 쓴다 (내 차선 + 옆 간격). 중심선 좌표로 재면 직각에 가까운 모서리·굽은 곳에서
			// 실제로는 옆이 비어 있는데 차 있다고 보거나 그 반대가 되어, "빈 차선 없음"으로 줄줄이 섰다.
			auto PhysicalLane = [&](const FLaneAgent& Other)
			{
				return Me.Lane + FVector::DotProduct(Other.Loc - Me.Loc, MyRight) * Me.Along;
			};
			const float BlockerLane = PhysicalLane(*Blocker);

			auto IsLaneFree = [&](float TargetLane, float Clearance, float MaxAbsLane)
			{
				if (FMath::Abs(TargetLane) > MaxAbsLane)
				{
					return false;
				}
				for (const FLaneAgent& Other : Agents)
				{
					if (&Other == &Me || &Other == Blocker || IsYieldingUpstream(Other))
					{
						continue;
					}
					// [변경] 막는 상대 바로 옆(내 바로 뒤 ~ 상대를 지나친 직후)만 비어 있으면 된다.
					// 예전에는 2.75m 앞까지 그 차선에 아무도 없어야 했다. 행인들이 길 폭 안에
					// 흩어져 걸어서 줄이 생기면 양옆이 늘 "차 있는" 것으로 보였고, 전부 서서 기다렸다.
					// 더 앞에 있는 사람은 비켜선 뒤에 새 앞사람으로 다시 판단하면 된다.
					const float Ahead = FVector::DotProduct(Other.Loc - Me.Loc, MyDir);
					if (Ahead < -SubNpcCrowdCVars::PassBehindWindow || Ahead > BlockAhead + SubNpcCrowdCVars::FollowStopDistance)
					{
						continue;
					}
					// 내 앞을 가로질러 가는 다른 경로 사람은 금방 지나간다 (막는 상대 판정과 같은 기준).
					if (Other.bForeign && FVector::DotProduct(Other.Fwd, MyDir) < SubNpcCrowdCVars::CrossingHeadingDot
						&& Ahead > SubNpcCrowdCVars::FollowStopDistance)
					{
						continue;
					}
					if (FMath::Abs(PhysicalLane(Other) - TargetLane) < Clearance)
					{
						return false;
					}
				}
				return true;
			};

			// 쪽 고르기: 이미 비켜 가는 중이면 그 쪽을 끝까지 유지한다 (좌우 왕복 방지).
			// 마주 오는 사람은 각자 자기 오른쪽(경로 기준 정방향 +, 역방향 -)으로 비킨다.
			// 그 밖에는 이미 치우친 쪽, 정확히 같으면 이름 해시로 가른다.
			float Prefer = 0.f;
			if (Rear->IsPassing())
			{
				Prefer = FMath::Sign(Rear->GetPassLaneTarget() - BlockerLane);
			}
			if (Prefer == 0.f)
			{
				if (bOncoming)
				{
					Prefer = Me.Along;
				}
				else if (!FMath::IsNearlyEqual(Me.Lane, BlockerLane, 1.f))
				{
					Prefer = FMath::Sign(Me.Lane - BlockerLane);
				}
				else
				{
					Prefer = (GetTypeHash(Rear->GetFName()) & 1) ? 1.f : -1.f;
				}
			}

			// 두 단계로 찾는다.
			//   1) 여유 있게: 나란히 설 간격(LaneClear), 길 폭 안.
			//   2) 비집고: 어깨가 거의 닿는 간격(SqueezeClearance), 길 가장자리를 조금 넘어서도 된다.
			// [변경] 예전에는 1)만 봐서, 병목에서는 빈 차선이 없다며 전부 줄 서서 기다렸다.
			// 너무 느려 보여서, 정말 막힌 게 아니면 사람처럼 옆으로 비집고 지나가게 한다.
			//   3) 오래 서 있었으면: 어깨가 살짝 겹치는 간격(StallClearance)까지 허용한다.
			//      영영 안 풀리는 정체보다 잠깐 스치는 게 훨씬 덜 어색하다.
			struct FPassTry { float Clearance; float MaxAbsLane; };
			const bool bStalled = Rear->GetStalledTime() > SubNpcCrowdCVars::StallRelaxTime;
			const FPassTry Tries[] =
			{
				{ LaneClear, HalfWidth },
				{ SubNpcCrowdCVars::SqueezeClearance, HalfWidth + SubNpcCrowdCVars::SqueezeOverflow },
				{ bStalled ? SubNpcCrowdCVars::StallClearance : SubNpcCrowdCVars::SqueezeClearance,
				  HalfWidth + SubNpcCrowdCVars::SqueezeOverflow },
			};

			float TargetLane = 0.f;
			float UsedClearance = LaneClear;
			bool bFound = false;
			const int32 NumSides = Rear->IsPassing() ? 1 : 2;
			for (const FPassTry& Try : Tries)
			{
				for (int32 SideIdx = 0; SideIdx < NumSides && !bFound; ++SideIdx)
				{
					const float Side = (SideIdx == 0) ? Prefer : -Prefer;
					const float Candidate = BlockerLane + Side * Try.Clearance;
					if (IsLaneFree(Candidate, Try.Clearance, Try.MaxAbsLane))
					{
						TargetLane = Candidate;
						UsedClearance = Try.Clearance;
						bFound = true;
					}
				}
				if (bFound)
				{
					break;
				}
			}

			if (bFound)
			{
				Rear->RequestPassLane(TargetLane, PassUntil);

				// 옆으로 벌어진 만큼만 앞으로 간다. 덜 빠졌을 때는 간격 제동을 그대로 따르므로
				// 상대 몸을 뚫지 않고, 다 빠지면 원래 속도로 지나간다. 멈춰 있어도 옆으로는
				// 비켜설 수 있다 (SubNpcCrowdFlow::SidestepSpeed).
				// 옆으로 얼마나 빠졌는지도 실제 위치로 잰다 (모서리에서 중심선 차선 값은 실제와 다르다).
				const float SideGapToBlocker = FMath::Abs(FVector::DotProduct(Blocker->Loc - Me.Loc, MyRight));
				const float Clear = FMath::SmoothStep(UsedClearance * 0.5f, UsedClearance, SideGapToBlocker);

				// [변경] 옆으로 빠지는 동안 기어가지 않는다. 앞사람이 걷고 있으면 그 속도에 맞춰
				// 걸으면서 비켜선다 - 간격이 줄지 않으니 몸이 겹치지 않고, 전진이 있으니 옆으로도
				// 제 속도로 빠진다. 앞사람이 서 있거나 이미 바짝 붙었으면 예전처럼 간격 제동을 따른다.
				const float MatchBlocker = (Blocker->bWalking && BlockAhead > StopDist)
					? FMath::Clamp(Blocker->Speed / FMath::Max(1.f, Rear->GetWalkSpeed()), 0.f, 1.f)
					: 0.f;

				// 비킬 차선은 찾았는데 오래 못 빠져나가고 있으면(행인끼리) 정체 해소처럼 천천히 스치며 나간다.
				// 예전에는 이 경우 정체 해소가 걸리지 않아 추월 중인 채로 10초 넘게 서 있었다.
				const float StallFloor = (!bBlockerIsPlayer && Rear->GetStalledTime() > SubNpcCrowdCVars::StallGiveUpTime)
					? SubNpcCrowdCVars::StallCreepScale
					: 0.f;
				Rear->RequestSlowDown(FMath::Max(StallFloor, FMath::Lerp(FMath::Max(GapScale, MatchBlocker), 1.f, Clear)));
				DrawBlockDebug(Rear, Blocker->Npc, PlayerPawn, FColor::Yellow);
			}
			else if (!bBlockerIsPlayer && Rear->GetStalledTime() > SubNpcCrowdCVars::StallGiveUpTime)
			{
				// [정체 해소] 한참을 서 있었는데도 비킬 곳이 없다. 행인끼리라면 천천히 스치며 지나간다.
				// 잠깐 어깨가 겹치는 게, 길 전체가 영영 멈춰 있는 것보다 낫다. 플레이어는 뚫지 않는다.
				Rear->RequestSlowDown(SubNpcCrowdCVars::StallCreepScale);
				DrawBlockDebug(Rear, Blocker->Npc, PlayerPawn, FColor::Magenta);
			}
			else
			{
				// 빈 차선이 없다 (병목). 옆으로 비비지 않고 뒤에서 줄 서서 기다린다.
				Rear->RequestSlowDown(GapScale);
				DrawBlockDebug(Rear, Blocker->Npc, PlayerPawn, FColor::Red);
			}
		}
	}

#if !UE_BUILD_SHIPPING
	// crowd.LogBlock: 2초마다 "누가 왜 느려졌고, 누가 가장 많이 막고 있나"를 한 덩어리로 찍는다.
	// 정체 원인을 화면 없이 로그만으로 찾기 위한 것이다.
	{
		static float LastBlockLogTime = -1000.f;
		// [버그 수정] static이라 PIE를 다시 켜도 이전 판의 시각이 남는다. 새 판은 시간이 0부터라 로그가 한동안 안 찍혔다.
		if (Now < LastBlockLogTime)
		{
			LastBlockLogTime = -1000.f;
			StallChainLog.Reset();
		}
		if (SubNpcCrowdCVars::LogBlock != 0 && Now - LastBlockLogTime >= 2.f)
		{
			LastBlockLogTime = Now;

			int32 Walking = 0, Stopped = 0;
			for (const ASubNpcCharacter* Npc : Nearby)
			{
				if (Npc->IsWalkingOnPath())
				{
					++Walking;
					if (Npc->GetStalledTime() > 0.5f)
					{
						++Stopped;
					}
				}
			}

			FString Reasons;
			for (const TPair<FString, int32>& R : ReasonCounts)
			{
				Reasons += FString::Printf(TEXT(" %s=%d"), *R.Key, R.Value);
			}

			BlockerCounts.ValueSort([](int32 A, int32 B) { return A > B; });
			FString Blockers;
			int32 Shown = 0;
			for (const TPair<const AActor*, int32>& B : BlockerCounts)
			{
				if (Shown++ >= 5)
				{
					break;
				}
				FString State = TEXT("플레이어");
				if (const ASubNpcCharacter* Npc = Cast<ASubNpcCharacter>(B.Key))
				{
					State = FString::Printf(TEXT("%s/속도%.0f%%%s%s%s%s"),
						Npc->IsWalkingOnPath() ? TEXT("걷기") : TEXT("멈춤(반응·스팟)"),
						Npc->GetCurrentMoveSpeed() / FMath::Max(1.f, Npc->GetWalkSpeed()) * 100.f,
						Npc->IsRejoiningPath() ? TEXT("/합류중") : TEXT(""),
						Npc->IsWaitingForGroup() ? TEXT("/일행대기") : TEXT(""),
						Npc->GetGroupLeader() ? TEXT("/일행") : TEXT(""),
						Npc->GetStalledTime() > 0.5f ? *FString::Printf(TEXT("/정지%.1f초"), Npc->GetStalledTime()) : TEXT(""));
				}
				Blockers += FString::Printf(TEXT(" [%s x%d %s @%s]"),
					*GetNameSafe(B.Key), B.Value, *State, *B.Key->GetActorLocation().ToCompactString());
			}

			UE_LOG(LogTemp, Log, TEXT("[CrowdBlock] 근처 걷는 사람 %d명 중 0.5초 넘게 정지 %d명 | 감속 이유:%s | 많이 막는 상대:%s"),
				Walking, Stopped, *Reasons, *Blockers);
			for (const FString& Line : StallChainLog)
			{
				UE_LOG(LogTemp, Log, TEXT("[CrowdStall]%s"), *Line);
			}
			StallChainLog.Reset();
		}
	}
#endif

	const float AvoidSq = FMath::Square(FMath::Min(AvoidDistance, SubNpcCrowdCVars::LaneClearance));

	for (int32 i = 0; i < Nearby.Num(); ++i)
	{
		ASubNpcCharacter* A = Nearby[i];
		const FVector LocA = A->GetActorLocation();

		for (int32 j = i + 1; j < Nearby.Num(); ++j)
		{
			ASubNpcCharacter* B = Nearby[j];

			// 앞뒤 간격과 추월은 위의 차선 계획이 처리했다. 경로 이음매(서로 다른 경로)의 쌍도
			// 다른 경로의 사람을 이 경로 좌표로 옮겨 넣어 거기서 같이 처리한다.
			// 여기서는 같은 경로 위에서 그래도 겹친 몸을 살짝 떼어내는 일만 한다.
			if (A->GetCurrentPath() != B->GetCurrentPath())
			{
				continue;
			}

			// 둘 다 멈춰 있으면 서로 할 일이 없다.
			if (!A->IsWalkingOnPath() && !B->IsWalkingOnPath())
			{
				continue;
			}

			const bool bSameGroup = A->IsSameGroup(B);

			// [의도] 같은 그룹(친구)끼리는 좌우로 밀어내지 않는다.
			// "친구는 붙고 남남은 떨어진다"는 그림이 이 한 줄에서 나온다.
			if (bSameGroup)
			{
				continue;
			}

			// 추월 중인 쌍은 좌우로 밀어내지 않는다. 추월 차선이 이미 간격을 벌리고 있고,
			// 밀어내기까지 겹치면 두 힘이 싸워서 서로 좌우로 흔들린다.
			if (A->IsPassing() || B->IsPassing())
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(LocA, B->GetActorLocation());
			if (DistSq >= AvoidSq || DistSq < KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// 겹친 정도(0~1). 가까울수록 세게 민다.
			const float Overlap = 1.f - (FMath::Sqrt(DistSq) / AvoidDistance);
			const float Push = Overlap * AvoidDistance * AvoidStrength;

			// 차선 오프셋이 작은 쪽을 더 왼쪽으로, 큰 쪽을 더 오른쪽으로 민다.
			// 값이 같으면 인덱스로 갈라서 서로 반대 방향이 되게 한다.
			const float OffA = A->GetLaneOffset();
			const float OffB = B->GetLaneOffset();
			const float Sign = (OffA != OffB) ? (OffA < OffB ? -1.f : 1.f) : ((i % 2 == 0) ? -1.f : 1.f);

			// 멈춘 쪽은 밀지 않는다. 반응이 끝나는 순간 쌓인 양만큼 튀어나간다.
			if (A->IsWalkingOnPath())
			{
				A->AddAvoidanceNudge(Sign * Push);
			}
			if (B->IsWalkingOnPath())
			{
				B->AddAvoidanceNudge(-Sign * Push);
			}

		}
	}
}

void USubNpcDirectorSubsystem::UpdateOffLaneYield()
{
	const float YieldDistSq = FMath::Square(SubNpcCrowdCVars::OffLaneYieldDistance);

	for (ASubNpcCharacter* Mover : ActiveNpcs)
	{
		if (!IsValid(Mover) || Mover->IsPooled())
		{
			continue;
		}
		if (!Mover->IsMovingOffLane())
		{
			Mover->SetOffLaneYield(false);
			continue;
		}

		const FVector MyLoc = Mover->GetActorLocation();
		const FVector MyFwd = Mover->GetActorForwardVector().GetSafeNormal2D();
		bool bYield = false;

		for (const ASubNpcCharacter* Other : ActiveNpcs)
		{
			if (Other == Mover || !IsValid(Other) || Other->IsPooled())
			{
				continue;
			}

			// 길 밖에서 움직이는 사람끼리만 양보한다.
			//   - 서 있는 사람(스팟 칸·줄 자리)에게 양보하면 영영 못 움직인다. 그 자리는 처음부터 떨어져 있다.
			//   - [변경] 길 위를 걷는 사람에게도 양보하지 않는다. 길을 가로지르다 양보하면 길 한가운데 서서
			//     장애물이 되고, 길 위 행인은 또 이 사람을 피하느라 서로 기다리며 뭉쳤다.
			//     길 위 행인이 이 사람을 보고 비켜 가므로(차선 계획의 장애물) 이쪽은 계속 걸어간다.
			if (!Other->IsMovingOffLane())
			{
				continue;
			}

			FVector To = Other->GetActorLocation() - MyLoc;
			To.Z = 0.f;
			const float DistSq = To.SizeSquared();
			if (DistSq > YieldDistSq || DistSq < KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const FVector ToDir = To.GetSafeNormal();

			// 내가 가는 방향 앞에 있을 때만 양보한다.
			if (FVector::DotProduct(MyFwd, ToDir) < SubNpcCrowdCVars::OffLaneYieldFrontDot)
			{
				continue;
			}

			// 둘 다 길 밖에서 서로를 향해 오고 있으면 한 명만 양보한다 (둘 다 서면 영영 못 지나간다).
			if (Other->IsMovingOffLane())
			{
				const FVector OtherFwd = Other->GetActorForwardVector().GetSafeNormal2D();
				const bool bOtherFacesMe = FVector::DotProduct(OtherFwd, -ToDir) >= SubNpcCrowdCVars::OffLaneYieldFrontDot;
				if (bOtherFacesMe && GetTypeHash(Mover->GetFName()) < GetTypeHash(Other->GetFName()))
				{
					continue;
				}
			}

			bYield = true;
			break;
		}

		Mover->SetOffLaneYield(bYield);
	}
}

void USubNpcDirectorSubsystem::UpdateGroupCohesion()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// 리더별로 가장 많이 뒤처진 일행의 거리를 잰다.
	// 대열 간격(GroupFollowGap)만큼은 원래 떨어져 있는 거리이므로 빼고 센다.
	// 경로 이음매를 넘는 순간에도 맞도록 진행도 대신 실제 거리로 잰다.
	TMap<ASubNpcCharacter*, float> MaxLag;
	for (ASubNpcCharacter* Npc : ActiveNpcs)
	{
		if (!IsValid(Npc))
		{
			continue;
		}
		ASubNpcCharacter* Leader = Npc->GetGroupLeader();
		if (!IsValid(Leader) || Leader->IsPooled())
		{
			continue;
		}
		const float Lag = FVector::Dist2D(Npc->GetActorLocation(), Leader->GetActorLocation()) - Npc->GetGroupFollowGap();
		float& Max = MaxLag.FindOrAdd(Leader, 0.f);
		Max = FMath::Max(Max, Lag);
	}

	for (const TPair<ASubNpcCharacter*, float>& Pair : MaxLag)
	{
		ASubNpcCharacter* Leader = Pair.Key;
		const float Lag = Pair.Value;

		if (!Leader->IsWalkingOnPath())
		{
			Leader->SetWaitingForGroup(false, Now);
			continue;
		}

		bool bWaiting = Leader->IsWaitingForGroup();
		const float WaitedFor = Now - Leader->GetGroupWaitStartTime();

		if (!bWaiting)
		{
			// 많이 벌어졌으면 기다리기 시작한다. 방금 기다리다 포기했으면 한동안은 다시 서지 않는다
			// (계속 못 따라오는 일행 때문에 리더가 서다 가다를 반복하지 않게).
			const bool bCooledDown = WaitedFor > SubNpcCrowdCVars::GroupWaitMaxTime + SubNpcCrowdCVars::GroupWaitCooldown;
			bWaiting = (Lag > SubNpcCrowdCVars::GroupWaitStartLag) && bCooledDown;
		}
		else if (Lag < SubNpcCrowdCVars::GroupWaitResumeLag || WaitedFor > SubNpcCrowdCVars::GroupWaitMaxTime)
		{
			// 다 따라왔으면, 또는 너무 오래 기다렸으면 다시 걷는다.
			bWaiting = false;
		}

		Leader->SetWaitingForGroup(bWaiting, Now);
		if (bWaiting)
		{
			// 완전히 서지 않고 아주 천천히 걸으며 기다린다. 길 한가운데 딱 서 있으면 그 자체가
			// 또 다른 정체가 된다. 느리게 걷는 리더는 뒷사람이 보통 앞사람처럼 추월해 간다.
			Leader->RequestSlowDown(SubNpcCrowdCVars::GroupWaitWalkScale);
		}
	}
}

void USubNpcDirectorSubsystem::UpdateGroupChats()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	// 리더별로 일행을 모은다. 그룹 정보는 추종자 쪽(GroupLeader)에만 있다.
	TMap<ASubNpcCharacter*, TArray<ASubNpcCharacter*, TInlineAllocator<4>>> Groups;
	for (ASubNpcCharacter* Npc : ActiveNpcs)
	{
		if (!IsValid(Npc))
		{
			continue;
		}
		if (ASubNpcCharacter* Leader = Npc->GetGroupLeader())
		{
			Groups.FindOrAdd(Leader).Add(Npc);
		}
	}

	for (TPair<ASubNpcCharacter*, TArray<ASubNpcCharacter*, TInlineAllocator<4>>>& Pair : Groups)
	{
		ASubNpcCharacter* Leader = Pair.Key;
		if (!IsValid(Leader) || !Leader->IsWalkingOnPath())
		{
			continue;
		}

		// 보이는 거리에 있는 무리만. 멀리서 멈춰 봐야 아무도 못 본다.
		const ESubNpcLodBand Band = Leader->GetLodBand();
		if (Band != ESubNpcLodBand::Near && Band != ESubNpcLodBand::Mid)
		{
			continue;
		}

		// 처음 본 리더는 시각만 정해둔다. 등장하자마자 멈추면 어색하다.
		if (Leader->GetNextGroupChatTime() <= 0.f)
		{
			Leader->SetNextGroupChatTime(Now + FMath::FRandRange(
				SubNpcCrowdCVars::GroupChatIntervalMin, SubNpcCrowdCVars::GroupChatIntervalMax));
			continue;
		}
		if (Now < Leader->GetNextGroupChatTime())
		{
			continue;
		}
		Leader->SetNextGroupChatTime(Now + FMath::FRandRange(
			SubNpcCrowdCVars::GroupChatIntervalMin, SubNpcCrowdCVars::GroupChatIntervalMax));

		if (FMath::FRand() > SubNpcCrowdCVars::GroupChatChance)
		{
			continue;
		}

		// 리더 근처에서 같이 걷고 있는 일행만 낀다.
		TArray<ASubNpcCharacter*, TInlineAllocator<4>> Members;
		Members.Add(Leader);
		const float RadiusSq = FMath::Square(SubNpcCrowdCVars::GroupChatRadius);
		for (ASubNpcCharacter* Member : Pair.Value)
		{
			if (Member->IsWalkingOnPath()
				&& Member->GetCurrentPath() == Leader->GetCurrentPath()
				&& FVector::DistSquared(Member->GetActorLocation(), Leader->GetActorLocation()) < RadiusSq)
			{
				Members.Add(Member);
			}
		}
		if (Members.Num() < 2)
		{
			continue;
		}

		// [변경] 멈추지 않고 걸으면서 이야기한다. 같은 시간 동안, 정해진 순서로 번갈아 손짓한다.
		// 첫 번째로 말할 사람은 매번 바꿔서 늘 리더만 먼저 말하지 않게 한다.
		const float Duration = FMath::FRandRange(
			SubNpcCrowdCVars::GroupChatDurationMin, SubNpcCrowdCVars::GroupChatDurationMax);
		const int32 FirstSpeaker = FMath::RandRange(0, Members.Num() - 1);

		for (int32 i = 0; i < Members.Num(); ++i)
		{
			const int32 Order = (i - FirstSpeaker + Members.Num()) % Members.Num();
			// 이야기 상대: 리더는 첫 번째 일행을, 일행은 리더를 본다.
			ASubNpcCharacter* Partner = (i == 0) ? Members[1] : Members[0];
			Members[i]->StartGroupChat(Duration, SubNpcCrowdCVars::GroupTalkAnimId,
				Order, Members.Num(), SubNpcCrowdCVars::GroupChatTurnLength, Partner);
		}

		// 대화가 끝난 뒤부터 다음 간격을 센다. 이야기 도중에 다시 시작되지 않게 한다.
		Leader->SetNextGroupChatTime(Now + Duration + FMath::FRandRange(
			SubNpcCrowdCVars::GroupChatIntervalMin, SubNpcCrowdCVars::GroupChatIntervalMax));
	}
}

void USubNpcDirectorSubsystem::UpdateLodSlice()
{
	FVector ViewerLocation;
	if (!GetViewerLocation(ViewerLocation))
	{
		return;
	}

	if (ActiveNpcs.Num() == 0)
	{
		LodCursor = 0;
		return;
	}

	// 거리 제곱으로 비교해서 제곱근 연산을 피한다. 인원이 많을수록 차이가 난다.
	// 밴드 경계는 개체마다 히스테리시스가 붙으므로 루프 안에서 계산한다.
	const float RecycleSq = FMath::Square(RecycleDistance);

	// [개선] 인원에 비례해 슬라이스를 키운다.
	// 고정 슬라이스면 인원이 늘수록 한 개체의 재검사 주기가 같이 늘어나서,
	// 플레이어가 달릴 때 LOD가 뒤늦게 따라온다.
	int32 SliceSize = LodSliceSize;
	if (LodFullSweepTime > 0.f)
	{
		const int32 SlicesPerSweep = FMath::Max(1, FMath::CeilToInt(LodFullSweepTime / LodUpdateInterval));
		SliceSize = FMath::Max(LodSliceSize, FMath::CeilToInt(static_cast<float>(ActiveNpcs.Num()) / SlicesPerSweep));
	}

	// crowd.LOD 0 이면 전원을 한 번에 Near로 올린다.
	// 슬라이스로 나눠 처리하면 "LOD를 껐는데도 일부는 아직 낮은" 상태가 섞여
	// 측정값이 흐려진다.
	const bool bLodEnabled = (SubNpcCrowdCVars::EnableLod != 0);
	const int32 Count = bLodEnabled ? FMath::Min(SliceSize, ActiveNpcs.Num()) : ActiveNpcs.Num();

	// 회수 대상은 순회 도중에 배열을 건드리지 않도록 모았다가 나중에 처리한다.
	TArray<ASubNpcCharacter*> ToRecycle;

	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveNpcs.Num() == 0)
		{
			break;
		}

		LodCursor = (LodCursor + 1) % ActiveNpcs.Num();
		ASubNpcCharacter* Npc = ActiveNpcs[LodCursor];

		if (!IsValid(Npc))
		{
			ActiveNpcs.RemoveAtSwap(LodCursor, 1, EAllowShrinking::No);
			continue;
		}

		const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), ViewerLocation);

		// 회수는 "충분히 멀다"만으로는 부족하다. 플레이어가 보고 있는 동안 사라지면
		// 그게 제일 티가 나는 위화감이다. 최근에 그려진 개체는 거리와 무관하게 남겨둔다.
		if (DistSq > RecycleSq && !Npc->WasRecentlyRendered(0.5f))
		{
			ToRecycle.Add(Npc);
			continue;
		}

		if (!bLodEnabled)
		{
			// 측정 모드: 거리와 무관하게 최고 상세도.
			// 회수는 그대로 살려둔다 (안 그러면 맵 밖까지 걸어간 개체가 계속 쌓인다).
			Npc->ApplyMovementTickRate(ESubNpcLodBand::Near);
			Npc->ApplyLodBand(ESubNpcLodBand::Near);
			continue;
		}

		// 거리만으로 정한 밴드. 아래에서 렌더링 여부로 한 단계 더 내려갈 수 있지만,
		// **이동 틱 주기는 이 값으로 고정한다.**
		//
		// [히스테리시스] 경계에 딱 걸친 개체는 미세한 거리 변화만으로 밴드가 널뛴다.
		// 밴드가 바뀔 때마다 강제 LOD·그림자·애니메이션 주기가 전부 다시 적용되므로
		// 그 자체로 눈에 띄는 깜빡임이 된다.
		// 그래서 "지금 있는 밴드에 머무는 쪽"으로 경계를 LodHysteresis만큼 밀어준다.
		// 밖으로 나가려면 조금 더 멀어져야 하고, 들어오려면 조금 더 가까워져야 한다.
		const int32 CurBand = static_cast<int32>(Npc->GetLodBand());
		constexpr float H = SubNpcCrowdCVars::LodHysteresis;

		const float NearLimit = (CurBand <= 0) ? NearDistance + H : NearDistance - H;
		const float MidLimit  = (CurBand <= 1) ? MidDistance  + H : MidDistance  - H;
		const float FarLimit  = (CurBand <= 2) ? FarDistance  + H : FarDistance  - H;

		ESubNpcLodBand DistanceBand;
		if (DistSq <= FMath::Square(NearLimit))      { DistanceBand = ESubNpcLodBand::Near; }
		else if (DistSq <= FMath::Square(MidLimit))  { DistanceBand = ESubNpcLodBand::Mid; }
		else if (DistSq <= FMath::Square(FarLimit))  { DistanceBand = ESubNpcLodBand::Far; }
		else                                         { DistanceBand = ESubNpcLodBand::Culled; }

		// 카메라를 돌리면 뒤쪽 개체가 순간적으로 "안 그려짐" 판정을 받는다.
		// 그때 틱 주기까지 같이 널뛰면 위치 갱신이 뚝뚝 끊겨서 순간이동으로 보인다.
		// 이동은 보이든 말든 매끄러워야 하므로 거리 기준으로만 정한다.
		Npc->ApplyMovementTickRate(DistanceBand);

		ESubNpcLodBand Band = DistanceBand;

		// 실제로 화면에 안 그려지고 있으면 한 단계 낮춘다.
		// WasRecentlyRendered는 렌더 스레드가 채워주는 값이라 공짜에 가깝다.
		//
		// [버그 수정] 예전에는 이 강등이 Culled까지 갈 수 있었다. 그런데 Culled는
		// 메시를 SetVisibility(false)로 숨긴다. 숨은 메시는 영원히 렌더링되지 않으므로
		// WasRecentlyRendered가 계속 false로 남고, 다음 갱신에서 또 Culled로 떨어진다.
		// 한 번 숨으면 거리가 Mid 안쪽으로 들어올 때까지 스스로 못 빠져나오는 자물쇠였다.
		//
		// 3인칭이라 카메라를 돌릴 때마다 등 뒤 개체가 "안 그려짐" 판정을 받는다.
		// 그래서 경계 근처에서 사라졌다 나타났다를 반복했다.
		//
		// 이제 "안 그려진다"를 이유로 한 강등은 Far까지만 허용한다.
		// Culled(=숨김)는 오직 거리로만 판정한다. 거리는 숨겨도 계속 갱신되므로
		// 스스로 되돌아올 수 있다.
		if (bDemoteWhenNotRendered && Band != ESubNpcLodBand::Culled)
		{
			if (!Npc->WasRecentlyRendered(0.5f))
			{
				// uint8 + 1은 int로 승격되므로 FMath::Min의 두 인자 타입이 어긋난다.
				// 양쪽을 int32로 맞춰서 비교한 뒤 다시 enum으로 되돌린다.
				const int32 Demoted = FMath::Min(
					static_cast<int32>(Band) + 1,
					static_cast<int32>(ESubNpcLodBand::Far));
				Band = static_cast<ESubNpcLodBand>(Demoted);
			}
		}

		Npc->ApplyLodBand(Band);
	}

	for (ASubNpcCharacter* Npc : ToRecycle)
	{
		ReturnToPool(Npc);
	}

	// 정원이 줄어들었으면 초과분을 먼 쪽부터 천천히 회수한다.
	// 한 프레임에 몰아서 처리하면 눈앞에서 사람들이 사라지는 게 보인다.
	if (ActiveNpcs.Num() > MaxActiveNpcs)
	{
		ASubNpcCharacter* Farthest = nullptr;
		float FarthestDistSq = -1.f;

		for (ASubNpcCharacter* Npc : ActiveNpcs)
		{
			if (!IsValid(Npc))
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), ViewerLocation);
			if (DistSq > FarthestDistSq)
			{
				FarthestDistSq = DistSq;
				Farthest = Npc;
			}
		}

		// 정원 초과분도 마찬가지로, 화면에 보이는 개체는 건드리지 않는다.
		if (Farthest && !Farthest->WasRecentlyRendered(0.5f))
		{
			ReturnToPool(Farthest);
		}
	}
}
