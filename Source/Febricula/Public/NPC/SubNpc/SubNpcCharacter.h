// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인(서브 NPC).
//
// ABaseNpcCharacter의 식별/애니메이션/상호작용을 그대로 물려받고,
// 군중에 필요한 세 가지를 더한다:
//   1) 스플라인 경로 추종 (NavMesh·BehaviorTree 안 씀)
//   2) 활동 스팟에서의 제스처
//   3) 플레이어와 부딪혔을 때의 반응
//
// [수명 관리]
//   이 액터는 절대 런타임에 Destroy되지 않는다. USubNpcDirectorSubsystem이 풀로 관리하며
//   Activate / Deactivate만 반복한다. 메타휴먼은 스폰/소멸 비용이 커서
//   그때그때 만들었다 지우면 프레임이 튀고 메모리가 파편화된다.
//   대신 Deactivate에서 **모든 참조와 타이머를 확실히 끊는 것**이 이 클래스의 계약이다.

#pragma once

#include "CoreMinimal.h"
#include "NPC/BaseNpcCharacter.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "SubNpcCharacter.generated.h"

class ANpcCrowdPath;
class ANpcActivitySpot;

UCLASS()
class FEBRICULA_API ASubNpcCharacter : public ABaseNpcCharacter
{
	GENERATED_BODY()

public:
	ASubNpcCharacter();

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//~ Begin IInteractable
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	//~ End IInteractable

	// ---- 풀 수명 -----------------------------------------------------------

	/**
	 * 풀에서 꺼내 경로 위에 세운다.
	 * 이전 개체의 상태가 남아 있으면 안 되므로, 여기서 모든 런타임 값을 새로 세팅한다.
	 */
	void ActivateFromPool(const FSubNpcArchetypeRow& Archetype, ANpcCrowdPath* StartPath, float StartDistance, bool bReversed);

	/**
	 * 풀로 되돌린다. 숨기고, 틱을 끄고, **들고 있던 모든 외부 참조를 끊는다.**
	 * 스팟 점유 해제와 타이머 정리가 여기서 반드시 일어나야 누수가 없다.
	 */
	void DeactivateToPool();

	UFUNCTION(BlueprintPure, Category = "Crowd")
	bool IsPooled() const { return CurrentActivity == ESubNpcActivity::Pooled; }

	UFUNCTION(BlueprintPure, Category = "Crowd")
	ESubNpcActivity GetActivity() const { return CurrentActivity; }

	// ---- LOD ---------------------------------------------------------------

	/**
	 * 상세도 단계를 적용한다. Director가 주기적으로 호출한다.
	 *
	 * 단계별로 바꾸는 것:
	 *   틱 간격 / 애니메이션 포즈 계산 방식 / 강제 LOD / 그림자 / 이동 처리 방식
	 * 같은 값이 다시 들어오면 아무것도 하지 않는다 (매 갱신마다 컴포넌트를 건드리면 그게 더 비싸다).
	 */
	void ApplyLodBand(ESubNpcLodBand NewBand);

	UFUNCTION(BlueprintPure, Category = "Crowd|LOD")
	ESubNpcLodBand GetLodBand() const { return CurrentLodBand; }

	// ---- 경로 --------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	ANpcCrowdPath* GetCurrentPath() const { return CurrentPath; }

	/** 그룹으로 다닐 때 앞사람. 있으면 앞사람의 경로 진행도를 따라간다 */
	void SetGroupLeader(ASubNpcCharacter* Leader, float FollowGap, float LaneOffset);

	// ---- 반응 --------------------------------------------------------------

	/**
	 * 플레이어와 부딪혔을 때 호출. 아키타입 가중치로 반응을 골라 재생한다.
	 * 쿨다운 중이거나 이미 반응 중이면 무시한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Reaction")
	void ReactToPlayerBump(AActor* Player, float ImpactSpeed);

	UPROPERTY(BlueprintAssignable, Category = "Crowd|Reaction")
	FOnNpcInteracted OnBumped;

protected:
	virtual void BeginPlay() override;

	/**
	 * 풀에서 꺼내 아키타입을 적용한 직후 불리는 블루프린트 훅.
	 *
	 * 여기서 AppearanceTag를 보고 메시·머티리얼·의상을 바꾸면 한 클래스로
	 * 여러 유형의 행인을 만들 수 있다. 풀은 하나만 유지되므로 메모리가 늘지 않는다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crowd")
	void OnArchetypeApplied(const FSubNpcArchetypeRow& InArchetype);

	/** 풀로 회수되기 직전 훅. 블루프린트에서 만든 것(파티클, 소품 등)을 여기서 정리한다 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crowd")
	void OnReturnedToPool();

	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	/** 경로를 따라 전진. LOD에 따라 이동 방식이 달라진다 */
	void TickPathFollow(float DeltaTime);

