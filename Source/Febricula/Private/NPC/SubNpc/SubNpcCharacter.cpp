// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/SubNpc/SubNpcCharacter.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dialogue/DialogueSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/SubNpc/NpcActivitySpot.h"
#include "NPC/SubNpc/NpcCrowdPath.h"
#include "NPC/SubNpc/SubNpcDirectorSubsystem.h"

// ---------------------------------------------------------------------------
// 그룹 추종 튜닝값.
//
// [왜 UPROPERTY가 아닌가]
//   ASubNpcCharacter에 데이터 멤버를 추가하면 그 뒤 프로퍼티들의 메모리 오프셋이
//   전부 밀린다. WalkSpeed / bIsMoving / CurrentSpeed가 뒤쪽에 있는데,
//   AnimBP의 Property Access는 컴파일 시점 오프셋을 들고 있어서
//   ABP를 다시 컴파일하기 전까지 엉뚱한 값을 읽는다.
//   실제로 이것 때문에 bIsMoving이 false로 읽혀 걷기 애니메이션이 깨졌다.
//
//   값 조정 빈도가 낮으므로 상수로 둔다. 에디터 노출이 필요해지면
//   반드시 클래스 "맨 끝"에 추가하고 ABP를 다시 컴파일할 것.
// ---------------------------------------------------------------------------
namespace SubNpcGroundSnap
{
	/**
	 * 지면에 붙여 세우기.
	 *
	 * 행인은 CharacterMovement를 안 쓰므로 중력이 없다. 원래 설계는 "스플라인의
	 * 높이가 곧 바닥"이었는데, 그러면 경로를 그릴 때 지형 높이를 일일이 맞춰야 한다.
	 *
	 * 대신 발밑으로 짧은 라인 트레이스를 한 번 쏴서 바닥 높이에 맞춘다.
	 * 경로는 대충 그려도 되고, 계단·경사로 위에서도 제대로 선다.
	 *
	 * 비용은 가까운 개체당 프레임에 트레이스 1회다. 부담되면 crowd.GroundSnap 0.
	 */
	static int32 Enabled = 1;
	static FAutoConsoleVariableRef CVarGroundSnap(
		TEXT("crowd.GroundSnap"),
		Enabled,
		TEXT("행인을 지면에 붙여 세운다. 0이면 스플라인 높이를 그대로 쓴다."),
		ECVF_Cheat);

	/** 발밑에서 위로 이만큼 올린 지점에서 아래로 쏜다 (cm) */
	constexpr float TraceUp = 120.f;

	/** 아래로 이만큼까지 찾는다 (cm). 못 찾으면 스플라인 높이를 그대로 쓴다 */
	constexpr float TraceDown = 400.f;
}

namespace SubNpcPathHandoff
{
	/**
	 * 경로 전환 시 어긋남을 흡수하는 감쇠율 (1/초). 클수록 빨리 따라붙는다.
	 *
	 * [버그 수정] 예전에는 FMath::VInterpTo를 썼다. 그 함수는 내부적으로
	 * Dist * Clamp(DeltaTime * Speed, 0, 1) 만큼 움직인다. 그런데 행인의 틱 간격은
	 * LOD 밴드마다 다르다 - Far는 0.3초다. 0.3 * 5 = 1.5 가 1.0으로 클램프되면
	 * **한 틱에 곧바로 0이 되어 보간이 통째로 사라진다.**
	 * 실제로 이 보간은 Near에서만 동작했고 Far에서는 예전 그대로 순간이동이었다.
	 *
	 * 지수 감쇠는 틱 간격이 아무리 커도 0에 닿지 않는다. 프레임률·틱 주기와 무관하게
	 * 같은 시간 상수를 유지한다.
	 */
	constexpr float BlendRate = 4.f;

	/** 이보다 크게 벌어진 이음매는 흡수를 포기한다 (cm). 레벨 배치가 잘못된 경우다 */
	constexpr float MaxBlendDistance = 2000.f;

	/** 이보다 작아지면 0으로 끊는다 (cm) */
	constexpr float SnapThreshold = 1.f;
}

namespace SubNpcCrowdFlow
{
	/** 감속 배율이 목표값을 따라가는 속도 (1/초). 크면 급브레이크처럼 보인다 */
	constexpr float SpeedScaleInterpRate = 6.f;

	/**
	 * 감속 요청이 원래 속도로 되돌아가는 속도 (1/초).
	 *
	 * [버그 수정] 예전에는 매 틱 PendingSpeedScale = 1.f 로 초기화했다.
	 * 그런데 요청을 넣는 회피 계산은 AvoidUpdateInterval(기본 0.1초)마다만 돈다.
	 * 60 FPS면 6프레임 중 5프레임은 "요청 없음"이 되어 다시 가속해버렸고,
	 * 결국 감속이 거의 적용되지 않았다.
	 * 이제는 천천히 회복시켜서, 다음 갱신까지 감속이 유지되게 한다.
	 */
	constexpr float SpeedScaleRecoverRate = 1.5f;
}

namespace SubNpcGroupTuning
{
	/** 이만큼(cm) 벌어지면 최대 속도차를 낸다 */
	constexpr float CatchUpRange = 150.f;

	/** 추종자 속도 배율 범위. 하한이 0보다 커야 뒷걸음질이 없다 */
	constexpr float SpeedScaleMin = 0.55f;
	constexpr float SpeedScaleMax = 1.65f;
}

