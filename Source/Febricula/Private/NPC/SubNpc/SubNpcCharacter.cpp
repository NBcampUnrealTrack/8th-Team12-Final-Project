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

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// 메타휴먼 기준 이게 제일 큰 절약이다: 화면에 안 보이면 본 계산 자체를 하지 않는다.
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

		// 애니메이션 업데이트 레이트 최적화 - 멀수록 포즈 갱신 빈도를 자동으로 낮춘다.
		MeshComp->bEnableUpdateRateOptimizations = true;
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
	ApplyLodBand(ESubNpcLodBand::Far);

	UE_CLOG(bDebugCrowdLog, LogTemp, Warning,
		TEXT("[CrowdDbg] Activate %s  Path=%s  Len=%.0f  Dist=%.0f  Speed=%.0f  Loc=%s"),
		*GetName(), *GetNameSafe(StartPath), StartPath->GetPathLength(),
		PathDistance, WalkSpeed, *GetActorLocation().ToCompactString());

	// 블루프린트에서 외형(메시/머티리얼/의상)을 갈아끼울 기회.
	OnArchetypeApplied(Archetype);

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
	GroupFollowGap = 0.f;
	ActivityEndTime = 0.f;
	DodgeTargetOffset = 0.f;

	// [중요] 부딪힘 기록도 반드시 지운다.
	// 이 액터는 풀에서 재사용되므로, 안 지우면 방금 회수된 개체의 기록을 물려받은 채
	// 새 행인이 등장해서 "방금 부딪혀놓고..." 같은 말을 한다.
	// 플레이어 입장에서는 처음 보는 사람이 없던 일을 따지는 셈이라 바로 티가 난다.
	LastBumpTime = -1000.f;
	LastReactionTime = -1000.f;
}

// ---------------------------------------------------------------------------
// LOD
// ---------------------------------------------------------------------------

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
		SetActorTickInterval(0.f);
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(0);                       // 0 = 자동 LOD에 맡김
			MeshComp->SetCastShadow(true);
			MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		}
		break;

	case ESubNpcLodBand::Mid:
		SetActorTickInterval(MidTickInterval);
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(MidForcedLod + 1);        // SetForcedLOD는 1-based (0은 자동)
			// 그림자는 메타휴먼에서 가장 비싼 항목 중 하나다. 중간 거리부터 끈다.
			MeshComp->SetCastShadow(false);
		}
		break;

	case ESubNpcLodBand::Far:
		SetActorTickInterval(FarTickInterval);
		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(true);
			MeshComp->bPauseAnims = false;
			MeshComp->SetForcedLOD(FarForcedLod + 1);
			MeshComp->SetCastShadow(false);
		}
		break;

	case ESubNpcLodBand::Culled:
	default:
		SetActorTickInterval(FarTickInterval);
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

// ---------------------------------------------------------------------------
// 이동
// ---------------------------------------------------------------------------

void ASubNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bDebugCrowdLog)
	{
		DebugLogTimer += DeltaTime;
	}
	if (bDebugCrowdLog && DebugLogTimer >= 1.f)
	{
		DebugLogTimer = 0.f;
		UE_LOG(LogTemp, Warning,
			TEXT("[CrowdDbg] TICK %s  Act=%d  LOD=%d  Dist=%.0f  Loc=%s  Path=%s  MoveTick=%d  Mode=%d"),
			*GetName(),
			static_cast<int32>(CurrentActivity),
			static_cast<int32>(CurrentLodBand),
			PathDistance,
			*GetActorLocation().ToCompactString(),
			*GetNameSafe(CurrentPath),
			GetCharacterMovement() ? (GetCharacterMovement()->IsComponentTickEnabled() ? 1 : 0) : -1,
			GetCharacterMovement() ? static_cast<int32>(GetCharacterMovement()->MovementMode.GetValue()) : -1);
	}

	if (CurrentActivity == ESubNpcActivity::Pooled)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	switch (CurrentActivity)
	{
	case ESubNpcActivity::AtSpot:
		if (Now >= ActivityEndTime)
		{
			EndSpotActivity();
		}
		return;

	case ESubNpcActivity::Reacting:
		if (Now >= ActivityEndTime)
		{
			EndReaction();
		}
		else
		{
			// 피하기 중에는 차선 오프셋을 목표치로 부드럽게 밀어 옆으로 비킨다.
			LaneOffset = FMath::FInterpTo(LaneOffset, DodgeTargetOffset, DeltaTime, 4.f);
			TickPathFollow(DeltaTime);
		}
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

	// 그룹으로 다니는 경우 앞사람의 진행도에서 간격만큼 뒤를 따라간다.
	// 각자 독립적으로 걸으면 신호에 걸릴 때마다 대열이 흩어진다.
	if (ASubNpcCharacter* Leader = GroupLeader.Get())
	{
		if (Leader->CurrentPath == CurrentPath && !Leader->IsPooled())
		{
			PathDistance = Leader->PathDistance - GroupFollowGap;
		}
		else
		{
			// 리더가 다른 경로로 갔거나 회수됐으면 그룹을 해산하고 혼자 걷는다.
			GroupLeader = nullptr;
		}
	}
	else
	{
		PathDistance += WalkSpeed * DeltaTime * (bReversedOnPath ? -1.f : 1.f);
	}

	const float Length = CurrentPath->GetPathLength();

	// 경로 끝(또는 역방향이면 시작)에 닿았으면 다음 경로로.
	if ((!bReversedOnPath && PathDistance >= Length) || (bReversedOnPath && PathDistance <= 0.f))
	{
		AdvanceToNextPath();
		return;
	}

	const FVector Target = CurrentPath->GetLocationAtDistance(PathDistance, LaneOffset);
	FVector Dir = CurrentPath->GetDirectionAtDistance(PathDistance) * (bReversedOnPath ? -1.f : 1.f);

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
		UE_LOG(LogTemp, Error,
			TEXT("[Crowd] 경로 '%s'의 길이가 %.0fcm뿐입니다. 스플라인 점을 벌려 최소 %.0fcm 이상으로 만드세요. "
				 "이 행인은 제자리에 세웁니다."),
			*GetNameSafe(CurrentPath), PathLength, MinUsablePathLength);

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

		// 퇴장구이거나, 안 보이는 거리다. 회수해서 다른 곳에 재활용한다.
		if (const UWorld* World = GetWorld())
		{
			if (USubNpcDirectorSubsystem* Director = World->GetSubsystem<USubNpcDirectorSubsystem>())
			{
				Director->ReturnToPool(this);
				return;
			}
		}
		DeactivateToPool();
		return;
	}

	CurrentPath = Next;
	bReversedOnPath = bNextReversed;
	PathDistance = bNextReversed ? Next->GetPathLength() : 0.f;

	// 새 구간마다 차선을 조금 다시 굴린다. 계속 같은 오프셋이면 대열이 너무 반듯하다.
	const float HalfWidth = Next->GetLaneHalfWidth();
	LaneOffset = FMath::Clamp(LaneOffset + FMath::FRandRange(-30.f, 30.f), -HalfWidth, HalfWidth);

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

	for (ANpcActivitySpot* Spot : CurrentPath->GetActivitySpots())
	{
		if (!Spot || !Spot->HasRoom())
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

void ASubNpcCharacter::BeginSpotActivity(ANpcActivitySpot* Spot, const FNpcActivityOption& Option)
{
	OccupiedSpot = Spot;
	CurrentActivity = ESubNpcActivity::AtSpot;

	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	ActivityEndTime = Now + FMath::FRandRange(Option.MinDuration, FMath::Max(Option.MinDuration, Option.MaxDuration));

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

	if (!Option.AnimId.IsNone())
	{
		PlayNpcAnim(Option.AnimId, Option.bLoop);
	}
}

void ASubNpcCharacter::EndSpotActivity()
{
	if (OccupiedSpot)
	{
		OccupiedSpot->ReleaseOccupancy(this);
		OccupiedSpot = nullptr;
	}

	StopNpcAnim();
	CurrentActivity = ESubNpcActivity::Walking;
	SetMovementState(ENpcMovementState::Walk);
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

	LastReactionTime = Now;
	LastBumpTime = Now;
	CurrentActivity = ESubNpcActivity::Reacting;
	ActivityEndTime = Now + ReactionDuration;

	// 스팟에서 뭔가 하던 중이었으면 놓아준다 (부딪혔는데 계속 메뉴를 보고 있으면 이상하다).
	if (OccupiedSpot)
	{
		OccupiedSpot->ReleaseOccupancy(this);
		OccupiedSpot = nullptr;
	}

	FName AnimId;
	switch (Reaction)
	{
	case ESubNpcReaction::Dodge:
		AnimId = Archetype.DodgeAnimId;
		{
			// 플레이어 반대쪽으로 비킨다.
			const FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
			const float Side = FVector::DotProduct(ToPlayer, GetActorRightVector()) > 0.f ? -1.f : 1.f;
			DodgeTargetOffset = LaneOffset + Side * DodgeSideStep;
		}
		break;

	case ESubNpcReaction::Angry:
		AnimId = Archetype.AngryAnimId;
		DodgeTargetOffset = LaneOffset;
		// 화낼 때는 멈춰서 플레이어를 쳐다본다.
		FaceTowards(Player);
		break;

	case ESubNpcReaction::Surprised:
	default:
		AnimId = Archetype.SurprisedAnimId;
		DodgeTargetOffset = LaneOffset;
		FaceTowards(Player);
		break;
	}

	if (!AnimId.IsNone())
	{
		PlayNpcAnim(AnimId, /*bLoop=*/false);
	}

	SetMovementState(ENpcMovementState::Interacting);
	OnBumped.Broadcast(Player);
}

void ASubNpcCharacter::EndReaction()
{
	StopNpcAnim();
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