	/** 현재 경로가 끝났을 때 다음 경로를 고르거나 회수 요청 */
	void AdvanceToNextPath();

	/** 이 경로 구간의 스팟들에 대해 "들를까" 판정 */
	void RollForActivitySpot();

	/** 스팟 행동 시작/종료 */
	void BeginSpotActivity(ANpcActivitySpot* Spot, const FNpcActivityOption& Option);
	void EndSpotActivity();

	/** 반응 종료 후 다시 걷기 */
	void EndReaction();

	/** 타이머·애니메이션·스팟 점유 등 진행 중인 모든 것을 정리한다 (풀 회수·EndPlay 공용) */
	void ClearRuntimeState();

	/** 지금 상황에 맞는 대사 한 줄을 고른다 */
	bool PickBarkLine(FText& OutLine) const;

	// ---- 설정 --------------------------------------------------------------

	/**
	 * 상호작용(E)으로 한 줄 대사를 보여줄지.
	 *
	 * 행인에게 말을 걸 수 있게 하면 "살아있는 거리" 느낌이 크게 올라간다.
	 * 대사 트리가 아니라 한 줄짜리라 데이터 비용도 거의 없다.
	 * 반대로 군중 밀도가 높은 구역에서는 E 프롬프트가 계속 떠서 방해가 되므로,
	 * 구역별로 끌 수 있게 옵션으로 뒀다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Interaction")
	bool bAllowTalk = true;

	/** 부딪힌 뒤 이 시간(초) 안에 말을 걸면 다른 대사가 나온다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Interaction", meta = (ClampMin = "0.0"))
	float BumpMemoryDuration = 20.f;

	/** 이 속도(cm/s) 이상으로 부딪혀야 반응한다. 천천히 스치는 건 무시 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Reaction", meta = (ClampMin = "0.0"))
	float BumpSpeedThreshold = 300.f;

	/** 반응 후 다시 반응하기까지의 쿨다운 (초). 없으면 벽에 끼었을 때 무한 반복된다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Reaction", meta = (ClampMin = "0.0"))
	float ReactionCooldown = 4.f;

	/** 반응 지속 시간 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Reaction", meta = (ClampMin = "0.0"))
	float ReactionDuration = 2.f;

	/** 피하기 반응에서 옆으로 비키는 거리 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Reaction")
	float DodgeSideStep = 90.f;

	/**
	 * 지나가는 플레이어를 힐끗 보는 확률 (0~1). 매 경로 진입마다 판정.
	 * 비용이 거의 없는데 "나를 인식한다"는 느낌이 커서 가성비가 제일 좋은 연출이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlanceChance = 0.2f;

	/** LOD 단계별 틱 간격 (초). Near는 매 프레임 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	float MidTickInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	float FarTickInterval = 0.3f;

	/** Mid / Far에서 강제할 스켈레탈 메시 LOD 인덱스 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	int32 MidForcedLod = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	int32 FarForcedLod = 3;

	// ---- 런타임 ------------------------------------------------------------

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Crowd")
	ESubNpcActivity CurrentActivity = ESubNpcActivity::Pooled;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Crowd|LOD")
	ESubNpcLodBand CurrentLodBand = ESubNpcLodBand::Culled;

	UPROPERTY(Transient)
	TObjectPtr<ANpcCrowdPath> CurrentPath;

	/** 지금 점유 중인 스팟. 풀 회수 시 반드시 해제해야 한다 */
	UPROPERTY(Transient)
	TObjectPtr<ANpcActivitySpot> OccupiedSpot;

	/** 그룹으로 다닐 때의 앞사람 (약한 참조 - 앞사람이 먼저 회수돼도 안전해야 한다) */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASubNpcCharacter> GroupLeader;

	/** 현재 경로 위 진행 거리 (cm) */
	float PathDistance = 0.f;

	/** 경로를 역방향으로 타는 중인가 */
	bool bReversedOnPath = false;

	/** 좌우 차선 오프셋 (cm). 개체마다 달라야 한 줄로 겹치지 않는다 */
	float LaneOffset = 0.f;

	/** 그룹 추종 시 앞사람과 벌릴 간격 (cm) */
	float GroupFollowGap = 0.f;

	/** 이 개체의 걷기 속도 (cm/s) */
	float WalkSpeed = 160.f;

	/** 마지막으로 반응한 시각. 쿨다운 판정용 */
	float LastReactionTime = -1000.f;

	/** 마지막으로 부딪힌 시각. 대사 분기용 */
	float LastBumpTime = -1000.f;

	/** 스팟 행동 / 반응이 끝날 시각 */
	float ActivityEndTime = 0.f;

	/** 반응 중 옆으로 비킬 목표 오프셋 */
	float DodgeTargetOffset = 0.f;

	/** 이 개체에 적용된 아키타입 (대사·반응 가중치 조회용) */
	FSubNpcArchetypeRow Archetype;
};