ASubNpcCharacter::ASubNpcCharacter()
{
	// 행인은 Director가 LOD에 따라 틱 간격을 조절한다.
	// 기본값은 꺼둔 상태로 시작해서, 풀에서 꺼낼 때만 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 행인은 길찾기를 하지 않는다. AIController 자체가 필요 없다.
	// (베이스가 BehaviorTree를 돌리려 하지만 BehaviorTreeAsset이 비어 있으면 아무 일도 안 한다)
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		// 행인은 이동을 직접 계산해서 위치를 세팅한다 (TickPathFollow 참고).
		// 그래서 캐릭터 무브먼트는 통째로 꺼둔다 - 마리당 중력·바닥 스윕 비용이 전부 사라진다.
		// 컴포넌트 자체는 ACharacter가 요구하므로 지우지 않고 틱만 멈춘다.
		Move->bUseRVOAvoidance = false;
		Move->PrimaryComponentTick.bCanEverTick = false;
		Move->PrimaryComponentTick.bStartWithTickEnabled = false;
	}

	bUseControllerRotationYaw = false;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		// [측정으로 찾은 낭비] 캡슐의 오버랩 이벤트를 끈다.
		//
		// stat game 프로파일링(90명):
		//   MoveComponent(Primitive)   0.59 ms  (92 calls)
		//   UpdateOverlaps Time        0.23 ms  (92 calls)
		//   PerformOverlapQuery Time   0.19 ms  (92 calls)
		//                              -------
		//                              1.01 ms
		//
		// 호출 수가 행인 수와 정확히 일치한다. 매 틱 SetActorLocationAndRotation을
		// 부를 때마다 캡슐이 오버랩 재계산을 하고 있었다. bSweep은 이미 false라
		// 스윕 비용은 없었지만, 오버랩 갱신은 스윕과 무관하게 돈다.
		//
		// 행인은 오버랩 이벤트를 쓰지 않는다:
		//   - 플레이어와 부딪히는 판정은 NotifyHit(블로킹 히트)이 담당한다
		//   - E키 상호작용은 플레이어 쪽에서 쏘는 쿼리라 응답 채널만 있으면 된다
		// 둘 다 오버랩 이벤트와 무관하므로 꺼도 동작이 바뀌지 않는다.
		Capsule->SetGenerateOverlapEvents(false);

		// 물리 볼륨(물속·중력 구역) 판정도 필요 없다. 이동할 때마다 도는 검사다.
		Capsule->SetShouldUpdatePhysicsVolume(false);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// 메타휴먼 기준 이게 제일 큰 절약이다: 화면에 안 보이면 본 계산 자체를 하지 않는다.
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

		// 애니메이션 업데이트 레이트 최적화 - 멀수록 포즈 갱신 빈도를 자동으로 낮춘다.
		MeshComp->bEnableUpdateRateOptimizations = true;

		// [측정으로 찾은 낭비] 메시의 물리 본 갱신을 끈다.
		//
		// 프로파일링(stat anim)에서 UpdateKinematicBonesToAnim이 90명 기준 2.96ms로
		// 잡혔다. 애니메이션 결과를 피직스 애셋의 본에 반영하는 작업인데,
		// 행인은 충돌을 전부 캡슐로 처리하고 메시의 물리 본은 아무도 조회하지 않는다.
		// 플레이어와 부딪히는 판정도, E키 상호작용도 캡슐이 담당한다.
		//
		// 즉 계산해놓고 아무도 안 쓰는 값이라 통째로 버릴 수 있다.
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetGenerateOverlapEvents(false);
		MeshComp->SetSimulatePhysics(false);

		// 물리와 애니메이션을 섞지 않는다 (래그돌 블렌딩을 안 쓰므로).
		MeshComp->bBlendPhysics = false;

		// 본 트랜스폼을 물리로 되돌려 쓰지 않는다.
		MeshComp->bUpdateJointsFromAnimation = false;
	}
}

void ASubNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 레벨에 직접 배치한 경우에도 Director가 관리하도록 등록한다.
	if (const UWorld* World = GetWorld())
	{
		if (USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>())
		{
			Director->RegisterNpc(this);
		}
	}
}

void ASubNpcCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨 종료 경로에서도 스팟 점유·타이머가 남지 않도록 같은 정리 함수를 탄다.
	ClearRuntimeState();

	if (const UWorld* World = GetWorld())
	{
		if (USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>())
		{
			Director->UnregisterNpc(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// 풀 수명
// ---------------------------------------------------------------------------

void ASubNpcCharacter::ActivateFromPool(const FSubNpcArchetypeRow& InArchetype, ANpcCrowdPath* StartPath, float StartDistance, bool bReversed)
{
	if (!StartPath)
	{
		return;
	}

	// 이전 개체의 상태가 조금이라도 남으면 "가끔 이상하게 행동하는 행인"이 생긴다.
	// 활성화 시점에 무조건 전부 새로 세팅한다.
	ClearRuntimeState();

	Archetype = InArchetype;
	CurrentPath = StartPath;
	PathDistance = FMath::Clamp(StartDistance, 0.f, StartPath->GetPathLength());
	bReversedOnPath = bReversed;
	GroupLeader = nullptr;
	GroupFollowGap = 0.f;

	// 개체별 랜덤화. 같은 메시라도 속도와 체격이 다르면 다른 사람처럼 보인다.
	WalkSpeed = FMath::FRandRange(Archetype.WalkSpeedMin, Archetype.WalkSpeedMax);
	const float Scale = FMath::FRandRange(Archetype.ScaleMin, Archetype.ScaleMax);
	SetActorScale3D(FVector(Scale));

	const float HalfWidth = StartPath->GetLaneHalfWidth();
	LaneOffset = FMath::FRandRange(-HalfWidth, HalfWidth);

	// 시작 위치로 순간이동시킨 뒤 보이게 한다 (순서가 반대면 옛 위치에서 한 프레임 깜빡인다).
	const FVector StartLoc = StartPath->GetLocationAtDistance(PathDistance, LaneOffset);
	const FVector Dir = StartPath->GetDirectionAtDistance(PathDistance) * (bReversedOnPath ? -1.f : 1.f);
	SetActorLocationAndRotation(StartLoc, Dir.Rotation());

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	// [중요] 부모 ABaseNpcCharacter가 생성자에서 bCanEverTick = false로 꺼둔다
	// (메인 NPC는 BehaviorTree로 돌아서 틱이 필요 없기 때문).
	// ASubNpcCharacter 생성자에서 다시 켜지만, 블루프린트가 그 값을 자기 기본값으로
	// 저장해버리면 틱 함수가 아예 등록되지 않아 SetActorTickEnabled가 무력해진다.
	// 행인은 틱이 곧 이동이므로 여기서 등록 여부까지 확실히 보장한다.
	PrimaryActorTick.bCanEverTick = true;
	if (!PrimaryActorTick.IsTickFunctionRegistered())
	{
		PrimaryActorTick.Target = this;
		PrimaryActorTick.RegisterTickFunction(GetLevel());
	}
	SetActorTickEnabled(true);

	CurrentActivity = ESubNpcActivity::Walking;
	SetMovementState(ENpcMovementState::Walk);

	// LOD는 Director가 다음 갱신에서 정한다. 그때까지는 가장 싼 상태로 둔다.
	CurrentLodBand = ESubNpcLodBand::Culled;
	ApplyMovementTickRate(ESubNpcLodBand::Far);
	ApplyLodBand(ESubNpcLodBand::Far);

	// 블루프린트에서 외형(메시/머티리얼/의상)을 갈아끼울 기회.
	OnArchetypeApplied(Archetype);

	NextSpotCheckDistance = PathDistance;
	RollForActivitySpot();
}

void ASubNpcCharacter::DeactivateToPool()
{
	// 블루프린트가 만들어둔 것(소품, 파티클 등)을 먼저 정리하게 한다.
	OnReturnedToPool();

	ClearRuntimeState();

	CurrentActivity = ESubNpcActivity::Pooled;

	// 숨기고, 충돌을 끄고, 틱을 멈춘다. 이 세 개가 모두 꺼져야 실제로 비용이 0이 된다.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// 스켈레탈 메시 컴포넌트는 숨겨도 애니메이션 틱이 살아있을 수 있어서 명시적으로 끈다.
		MeshComp->SetComponentTickEnabled(false);
		MeshComp->bPauseAnims = true;
	}

	// 화면 밖 먼 곳으로 치워둔다. 안 그러면 마지막 위치에서 충돌/오버랩 질의에 계속 걸린다.
	SetActorLocation(FVector(0.f, 0.f, -100000.f));
}

void ASubNpcCharacter::ClearRuntimeState()
{
	// [누수 방지 핵심] 스팟 점유를 안 풀면 그 스팟은 영영 정원이 찬 상태로 남는다.
	if (OccupiedSpot)
	{
		OccupiedSpot->ReleaseOccupancy(this);
		OccupiedSpot = nullptr;
	}

	// 재생 중인 슬롯 애니메이션 정리 (몽타주 인스턴스가 남지 않게)
	StopNpcAnim(0.f);

	// 외부 액터를 가리키는 참조는 전부 끊는다.
	// 풀에 들어간 NPC가 경로/리더를 계속 붙잡고 있으면 그쪽 액터가 GC되지 않는다.
	CurrentPath = nullptr;
	GroupLeader = nullptr;

	PathDistance = 0.f;
	bReversedOnPath = false;
	LaneOffset = 0.f;
	PathHandoffOffset = FVector::ZeroVector;
	PendingSpeedScale = 1.f;
	CurrentSpeedScale = 1.f;
	GroupFollowGap = 0.f;
	ActivityEndTime = 0.f;
	DodgeTargetOffset = 0.f;

	// [중요] 부딪힘 기록도 반드시 지운다.
	// 이 액터는 풀에서 재사용되므로, 안 지우면 방금 회수된 개체의 기록을 물려받은 채
	// 새 행인이 등장해서 "방금 부딪혀놓고..." 같은 말을 한다.
	// 플레이어 입장에서는 처음 보는 사람이 없던 일을 따지는 셈이라 바로 티가 난다.
	LastBumpTime = -1000.f;
	LastReactionTime = -1000.f;

	// [누수 방지] 새로 추가된 런타임 상태도 반드시 여기서 지운다.
	// 하나라도 빠지면 재사용된 개체가 이전 개체의 행동을 물려받는다.
	CurrentReaction = ESubNpcReaction::None;
	bIsMoving = false;
	CurrentSpeed = 0.f;
	CurrentSpotOption = FNpcActivityOption();
	CurrentSpotStep = -1;
	bResumeSpotAfterReaction = false;
	PendingAvoidNudge = 0.f;
	bPendingRecycle = false;
	NextSpotCheckDistance = 0.f;
}

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

void ASubNpcCharacter::ApplyMovementTickRate(ESubNpcLodBand DistanceBand)
{
	// 거리만 본다. 렌더링 여부로 내려간 밴드를 쓰면 카메라 회전마다 주기가 널뛴다.
	float Interval = 0.f;
	switch (DistanceBand)
	{
	case ESubNpcLodBand::Near:	Interval = 0.f;					break;
	case ESubNpcLodBand::Mid:	Interval = MidTickInterval;		break;
	default:					Interval = FarTickInterval;		break;
	}

	// 같은 값이면 건드리지 않는다. 틱 함수를 매번 다시 등록시킬 이유가 없다.
	if (!FMath::IsNearlyEqual(PrimaryActorTick.TickInterval, Interval))
	{
		SetActorTickInterval(Interval);
	}
}

void ASubNpcCharacter::ApplyLodBand(ESubNpcLodBand NewBand)
{
	if (NewBand == CurrentLodBand)
	{
		// 같은 값을 매번 적용하면 컴포넌트 갱신 비용이 오히려 더 든다.
		return;
	}

	CurrentLodBand = NewBand;

	USkeletalMeshComponent* MeshComp = GetMesh();

	switch (NewBand)
	{
	case ESubNpcLodBand::Near:
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(0);                       // 0 = 자동 LOD에 맡김
			MeshComp->SetCastShadow(true);
			MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			ApplyAnimUpdateRate(MeshComp, 1);   // 매 프레임
		}
		break;

	case ESubNpcLodBand::Mid:
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(MidForcedLod + 1);        // SetForcedLOD는 1-based (0은 자동)
			// 그림자는 메타휴먼에서 가장 비싼 항목 중 하나다. 중간 거리부터 끈다.
			MeshComp->SetCastShadow(false);
			ApplyAnimUpdateRate(MeshComp, MidAnimFrameSkip);
		}
		break;

	case ESubNpcLodBand::Far:
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(FarForcedLod + 1);
			MeshComp->SetCastShadow(false);
			ApplyAnimUpdateRate(MeshComp, FarAnimFrameSkip);
		}
		break;

	case ESubNpcLodBand::Culled:
	default:
		if (MeshComp)
		{
			// 렌더링도 애니메이션도 멈춘다. 위치 갱신(액터 틱)만 살려두면
			// 다시 보이게 됐을 때 "그동안 걸어온 것처럼" 자연스럽게 이어진다.
			MeshComp->SetComponentTickEnabled(false);
			MeshComp->bPauseAnims = true;
			MeshComp->SetCastShadow(false);
			MeshComp->SetVisibility(false, /*bPropagateToChildren=*/true);
		}
		break;
	}

	// Culled에서 빠져나올 때 다시 보이게 한다.
	if (MeshComp && NewBand != ESubNpcLodBand::Culled)
	{
		MeshComp->SetVisibility(true, /*bPropagateToChildren=*/true);
	}
}

void ASubNpcCharacter::ApplyAnimUpdateRate(USkeletalMeshComponent* MeshComp, int32 FrameSkip)
{
	// [측정으로 찾은 개선] 거리 밴드마다 포즈 갱신 빈도를 다르게 준다.
	//
	// 예전에는 LOD 밴드가 틱 간격·강제 메시LOD·그림자만 바꿨다. 그런데 프로파일링해보니
	// 군중 비용의 대부분이 애니메이션이었고, 그 부분은 밴드와 무관하게 매 프레임 돌고 있었다.
	// LOD를 껐다 켜도 게임 스레드가 거의 안 변하던 이유가 이것이다.
	//
	// UpdateRateOptimizations는 "N프레임에 한 번만 평가하고 사이는 보간"하는 기능이다.
	// 멀리 있는 개체는 4~8프레임에 한 번만 평가해도 눈에 안 띈다.
	if (!MeshComp)
	{
		return;
	}

	FAnimUpdateRateParameters* Params = MeshComp->AnimUpdateRateParams;
	if (!Params)
	{
		return;
	}

	// 엔진이 거리로 자동 계산하는 것을 끄고 우리가 직접 정한다.
	// Director가 이미 거리 판정을 하고 있으므로 두 번 할 이유가 없다.
	Params->bShouldUseLodMap = false;
	Params->UpdateRate = FMath::Max(1, FrameSkip);
	Params->EvaluationRate = FMath::Max(1, FrameSkip);

	// 건너뛴 프레임을 보간해서 채운다. 안 켜면 뚝뚝 끊겨 보인다.
	Params->bInterpolateSkippedFrames = (FrameSkip > 1);
}

// ---------------------------------------------------------------------------
// 이동
// ---------------------------------------------------------------------------

void ASubNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentActivity == ESubNpcActivity::Pooled)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// ABP가 읽을 값을 갱신한다.
	// 프로퍼티라 Property Access로 스레드 안전하게 읽힌다.
	bIsMoving = (CurrentActivity == ESubNpcActivity::Walking) ||
		(CurrentActivity == ESubNpcActivity::Reacting && CurrentReaction == ESubNpcReaction::Dodge);
	// 감속까지 반영해야 걷기 애니메이션 재생 속도가 실제 이동과 맞는다.
	CurrentSpeed = bIsMoving ? WalkSpeed * CurrentSpeedScale : 0.f;

	switch (CurrentActivity)
	{
	case ESubNpcActivity::AtSpot:
		if (Now >= ActivityEndTime)
		{
			// 한 동작으로 끝내지 않고 다음 단계가 있으면 이어서 재생한다.
			AdvanceSpotStep();
		}
		return;

	case ESubNpcActivity::Reacting:
		if (Now >= ActivityEndTime)
		{
			EndReaction();
		}
		else if (CurrentReaction == ESubNpcReaction::Dodge)
		{
			// 피하기는 걸으면서 비킨다. 차선 오프셋을 목표치로 부드럽게 민다.
			LaneOffset = FMath::FInterpTo(LaneOffset, DodgeTargetOffset, DeltaTime, 4.f);
			TickPathFollow(DeltaTime);
		}
		// [버그 수정] 화내기·놀라기는 "멈춰서 플레이어를 쳐다본다"가 연출인데,
		// 예전에는 여기서도 TickPathFollow를 불러 계속 전진했다.
		// 게다가 FaceTowards로 돌려둔 회전을 진행 방향으로 매 틱 덮어써서,
		// 화내는 애니메이션이 나오는 동안 옆을 보며 걸어가는 그림이 됐다.
		return;

	case ESubNpcActivity::Walking:
	default:
		TickPathFollow(DeltaTime);
		return;
	}
}

void ASubNpcCharacter::TickPathFollow(float DeltaTime)
{
	if (!CurrentPath)
	{
		return;
	}

	// 앞사람 때문에 걸린 제동을 반영한다. 목표 배율로 곧장 튀면 급브레이크로 보이므로
	// 부드럽게 따라가고, 요청값은 매 틱 1로 되돌려 Director가 다시 채우게 한다.
	CurrentSpeedScale = FMath::FInterpTo(
		CurrentSpeedScale, PendingSpeedScale, DeltaTime, SubNpcCrowdFlow::SpeedScaleInterpRate);

	// 요청은 회피 갱신 주기마다만 들어온다. 매 틱 1로 되돌리면 그 사이 프레임에서
	// 다시 가속해버리므로, 천천히 회복시켜 다음 갱신까지 감속을 유지한다.
	PendingSpeedScale = FMath::Min(
		1.f, PendingSpeedScale + DeltaTime * SubNpcCrowdFlow::SpeedScaleRecoverRate);

	// 그룹으로 다니는 경우 앞사람의 진행도에서 간격만큼 뒤를 따라간다.
	// 각자 독립적으로 걸으면 신호에 걸릴 때마다 대열이 흩어진다.
	// 진행 부호. 역방향이면 PathDistance가 줄어드는 쪽이 "앞"이다.
	const float Along = bReversedOnPath ? -1.f : 1.f;

	if (ASubNpcCharacter* Leader = GroupLeader.Get())
	{
		if (Leader->CurrentPath == CurrentPath && !Leader->IsPooled())
		{
			// [버그 수정] 리더 위치를 그대로 대입하지 않는다.
			//
			// 예전에는 PathDistance = Leader->PathDistance - Gap 으로 매 틱 덮어썼다.
			// 그래서 세 가지가 눈에 보이는 결함으로 나타났다.
			//   1) 리더가 스팟에 들어가 멈추면 추종자도 그 자리에 얼어붙는다
			//   2) 리더가 막다른 길에서 방향을 돌리면 추종자가 순간이동으로 뒤로 간다
			//   3) 리더의 회피 흔들림이 추종자에게 그대로 복사돼 대열이 떤다
			//
			// 대신 추종자도 자기 속도로 걷되, 벌어진 만큼 속도만 조절한다.
			// 위치를 직접 건드리지 않으므로 뒷걸음질도 순간이동도 없다.
			const float Behind = bReversedOnPath ? GroupFollowGap : -GroupFollowGap;
			const float Desired = Leader->PathDistance + Behind;

			// 진행 방향 기준 오차. 양수면 뒤처진 상태다.
			const float Error = (Desired - PathDistance) * Along;

			const float SpeedScale = FMath::Clamp(
				1.f + Error / SubNpcGroupTuning::CatchUpRange,
				SubNpcGroupTuning::SpeedScaleMin,
				SubNpcGroupTuning::SpeedScaleMax);

			PathDistance += WalkSpeed * SpeedScale * CurrentSpeedScale * DeltaTime * Along;
		}
		else
		{
			// 리더가 다른 경로로 갔거나 회수됐으면 그룹을 해산하고 혼자 걷는다.
			GroupLeader = nullptr;
			PathDistance += WalkSpeed * CurrentSpeedScale * DeltaTime * Along;
		}
	}
	else
	{
		PathDistance += WalkSpeed * CurrentSpeedScale * DeltaTime * Along;
	}

	// 이웃이 밀어낸 양을 반영한다. Director가 이번 프레임에 누적해 둔 값이다.
	// 즉시 더하지 않고 목표를 향해 보간하는 이유: 서로 밀면 진동하기 때문이다.
	if (!FMath::IsNearlyZero(PendingAvoidNudge))
	{
		const float Target = LaneOffset + FMath::Clamp(PendingAvoidNudge, -MaxAvoidOffset, MaxAvoidOffset);
		const float HalfWidth = CurrentPath->GetLaneHalfWidth();
		LaneOffset = FMath::Clamp(
			FMath::FInterpTo(LaneOffset, Target, DeltaTime, AvoidInterpSpeed),
			-HalfWidth, HalfWidth);
		PendingAvoidNudge = 0.f;
	}

	// 스팟 판정은 일정 거리마다 다시 굴린다 (경로 진입 시 1회 판정의 부작용 제거).
	if (FMath::Abs(PathDistance - NextSpotCheckDistance) >= SpotCheckInterval)
	{
		NextSpotCheckDistance = PathDistance;
		RollForActivitySpot();
		if (CurrentActivity != ESubNpcActivity::Walking)
		{
			return;   // 스팟에 들어갔으면 이번 틱 이동은 건너뛴다
		}
	}

	const float Length = CurrentPath->GetPathLength();

	// 경로 끝(또는 역방향이면 시작)에 닿았으면 다음 경로로.
	if ((!bReversedOnPath && PathDistance >= Length) || (bReversedOnPath && PathDistance <= 0.f))
	{
		AdvanceToNextPath();
		return;
	}

	FVector Target = CurrentPath->GetLocationAtDistance(PathDistance, LaneOffset);
	FVector Dir = CurrentPath->GetDirectionAtDistance(PathDistance) * (bReversedOnPath ? -1.f : 1.f);

	// 지면에 붙여 세운다. 경로 높이를 대충 그려도 계단·경사로 위에 제대로 선다.
	// 가까운 개체만 한다 - 멀리 있는 사람의 발밑 몇 cm는 화면에서 안 보인다.
	if (SubNpcGroundSnap::Enabled != 0 &&
		(CurrentLodBand == ESubNpcLodBand::Near || CurrentLodBand == ESubNpcLodBand::Mid))
	{
		if (const UWorld* World = GetWorld())
		{
			const float HalfHeight = GetCapsuleComponent()
				? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
				: 88.f;

			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SubNpcGroundSnap), /*bTraceComplex=*/false, this);

			const FVector Foot = Target - FVector(0.f, 0.f, HalfHeight);
			const FVector Start = Foot + FVector(0.f, 0.f, SubNpcGroundSnap::TraceUp);
			const FVector End = Foot - FVector(0.f, 0.f, SubNpcGroundSnap::TraceDown);

			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				Target.Z = Hit.ImpactPoint.Z + HalfHeight;
			}
		}
	}

	// 경로를 막 갈아탔다면, 그때 어긋난 양을 조금씩 갚아나가며 따라붙는다.
	// 이게 없으면 전환 프레임에 위치가 뚝 끊겨서 순간이동으로 보인다.
	if (!PathHandoffOffset.IsNearlyZero())
	{
		// 지수 감쇠. DeltaTime이 커도(Far는 0.3초 틱) 0으로 튀지 않는다.
		PathHandoffOffset *= FMath::Exp(-SubNpcPathHandoff::BlendRate * DeltaTime);

		if (PathHandoffOffset.SizeSquared() < FMath::Square(SubNpcPathHandoff::SnapThreshold))
		{
			PathHandoffOffset = FVector::ZeroVector;
		}

		Target += PathHandoffOffset;
	}

	// [설계] 행인은 CharacterMovement를 쓰지 않고 위치를 직접 세팅한다.
	//
	// 처음에는 가까운 거리에서만 AddMovementInput을 쓰려 했는데, 두 가지 이유로 바꿨다.
	//   1) 컨트롤러가 없는 폰에서는 입력 경로가 통째로 무시된다 (bForce로도 실제 이동이 안 됨)
	//   2) 행인 수십 명에게 캐릭터 무브먼트(중력·바닥 스윕)를 돌리는 건 애초에 너무 비싸다
	//
	// 중력이 필요 없는 이유는 **경로의 높이가 곧 바닥**이기 때문이다. 스플라인을 지면에 맞춰
	// 그려두면 행인은 항상 그 위를 걷는다. 지형을 따라 오르내려야 하면 경로를 그렇게 그리면 된다.
	//
	// 플레이어와의 충돌은 그대로 살아 있다. 캡슐 콜리전은 무브먼트와 무관하게 동작하고,
	// 플레이어가 뛰어와 부딪히면 NotifyHit이 정상적으로 불린다.
	const bool bIsClose =
		(CurrentLodBand == ESubNpcLodBand::Near || CurrentLodBand == ESubNpcLodBand::Mid);

	// 회전은 살짝 보간한다. 스플라인 방향을 그대로 대입하면 코너에서 홱 돌아 로봇처럼 보인다.
	const FRotator TargetRot = Dir.Rotation();
	const FRotator NewRot = bIsClose
		? FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 8.f)
		: TargetRot;

	// [스윕을 끄는 이유]
	//
	// 스윕을 켜면 행인끼리 캡슐로 서로를 막는다. 같은 지점에 두 명이 등장하는 순간
	// 둘 다 영영 못 움직이고, PathDistance만 계속 올라 몸과 진행도가 어긋난다.
	// 인파가 많아질수록 이 정체는 반드시 생긴다.
	//
	// 스윕을 꺼도 **플레이어와의 부딪힘 반응은 그대로 동작한다.**
	// 히트 이벤트를 만드는 건 플레이어 쪽 무브먼트의 스윕이고, 행인 캡슐은 그냥
	// 막는 물체로 존재하기만 하면 되기 때문이다. E키 상호작용(오버랩 질의)도 마찬가지다.
	//
	// 잃는 건 "행인이 벽을 뚫을 수 있다" 하나뿐인데, 경로는 어차피 사람이 그리는 것이라
	// 벽을 통과하는 선을 안 그리면 그만이다. (레퍼런스 영상의 군중도 서로 통과한다)
	SetActorLocationAndRotation(Target, NewRot, /*bSweep=*/bSweepWhileMoving);
}

