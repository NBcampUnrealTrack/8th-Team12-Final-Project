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
		// RVO 회피는 마리당 비용이 커서 군중에는 못 쓴다.
		// 행인끼리 겹치는 문제는 차선 오프셋(LaneOffset)으로 푼다.
		Move->bUseRVOAvoidance = false;

		// 경로가 정해져 있으므로 네비게이션 관련 처리도 전부 끈다.
		Move->SetMovementMode(MOVE_Walking);
		Move->bUseControllerDesiredRotation = false;
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 360.f, 0.f);
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

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = WalkSpeed;
	}

	// 시작 위치로 순간이동시킨 뒤 보이게 한다 (순서가 반대면 옛 위치에서 한 프레임 깜빡인다).
	const FVector StartLoc = StartPath->GetLocationAtDistance(PathDistance, LaneOffset);
	const FVector Dir = StartPath->GetDirectionAtDistance(PathDistance) * (bReversedOnPath ? -1.f : 1.f);
	SetActorLocationAndRotation(StartLoc, Dir.Rotation());

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);

	CurrentActivity = ESubNpcActivity::Walking;
	SetMovementState(ENpcMovementState::Walk);

	// LOD는 Director가 다음 갱신에서 정한다. 그때까지는 가장 싼 상태로 둔다.
	CurrentLodBand = ESubNpcLodBand::Culled;
	ApplyLodBand(ESubNpcLodBand::Far);

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

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetComponentTickEnabled(false);
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
	UCharacterMovementComponent* Move = GetCharacterMovement();

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
		if (Move) { Move->SetComponentTickEnabled(true); }
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
		if (Move) { Move->SetComponentTickEnabled(true); }
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
		// 먼 거리에서는 캐릭터 무브먼트(중력·캡슐 스윕)를 끄고 위치만 직접 옮긴다.
		// 어차피 안 보이는 거리에서 물리적으로 정확할 이유가 없고, 이게 CPU 절약의 대부분이다.
		if (Move)
		{
			Move->StopMovementImmediately();
			Move->SetComponentTickEnabled(false);
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
		if (Move)
		{
			Move->StopMovementImmediately();
			Move->SetComponentTickEnabled(false);
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

	if (CurrentLodBand == ESubNpcLodBand::Near || CurrentLodBand == ESubNpcLodBand::Mid)
	{
		// 가까울 때만 캐릭터 무브먼트를 쓴다. 플레이어와의 충돌·중력이 필요한 거리다.
		const FVector ToTarget = Target - GetActorLocation();
		FVector MoveDir = ToTarget;
		MoveDir.Z = 0.f;
		if (MoveDir.Normalize())
		{
			AddMovementInput(MoveDir, 1.f);
		}
	}
	else
	{
		// 멀면 위치를 직접 세팅한다 (스윕 없음). 지형을 정확히 밟지 않아도 티가 안 나는 거리다.
		SetActorLocationAndRotation(Target, Dir.Rotation(), /*bSweep=*/false);
	}
}

void ASubNpcCharacter::AdvanceToNextPath()
{
	bool bNextReversed = false;
	ANpcCrowdPath* Next = CurrentPath ? CurrentPath->PickNextPath(bNextReversed) : nullptr;

	if (!Next)
	{
		// 막다른 길 = 이 행인의 역할이 끝났다. 풀로 돌려보낸다.
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

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}

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
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
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

		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->MaxWalkSpeed = WalkSpeed;
		}
	}
}