void ASubNpcCharacter::AdvanceToNextPath()
{
	// [방어] 길이가 사실상 0인 경로는 "도착"이 매 틱 성립해서 무한 재활용을 일으킨다.
	// 등장하자마자 회수되고 스포너가 다시 채우는 게 반복되어, 겉으로는
	// "안 움직이는데 계속 다른 사람으로 바뀐다"로 보인다.
	const float PathLength = CurrentPath ? CurrentPath->GetPathLength() : 0.f;
	if (PathLength < MinUsablePathLength)
	{
		// 같은 경로를 밟는 행인마다 찍히면 로그가 도배된다. 경로마다 한 번만 알린다.
		static TSet<TWeakObjectPtr<const ANpcCrowdPath>> ReportedShortPaths;
		if (!ReportedShortPaths.Contains(CurrentPath))
		{
			ReportedShortPaths.Add(CurrentPath);
			UE_LOG(LogTemp, Warning,
				TEXT("[Crowd] 경로 '%s'의 길이가 %.0fcm뿐입니다. 스플라인 점을 벌려 최소 %.0fcm 이상으로 만드세요."),
				*GetNameSafe(CurrentPath), PathLength, MinUsablePathLength);
		}

		CurrentPath = nullptr;
		CurrentActivity = ESubNpcActivity::Walking;
		SetMovementState(ENpcMovementState::Idle);
		return;
	}

	bool bNextReversed = false;
	ANpcCrowdPath* Next = CurrentPath->PickNextPath(bNextReversed);

	if (!Next)
	{
		// 막다른 길이다.
		//
		// [설계] 두 가지 끝이 있다.
		//   1) 퇴장구 (bDespawnAtEnd) - 맵 밖, 출입문, 골목 끝처럼 나가면 사라지는 게 맞는 곳.
		//      보이든 말든 즉시 회수한다. 사라지는 순간을 가리는 건 경로를 그리는 쪽의 몫이다.
		//      (문 안쪽으로 몇 미터 더 그려두면 된다)
		//   2) 그냥 끝난 구간 - 이어붙이는 걸 깜빡했거나 아직 안 만든 길.
		//      여기서 무조건 회수하면 플레이어 눈앞에서 사람이 뻥 사라지므로,
		//      보이는 거리면 방향만 돌려서 왔던 길을 되돌아가게 한다.
		const bool bIsExit = CurrentPath->IsDespawnAtEnd();

		if (!bIsExit &&
			(CurrentLodBand == ESubNpcLodBand::Near || CurrentLodBand == ESubNpcLodBand::Mid))
		{
			bReversedOnPath = !bReversedOnPath;

			// 끝점에 딱 붙어 있으면 다음 틱에 또 도착 판정이 나므로 살짝 안쪽으로 밀어준다.
			PathDistance = FMath::Clamp(PathDistance, 0.f, PathLength);
			PathDistance += bReversedOnPath ? -TurnAroundNudge : TurnAroundNudge;
			return;
		}

		// 퇴장구이거나, 안 보이는 거리다. 회수 대상으로 표시만 해둔다.
		//
		// [버그 수정] 예전에는 여기서 Director->ReturnToPool(this)를 바로 불렀다.
		// 그러면 Director가 활성 목록을 순회하는 도중에 그 목록에서 자기를 빼내게 되어,
		// 순회 인덱스가 어긋날 수 있다. LOD 갱신 쪽은 이미 모았다가 처리하도록
		// 되어 있었는데 이 경로만 보호를 안 받고 있었다.
		//
		// 지금은 표시만 하고 실제 회수는 Director가 안전한 시점에 처리한다.
		// 표시된 개체는 이동을 멈춰야 하므로 경로 참조를 끊어 둔다.
		bPendingRecycle = true;
		CurrentPath = nullptr;
		return;
	}

	// [수정] 예전에는 다음 경로의 0 지점(역방향이면 끝점)으로 무조건 보냈다.
	// 두 스플라인의 이음매가 딱 붙어 있지 않으면 그 간격만큼 순간이동한다.
	// 「걷다가 갑자기 사라졌다가 엉뚱한 데서 나타난다」의 정체가 이것이다.
	//
	// 지금은 지금 서 있는 자리에서 가장 가까운 지점으로 들어간다.
	// 이음매가 붙어 있으면 그 값이 사실상 0이라 동작이 같고,
	// 떨어져 있어도 튀는 거리를 최소로 줄인다.
	const ANpcCrowdPath* PrevPath = CurrentPath;
	const FVector HandoffLocation = GetActorLocation();

	CurrentPath = Next;
	bReversedOnPath = bNextReversed;

	float EnterDistance = bNextReversed ? Next->GetPathLength() : 0.f;
	const float GapSq = Next->FindClosestPointOnPath(HandoffLocation, EnterDistance);

	PathDistance = EnterDistance;
	NextSpotCheckDistance = PathDistance;

#if !UE_BUILD_SHIPPING
	// 그래도 멀다면 레벨 배치 문제다. 어느 경로 쌍이 벌어져 있는지 찍어준다.
	constexpr float AcceptableGapSq = 300.f * 300.f;
	if (GapSq > AcceptableGapSq)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Crowd] 경로 이음매가 %.0f cm 벌어져 있습니다: %s -> %s. 스플라인 끝점을 서로 붙여주세요."),
			FMath::Sqrt(GapSq), *GetNameSafe(PrevPath), *GetNameSafe(Next));
	}
#endif

	// 새 구간마다 차선을 조금 다시 굴린다. 계속 같은 오프셋이면 대열이 너무 반듯하다.
	const float HalfWidth = Next->GetLaneHalfWidth();
	LaneOffset = FMath::Clamp(LaneOffset + FMath::FRandRange(-30.f, 30.f), -HalfWidth, HalfWidth);

	// [부드러운 전환] 새 경로에서의 첫 위치와 지금 서 있는 자리의 차이를 기억해둔다.
	//
	// 다음 틱부터 이 오프셋이 더해진 자리에 서고, 그 값이 매 틱 0을 향해 줄어든다.
	// 그래서 사람이 새 경로로 **미끄러져 들어가지, 순간이동하지 않는다.**
	// 차선 오프셋을 다시 굴린 것까지 여기서 함께 흡수된다.
	const FVector NewTarget = Next->GetLocationAtDistance(PathDistance, LaneOffset);
	const FVector Gap = HandoffLocation - NewTarget;

	PathHandoffOffset = (Gap.SizeSquared() <= FMath::Square(SubNpcPathHandoff::MaxBlendDistance))
		? Gap
		: FVector::ZeroVector;   // 너무 멀면 흡수를 포기한다 (위 경고 로그가 이미 알려준다)

	RollForActivitySpot();
}

// ---------------------------------------------------------------------------
// 활동 스팟
// ---------------------------------------------------------------------------

void ASubNpcCharacter::RollForActivitySpot()
{
	if (!CurrentPath || OccupiedSpot || GroupLeader.IsValid())
	{
		// 그룹 추종 중인 개체는 멈추지 않는다 (리더만 멈추면 대열이 깨진다).
		return;
	}

	// [버그 수정] 예전에는 배열을 앞에서부터 훑고 첫 성공에서 멈췄다.
	// 그래서 한 경로에 스팟이 여러 개면 0번이 압도적으로 자주 걸리고,
	// 뒤쪽 스팟은 앞이 꽉 찼을 때만 쓰였다. 순서를 섞어서 고르게 분산시킨다.
	TArray<ANpcActivitySpot*> Candidates;
	Candidates.Reserve(CurrentPath->GetActivitySpots().Num());
	for (ANpcActivitySpot* Spot : CurrentPath->GetActivitySpots())
	{
		if (Spot)
		{
			Candidates.Add(Spot);
		}
	}
	for (int32 i = Candidates.Num() - 1; i > 0; --i)
	{
		Candidates.Swap(i, FMath::RandRange(0, i));
	}

	const FVector MyLocation = GetActorLocation();
	const float EngageRadiusSq = FMath::Square(SpotEngageRadius);

	for (ANpcActivitySpot* Spot : Candidates)
	{
		if (!Spot->HasRoom())
		{
			continue;
		}

		// [버그 수정] 거리 조건이 없으면 이미 지나쳐 온 스팟에도 당첨돼서
		// 뒤로 순간이동하는 것처럼 보인다.
		if (FVector::DistSquared(Spot->GetStandLocation(), MyLocation) > EngageRadiusSq)
		{
			continue;
		}

		if (FMath::FRand() > Spot->GetEngageChance())
		{
			continue;
		}

		FNpcActivityOption Option;
		if (!Spot->PickActivity(Option))
		{
			continue;
		}

		if (!Spot->TryOccupy(this))
		{
			continue;
		}

		BeginSpotActivity(Spot, Option);
		return;
	}
}

bool ASubNpcCharacter::TryEnterNearbySpot()
{
	RollForActivitySpot();
	return CurrentActivity == ESubNpcActivity::AtSpot;
}

bool ASubNpcCharacter::TryEnterSpot(ANpcActivitySpot* Spot)
{
	if (!Spot || OccupiedSpot || !Spot->HasRoom())
	{
		return false;
	}

	FNpcActivityOption Option;
	if (!Spot->PickActivity(Option))
	{
		return false;
	}

	if (!Spot->TryOccupy(this))
	{
		return false;
	}

	BeginSpotActivity(Spot, Option);
	return true;
}

void ASubNpcCharacter::LeaveSpotIfAny()
{
	if (CurrentActivity == ESubNpcActivity::AtSpot || OccupiedSpot)
	{
		EndSpotActivity();
	}
}

void ASubNpcCharacter::BeginSpotActivity(ANpcActivitySpot* Spot, const FNpcActivityOption& Option)
{
	OccupiedSpot = Spot;
	CurrentActivity = ESubNpcActivity::AtSpot;

	// 단계 배열을 이어서 재생하려면 고른 행동을 들고 있어야 한다.
	CurrentSpotOption = Option;
	CurrentSpotStep = -1;   // -1 = 첫 동작(Option.AnimId)

	// 스팟 앞에 세우고, 그 행동이 바라봐야 할 지점을 향하게 한다.
	// "무슨 애니메이션 + 어디를 보는가" 이 두 개가 스팟 연출의 전부다.
	const FVector Stand = Spot->GetStandLocation();
	const FVector LookAt = Spot->GetLookAtLocation(Option);

	FVector Facing = LookAt - Stand;
	Facing.Z = 0.f;
	if (!Facing.Normalize())
	{
		Facing = GetActorForwardVector();
	}

	SetActorLocationAndRotation(Stand, Facing.Rotation(), /*bSweep=*/false);

	SetMovementState(ENpcMovementState::Interacting);

	PlaySpotStep(Option.AnimId, Option.MinDuration, Option.MaxDuration, Option.bLoop);
}

void ASubNpcCharacter::PlaySpotStep(FName AnimId, float MinDuration, float MaxDuration, bool bLoop)
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	ActivityEndTime = Now + FMath::FRandRange(MinDuration, FMath::Max(MinDuration, MaxDuration));

	if (!AnimId.IsNone())
	{
		PlayNpcAnim(AnimId, bLoop);
	}
	else
	{
		// 애니메이션 없는 단계 = 그냥 그 시간만큼 서 있는다.
		StopNpcAnim();
	}
}

void ASubNpcCharacter::AdvanceSpotStep()
{
	const int32 NextStep = CurrentSpotStep + 1;
	if (!CurrentSpotOption.ExtraSteps.IsValidIndex(NextStep))
	{
		EndSpotActivity();
		return;
	}

	CurrentSpotStep = NextStep;
	const FNpcActivityStep& Step = CurrentSpotOption.ExtraSteps[NextStep];
	PlaySpotStep(Step.AnimId, Step.MinDuration, Step.MaxDuration, Step.bLoop);
}

void ASubNpcCharacter::EndSpotActivity()
{
	if (OccupiedSpot)
	{
		OccupiedSpot->ReleaseOccupancy(this);
		OccupiedSpot = nullptr;
	}

	CurrentSpotOption = FNpcActivityOption();
	CurrentSpotStep = -1;

	StopNpcAnim();
	CurrentActivity = ESubNpcActivity::Walking;
	SetMovementState(ENpcMovementState::Walk);

	// 방금 떠난 스팟에 곧바로 다시 걸리지 않도록 판정 기준을 현재 위치로 옮긴다.
	NextSpotCheckDistance = PathDistance;
}

// ---------------------------------------------------------------------------
// 플레이어 반응
// ---------------------------------------------------------------------------

void ASubNpcCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	const APawn* OtherPawn = Cast<APawn>(Other);
	if (!OtherPawn || !OtherPawn->IsPlayerControlled())
	{
		return;
	}

	ReactToPlayerBump(Other, OtherPawn->GetVelocity().Size2D());
}

void ASubNpcCharacter::ReactToPlayerBump(AActor* Player, float ImpactSpeed)
{
	const UWorld* World = GetWorld();
	if (!World || !Player)
	{
		return;
	}

	// 천천히 스치는 건 무시. 안 그러면 인파를 헤치고 걸을 때마다 전원이 화를 낸다.
	if (ImpactSpeed < BumpSpeedThreshold)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastReactionTime < ReactionCooldown || CurrentActivity == ESubNpcActivity::Reacting)
	{
		return;
	}

	// 반응 종류를 아키타입 가중치로 고른다. 같은 상황에서도 사람마다 다르게 반응해야 재밌다.
	const TArray<float> Weights = { Archetype.DodgeWeight, Archetype.AngryWeight, Archetype.SurprisedWeight };
	const int32 Picked = NpcWeightedPick::PickIndex(Weights);
	if (Picked == INDEX_NONE)
	{
		return;
	}

	static const ESubNpcReaction ReactionByIndex[] = { ESubNpcReaction::Dodge, ESubNpcReaction::Angry, ESubNpcReaction::Surprised };
	const ESubNpcReaction Reaction = ReactionByIndex[Picked];
	CurrentReaction = Reaction;

	LastReactionTime = Now;
	LastBumpTime = Now;

	// ---- 부딪힌 방향 판정 ---------------------------------------------------
	//
	// 플레이어가 내 오른쪽에 있으면 오른쪽에서 부딪힌 것이므로 **왼쪽으로** 밀린다.
	// 그 반대도 마찬가지다. 좌우가 반대로 나오면 여기 부호만 뒤집으면 된다.
	const FVector ToPlayerDir = Player->GetActorLocation() - GetActorLocation();
	const float SideDot = FVector::DotProduct(ToPlayerDir, GetActorRightVector());

	// 정확히 정면이면 판정이 흔들린다. 이름 해시로 한쪽을 정해 매번 같게 만든다.
	const bool bPushedLeft = (FMath::Abs(SideDot) > 1.f)
		? (SideDot > 0.f)
		: ((GetTypeHash(GetFName()) & 1) != 0);

	// 왼쪽으로 밀리면 차선 오프셋은 음수 방향으로 간다.
	const float PushSign = bPushedLeft ? -1.f : 1.f;

	// 걷던 중이면 "비켜서는" 동작, 서 있었으면 "밀리는" 동작이 자연스럽다.
	const bool bWasWalking = (CurrentActivity == ESubNpcActivity::Walking);

	// [개선] 스팟에서 뭔가 하던 중이었으면 반응이 끝난 뒤 하던 일로 되돌아간다.
	//
	// 예전에는 점유를 즉시 풀고 걷기로 빠져나갔다. 그래서 앉아서 책을 보던 사람이
	// 한 번 부딪히면 벌떡 일어나 걸어가 버렸다. 놀란 뒤 다시 앉는 쪽이 자연스럽고,
	// 스팟이 비지 않으므로 그 자리를 다른 개체가 가로채지도 않는다.
	bResumeSpotAfterReaction = (CurrentActivity == ESubNpcActivity::AtSpot && OccupiedSpot != nullptr);

	CurrentActivity = ESubNpcActivity::Reacting;
	ActivityEndTime = Now + ReactionDuration;

	FName AnimId;
	switch (Reaction)
	{
	case ESubNpcReaction::Dodge:
		{
			// 방향 × 상태(걷는 중/서 있음)로 네 가지 중 하나를 고른다.
			// 비어 있으면 기존 DodgeAnimId로 대체한다 - 애니메이션을 하나만
			// 만들어도 동작하고, 나중에 하나씩 채워 넣을 수 있다.
			const FName Specific = bWasWalking
				? (bPushedLeft ? Archetype.DodgeLeftAnimId : Archetype.DodgeRightAnimId)
				: (bPushedLeft ? Archetype.ShoveLeftAnimId : Archetype.ShoveRightAnimId);

			AnimId = Specific.IsNone() ? Archetype.DodgeAnimId : Specific;

			// 밀린 쪽으로 실제로 몸을 옮긴다. 애니메이션만 재생하고 제자리면
			// "맞았는데 안 밀리는" 어색함이 남는다.
			DodgeTargetOffset = LaneOffset + PushSign * DodgeSideStep;
		}
		break;

	case ESubNpcReaction::Angry:
		AnimId = Archetype.AngryAnimId;
		// 화를 내도 몸은 부딪힌 만큼 밀린다. 물리적 반응과 감정 반응은 별개다.
		DodgeTargetOffset = LaneOffset + PushSign * DodgeSideStep * 0.4f;
		// 화낼 때는 멈춰서 플레이어를 쳐다본다.
		FaceTowards(Player);
		break;

	case ESubNpcReaction::Surprised:
	default:
		AnimId = Archetype.SurprisedAnimId;
		DodgeTargetOffset = LaneOffset + PushSign * DodgeSideStep * 0.6f;
		FaceTowards(Player);
		break;
	}

	if (!AnimId.IsNone())
	{
		PlayNpcAnim(AnimId, /*bLoop=*/false);
	}

	SetMovementState(ENpcMovementState::Interacting);

	// 블루프린트 연출 훅. 몽타주·사운드·이펙트를 붙이고 싶을 때 쓴다.
	OnBumpReaction(Reaction, bPushedLeft, bWasWalking);

	OnBumped.Broadcast(Player);
}

void ASubNpcCharacter::EndReaction()
{
	StopNpcAnim();
	CurrentReaction = ESubNpcReaction::None;

	// [개선] 하던 스팟 행동이 있었으면 그리로 되돌아간다.
	// 점유는 계속 들고 있었으므로 자리를 뺏겼을 걱정은 없다.
	if (bResumeSpotAfterReaction && OccupiedSpot)
	{
		bResumeSpotAfterReaction = false;
		CurrentActivity = ESubNpcActivity::AtSpot;
		SetMovementState(ENpcMovementState::Interacting);

		// 처음부터 다시 하지 않고 하던 단계를 이어서 재생한다.
		const int32 Step = CurrentSpotStep;
		if (Step < 0)
		{
			PlaySpotStep(CurrentSpotOption.AnimId, CurrentSpotOption.MinDuration,
				CurrentSpotOption.MaxDuration, CurrentSpotOption.bLoop);
		}
		else if (CurrentSpotOption.ExtraSteps.IsValidIndex(Step))
		{
			const FNpcActivityStep& S = CurrentSpotOption.ExtraSteps[Step];
			PlaySpotStep(S.AnimId, S.MinDuration, S.MaxDuration, S.bLoop);
		}
		else
		{
			EndSpotActivity();
		}
		return;
	}

	bResumeSpotAfterReaction = false;
	CurrentActivity = ESubNpcActivity::Walking;
	SetMovementState(ENpcMovementState::Walk);

	// 비켰던 차선은 그대로 두고 계속 걷는다 (원래 자리로 홱 돌아오면 어색하다).
	DodgeTargetOffset = LaneOffset;
}

// ---------------------------------------------------------------------------
// 상호작용 (한 줄 대사)
// ---------------------------------------------------------------------------

bool ASubNpcCharacter::CanInteract_Implementation(AActor* Interactor) const
{
	if (!bAllowTalk || CurrentActivity == ESubNpcActivity::Pooled)
	{
		return false;
	}

	// 멀리 있는(저LOD) 행인에게는 프롬프트를 띄우지 않는다.
	if (CurrentLodBand == ESubNpcLodBand::Far || CurrentLodBand == ESubNpcLodBand::Culled)
	{
		return false;
	}

	return Super::CanInteract_Implementation(Interactor);
}

FText ASubNpcCharacter::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return NSLOCTEXT("Npc", "SubNpcTalkPrompt", "E - 말 걸기");
}

void ASubNpcCharacter::Interact_Implementation(AActor* Interactor)
{
	Super::Interact_Implementation(Interactor);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
	if (!Dialogue || Dialogue->IsDialogueActive())
	{
		return;
	}

	FText Line;
	if (!PickBarkLine(Line))
	{
		return;
	}

	// 행인은 대화 트리를 갖지 않는다. 한 줄만 띄우고 끝.
	// DataTable도, 이벤트 등록도 필요 없어서 행인이 몇 명이든 데이터 비용이 늘지 않는다.
	FaceTowards(Interactor);

	const FText Speaker = Archetype.DisplayName.IsEmpty()
		? NSLOCTEXT("Npc", "SubNpcDefaultName", "행인")
		: Archetype.DisplayName;

	Dialogue->StartSimpleLines(Speaker, { Line }, this);
}

bool ASubNpcCharacter::PickBarkLine(FText& OutLine) const
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// 방금 부딪혔던 상대가 말을 걸면 다른 대사가 나온다.
	// 비용은 배열 하나인데 "얘가 나를 기억한다"는 인상이 커서 가성비가 제일 좋다.
	const bool bRecentlyBumped = (Now - LastBumpTime) < BumpMemoryDuration;
	const TArray<FText>& Pool = (bRecentlyBumped && Archetype.BumpedBarks.Num() > 0)
		? Archetype.BumpedBarks
		: Archetype.IdleBarks;

	if (Pool.Num() == 0)
	{
		return false;
	}

	OutLine = Pool[FMath::RandRange(0, Pool.Num() - 1)];
	return true;
}

// ---------------------------------------------------------------------------
// 그룹
// ---------------------------------------------------------------------------

void ASubNpcCharacter::SetGroupLeader(ASubNpcCharacter* Leader, float FollowGap, float InLaneOffset)
{
	GroupLeader = Leader;
	GroupFollowGap = FollowGap;
	LaneOffset = InLaneOffset;

	if (Leader)
	{
		// 리더와 같은 경로·방향을 공유해야 간격 계산이 맞는다.
		CurrentPath = Leader->CurrentPath;
		bReversedOnPath = Leader->bReversedOnPath;
		PathDistance = Leader->PathDistance - FollowGap;
		WalkSpeed = Leader->WalkSpeed;
	}
}
