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

	/**
	 * 이동 틱 주기를 정한다. **거리만** 본다.
	 *
	 * 예전에는 ApplyLodBand가 틱 주기까지 같이 정했다. 그런데 그 밴드는
	 * "지금 화면에 그려지고 있나"로도 한 단계 내려간다. 카메라를 돌리면
	 * 뒤쪽 개체가 순식간에 Far로 떨어졌다가 다시 올라오고, 그때마다 틱 주기가
	 * 0초 <-> 0.3초를 오가면서 위치 갱신이 뚝뚝 끊겼다.
	 * 애니메이션은 계속 재생되니 "걷는데 순간이동하는" 그림이 된다.
	 *
	 * 이동은 화면에 보이든 말든 매끄러워야 한다. 그래서 분리했다.
	 * 절약 효과가 큰 쪽(포즈 평가, 강제 LOD, 그림자)은 ApplyLodBand가 그대로 맡는다.
	 */
	void ApplyMovementTickRate(ESubNpcLodBand DistanceBand);

	UFUNCTION(BlueprintPure, Category = "Crowd|LOD")
	ESubNpcLodBand GetLodBand() const { return CurrentLodBand; }

	// ---- 경로 --------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	ANpcCrowdPath* GetCurrentPath() const { return CurrentPath; }

	/** 그룹으로 다닐 때 앞사람. 있으면 앞사람의 경로 진행도를 따라간다 */
	void SetGroupLeader(ASubNpcCharacter* Leader, float FollowGap, float LaneOffset);

	// ---- StateTree 진입점 ----------------------------------------------------
	//
	// StateTree 태스크가 부르는 함수들. 후보 선정·확률 판정·점유 같은 로직은
	// 이미 이 클래스에 있으므로 태스크에서 다시 구현하지 않고 여기로 위임한다.
	// 나중에 Mass로 옮길 때 태스크는 그대로 두고 이 구현만 갈아끼우면 된다.

	/** 근처 스팟 중 하나에 들어가 본다. 성공하면 상태가 AtSpot으로 바뀐다 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Spot")
	bool TryEnterNearbySpot();

	/** 지정한 스팟에 들어가 본다 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Spot")
	bool TryEnterSpot(ANpcActivitySpot* Spot);

	/** 스팟에 있으면 나온다. 없으면 아무 일도 하지 않는다 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Spot")
	void LeaveSpotIfAny();

	// ---- 반응 --------------------------------------------------------------

	/**
	 * 플레이어와 부딪혔을 때 호출. **ImpactSpeed(플레이어 수평 속도)만으로**
	 * 아키타입의 BumpTiers에서 반응과 애니메이션을 골라 재생한다.
	 * 쿨다운 중이거나 이미 반응·대화 중이면 무시한다.
	 *
	 * 두 곳에서 불린다.
	 *   1) 플레이어가 와서 부딪혔을 때 (NotifyHit)
	 *   2) 행인이 걸어오다 플레이어와 닿았을 때 (Director의 거리 판정)
	 *      행인은 스윕 없이 움직이므로 이 경우 NotifyHit이 안 불린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Reaction")
	void ReactToPlayerBump(AActor* Player, float ImpactSpeed);

	/**
	 * 부딪힘 판정에 쓸 플레이어 속도 (cm/s). ReactToPlayerBump의 ImpactSpeed로 넘긴다.
	 *
	 * 실제 속도만 보면 안 된다. 뛰어와 행인 몸에 막히는 순간 속도가 뚝 떨어져서,
	 * 달려서 부딪혀도 걷기 구간으로 판정됐다. 입력을 누르고 있는 동안은
	 * "내려던 속도"(지금 MaxSpeed × 입력 세기)도 함께 보고 큰 쪽을 쓴다.
	 */
	static float GetPlayerBumpSpeed(const APawn* Player);

	/**
	 * 부딪힘 반응이 결정된 순간 블루프린트에 알린다.
	 *
	 * C++가 고른 애니메이션으로 충분하면 구현하지 않아도 된다.
	 * 몽타주·사운드·나이아가라 등 연출을 붙이고 싶을 때 쓴다.
	 *
	 * @param Reaction      고른 반응 종류 (피하기 / 화내기 / 놀라기)
	 * @param bPushedLeft   true면 왼쪽으로 밀린다 (= 오른쪽에서 부딪혔다)
	 * @param bWasWalking   부딪힐 때 걷고 있었는가. false면 서 있던 중
	 *
	 * [주의] UFUNCTION은 클래스 크기에 영향이 없다. 여기에 변수를 추가하면
	 * WalkSpeed / bIsMoving / CurrentSpeed 오프셋이 밀려 AnimBP가 깨진다.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Crowd|Reaction")
	void OnBumpReaction(ESubNpcReaction Reaction, bool bPushedLeft, bool bWasWalking);

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

	/** 거리 밴드에 맞는 애니메이션 갱신 빈도를 적용한다 (N프레임에 한 번 평가) */
	static void ApplyAnimUpdateRate(class USkeletalMeshComponent* MeshComp, int32 FrameSkip);

	/** 경로를 따라 전진. LOD에 따라 이동 방식이 달라진다 */
	void TickPathFollow(float DeltaTime);

	/** 현재 경로가 끝났을 때 다음 경로를 고르거나 회수 요청 */
	void AdvanceToNextPath();

	/** 이 경로 구간의 스팟들에 대해 "들를까" 판정 */
	void RollForActivitySpot();

	/** 스팟 행동 시작/종료 */
	void BeginSpotActivity(ANpcActivitySpot* Spot, const FNpcActivityOption& Option);
	void EndSpotActivity();

	/** 다음 단계로 넘어간다. 남은 단계가 없으면 EndSpotActivity를 부른다 */
	void AdvanceSpotStep();

	/** 단계 하나를 재생한다 (애니메이션 + 종료 시각 설정) */
	void PlaySpotStep(FName AnimId, float MinDuration, float MaxDuration, bool bLoop);

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

	/**
	 * 부딪힌 뒤 이 시간(초) 안에 말을 걸면 BumpedBarks 대사가 나온다.
	 *
	 * 짧게 잡는 게 맞다. "방금 부딪힌 일에 대한 반응"이라는 전제로 대사를 쓰기 때문에,
	 * 창이 길면 한참 뒤에 말을 걸었는데 방금 일처럼 말하는 어색한 상황이 나온다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Interaction", meta = (ClampMin = "0.0"))
	float BumpMemoryDuration = 12.f;

	/**
	 * [사용 안 함] 속도 기준은 아키타입 테이블의 BumpTiers로 옮겼다 (DT_SubNpcArchetypes).
	 *
	 * 지우지 않고 남겨둔 이유: 이 클래스의 프로퍼티를 빼면 뒤쪽 WalkSpeed / bIsMoving /
	 * CurrentSpeed의 오프셋이 밀려 ABP_Passerby의 Property Access가 엉뚱한 값을 읽는다.
	 * 에디터에서 헷갈리지 않도록 숨겨둔다.
	 */
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "DT_SubNpcArchetypes의 BumpTiers를 쓰세요."))
	float BumpSpeedThreshold_DEPRECATED = 700.f;

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

	/**
	 * 이동할 때 캡슐 스윕을 쓸지.
	 *
	 * **기본값 false를 권장한다.** 켜면 행인끼리 서로를 막아서, 같은 지점에 두 명이
	 * 등장하는 순간 둘 다 영영 멈춘다 (진행도만 오르고 몸은 제자리).
	 * 끄더라도 플레이어와의 부딪힘 반응과 E키 상호작용은 정상 동작한다.
	 *
	 * 벽을 절대 통과하면 안 되는 구역에서만, 행인 밀도를 낮춘 뒤 켜는 걸 고려한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	bool bSweepWhileMoving = false;

	/**
	 * 이보다 짧은 경로는 쓸 수 없는 것으로 본다 (cm).
	 * 길이가 0에 가까우면 도착 판정이 매 틱 성립해서 무한 재활용이 일어난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path", meta = (ClampMin = "1.0"))
	float MinUsablePathLength = 50.f;

	/** 막다른 길에서 되돌아설 때 끝점에서 안쪽으로 밀어줄 거리 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path", meta = (ClampMin = "1.0"))
	float TurnAroundNudge = 20.f;

	/**
	 * 스팟에 들를지 판정하는 간격 (cm).
	 *
	 * 예전에는 경로에 진입할 때 한 번만 판정했다. 그러면 경로 중간이나 끝에 있는 스팟도
	 * 진입 순간에 당첨될 수 있어서, 이미 지나쳐 걸어간 개체가 뒤에 있는 스팟으로
	 * 순간이동하는 것처럼 보였다. 일정 거리마다 다시 굴려서 "근처에 왔을 때" 들르게 한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path", meta = (ClampMin = "10.0"))
	float SpotCheckInterval = 150.f;

	/** 이 거리 안에 있는 스팟만 후보로 본다 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path", meta = (ClampMin = "0.0"))
	float SpotEngageRadius = 400.f;

	// ---- 소프트 회피 ---------------------------------------------------------

	/**
	 * 가까운 행인끼리 좌우로 살짝 비켜서게 한다.
	 *
	 * RVO 같은 정식 회피와는 다르다. 진로를 다시 계산하지 않고, 이미 갖고 있는
	 * 좌우 오프셋 값 하나만 서로 반대로 민다. "스스로 판단하지 않는다"는 원칙을
	 * 깨지 않으면서 겹침만 줄이는 절충안이다.
	 *
	 * Director가 Near/Mid 대역에 대해서만 계산한다. 멀리서 겹치는 건 어차피 안 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Avoidance")
	bool bSoftAvoidance = true;

	/** 회피 오프셋이 목표값을 향해 움직이는 속도 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Avoidance", meta = (ClampMin = "0.1"))
	float AvoidInterpSpeed = 3.f;

	/** 한 프레임에 밀릴 수 있는 최대량 (cm). 너무 크면 옆으로 튄다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Avoidance", meta = (ClampMin = "0.0"))
	float MaxAvoidOffset = 60.f;

	/** LOD 단계별 틱 간격 (초). Near는 매 프레임 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	float MidTickInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD")
	float FarTickInterval = 0.3f;

	/**
	 * Mid / Far에서 몇 프레임에 한 번 포즈를 평가할지.
	 *
	 * 프로파일링에서 군중 비용의 86%가 애니메이션으로 나왔고, 그 부분은 거리와
	 * 무관하게 매 프레임 돌고 있었다. 멀리 있는 개체를 4~8프레임에 한 번만
	 * 평가하게 하면 그만큼 그대로 절약된다. 사이 프레임은 보간으로 채운다.
	 *
	 * 1이면 매 프레임 (절약 없음).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MidAnimFrameSkip = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|LOD", meta = (ClampMin = "1", ClampMax = "16"))
	int32 FarAnimFrameSkip = 4;

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

	/**
	 * 이 개체의 걷기 속도 (cm/s).
	 *
	 * [애님 블루프린트에 노출하는 이유]
	 *   서브 NPC는 CharacterMovement를 쓰지 않고 위치를 직접 세팅한다.
	 *   그래서 GetVelocity()가 항상 0이고, 서드퍼슨 ABP처럼 속도로 로코모션을
	 *   판단하는 그래프는 영원히 대기 포즈만 낸다.
	 *
	 *   ABP가 이 값을 직접 읽게 하면 해결된다. 함수 호출이 아니라 프로퍼티 읽기라
	 *   Property Access로 접근할 수 있고, 그러면 Thread Safe Update Animation
	 *   안에서도 쓸 수 있다.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Crowd|Anim")
	float WalkSpeed = 160.f;

	/**
	 * 지금 실제로 걷고 있는가. ABP의 걷기/대기 전환에 쓴다.
	 * 스팟에서 활동 중이거나 반응 중이면 false다.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Crowd|Anim")
	bool bIsMoving = false;

	/**
	 * ABP가 읽을 현재 속력 (cm/s). 걷는 중이면 WalkSpeed, 아니면 0.
	 * 로코모션 블렌드스페이스에 그대로 넣을 수 있는 값이다.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Crowd|Anim")
	float CurrentSpeed = 0.f;

	/** 마지막으로 반응한 시각. 쿨다운 판정용 */
	float LastReactionTime = -1000.f;

	/** 마지막으로 부딪힌 시각. 대사 분기용 */
	float LastBumpTime = -1000.f;

	/** 스팟 행동 / 반응이 끝날 시각 */
	float ActivityEndTime = 0.f;

	/** 반응 중 옆으로 비킬 목표 오프셋 */
	float DodgeTargetOffset = 0.f;

	/** 지금 재생 중인 반응 종류. 피하기만 걸으면서 비키고 나머지는 멈춘다 */
	ESubNpcReaction CurrentReaction = ESubNpcReaction::None;

	/** 이 개체에 적용된 아키타입 (대사·반응 가중치 조회용) */
	FSubNpcArchetypeRow Archetype;

	// ---- 소프트 회피 (Director가 호출) --------------------------------------

public:
	/** 회피 계산에 필요한 값들. Director가 이웃끼리 비교할 때 읽는다 */
	float GetPathDistance() const { return PathDistance; }
	float GetLaneOffset() const { return LaneOffset; }

	/** 그룹 리더 (없으면 nullptr) */
	ASubNpcCharacter* GetGroupLeader() const { return GroupLeader.Get(); }

	/**
	 * 같은 그룹인가.
	 *
	 * 친구끼리는 회피로 밀어내면 안 된다. 붙어 다니라고 묶어놓고 떼어놓으면
	 * 대형이 계속 흔들려서 오히려 더 어색하다.
	 *
	 * [주의] 이 클래스에 데이터 멤버를 새로 추가하지 않는다.
	 * WalkSpeed / bIsMoving / CurrentSpeed 앞에 멤버가 끼면 오프셋이 밀려서
	 * AnimBP의 Property Access가 엉뚱한 값을 읽는다 (걷기 애니메이션이 깨진다).
	 * 메서드 추가는 레이아웃에 영향이 없으므로 안전하다.
	 */
	bool IsSameGroup(const ASubNpcCharacter* Other) const
	{
		if (!Other || Other == this)
		{
			return false;
		}
		const ASubNpcCharacter* MyLeader = GroupLeader.Get();
		const ASubNpcCharacter* ItsLeader = Other->GroupLeader.Get();
		return (MyLeader && MyLeader == ItsLeader)
			|| (MyLeader == Other)
			|| (ItsLeader == this);
	}
	bool IsWalkingOnPath() const { return CurrentActivity == ESubNpcActivity::Walking && CurrentPath != nullptr; }

	/**
	 * 경로 위에서 뒷사람을 막는 몸이 있는가.
	 *
	 * 걷는 중이 아니어도 부딪혀 멈춘(Reacting) 사람은 경로 위에 그대로 서 있다.
	 * 이걸 빼면 뒷사람이 앞사람을 못 보고 뚫고 지나간다.
	 * 스팟에서 반응 중인 사람은 경로 밖에 서 있으므로 제외한다.
	 */
	bool IsBlockingOnPath() const
	{
		return IsWalkingOnPath()
			|| (CurrentActivity == ESubNpcActivity::Reacting && CurrentPath != nullptr && !bResumeSpotAfterReaction);
	}

	/**
	 * 좌우로 밀어낼 양을 누적한다. 실제 반영은 다음 틱에서 보간으로 이뤄진다.
	 * 즉시 위치를 바꾸지 않는 이유: 두 개체가 서로를 동시에 밀면 진동하기 때문이다.
	 */
	void AddAvoidanceNudge(float Amount) { PendingAvoidNudge += Amount; }

	/**
	 * 앞사람이 막고 있으니 속도를 줄이라는 요청 (0~1 배율).
	 *
	 * 행인은 1차원 경로 위를 걷기 때문에, 좌우로만 밀어내면 앞사람이 느릴 때
	 * **그대로 뚫고 지나간다.** 진행 방향으로도 제동이 필요하다.
	 * 여러 요청이 오면 가장 강한(가장 느린) 것을 따른다.
	 */
	void RequestSlowDown(float Scale)
	{
		PendingSpeedScale = FMath::Min(PendingSpeedScale, FMath::Clamp(Scale, 0.f, 1.f));
	}

	bool IsReversedOnPath() const { return bReversedOnPath; }

	/** 앞이 막혀 크게 감속한 상태인가. 뒷사람이 기다릴지 추월할지 판단하는 데 쓴다 */
	bool IsHeldUp() const { return CurrentSpeedScale < 0.6f; }

	/**
	 * 막힌 앞사람을 추월하려고 옆 차선으로 옮겨 가라는 요청.
	 *
	 * 좌우 밀어내기(PendingAvoidNudge)는 한 번 반영하고 사라지는 약한 힘이라
	 * 앞사람 옆으로 빠져나가기엔 부족하다. 그래서 목표 차선을 따로 주고,
	 * Director가 계속 갱신하는 동안(Until까지) 그쪽으로 옮겨 간다.
	 */
	void RequestPassLane(float TargetLane, float Until)
	{
		PassLaneTarget = TargetLane;
		PassLaneUntil = Until;
	}

	/** 추월 중인가. 추월하는 동안에는 방향을 바꾸지 않아야 좌우로 떨지 않는다 */
	bool IsPassing() const;

	float GetPassLaneTarget() const { return PassLaneTarget; }

	/** 회수 예약 여부. 순회 중에 배열이 바뀌지 않도록 Director가 나중에 처리한다 */
	bool IsPendingRecycle() const { return bPendingRecycle; }
	void ClearPendingRecycle() { bPendingRecycle = false; }

protected:
	/** 이번 프레임에 이웃들이 밀어낸 양의 합 */
	float PendingAvoidNudge = 0.f;

	/** 경로 끝에 닿아 회수를 기다리는 중 */
	bool bPendingRecycle = false;

	// ---- 스팟 단계 진행 ------------------------------------------------------

	/** 지금 재생 중인 스팟 행동 (단계 배열을 들고 있어야 이어서 재생할 수 있다) */
	FNpcActivityOption CurrentSpotOption;

	/** 지금 몇 번째 단계인가. -1 = 첫 동작(AnimId), 0부터는 ExtraSteps 인덱스 */
	int32 CurrentSpotStep = -1;

	/**
	 * 반응이 끝난 뒤 하던 스팟 행동으로 되돌아갈지.
	 * 앉아 있다가 부딪혀 놀란 사람은 다시 앉는 게 자연스럽다.
	 */
	bool bResumeSpotAfterReaction = false;

	/** 다음 스팟 판정을 할 경로 진행 거리. 경로 진입 시 한 번만 보는 문제를 없앤다 */
	float NextSpotCheckDistance = 0.f;

	/**
	 * 경로를 갈아탈 때 생기는 위치 튐을 흡수하는 잔여 오프셋 (월드 공간).
	 *
	 * 경로 전환은 본질적으로 불연속이다. 새 스플라인의 어느 지점으로 옮겨 앉는 순간
	 * 위치가 한 프레임 만에 바뀌고, 그게 「사라졌다가 다른 데서 나타남」으로 보인다.
	 *
	 * 그래서 전환 순간의 어긋난 양을 여기에 담아두고, 매 틱 조금씩 0으로 줄인다.
	 * 결과적으로 사람이 새 경로 쪽으로 **부드럽게 미끄러져 들어간다.**
	 *
	 * [주의] 이 멤버에는 UPROPERTY를 붙이지 않는다. 리플렉션 프로퍼티를 추가하면
	 * ABP_Passerby의 Property Access가 읽는 WalkSpeed / bIsMoving / CurrentSpeed의
	 * 오프셋 계산에 영향을 줄 수 있다. 여기 순수 C++ 멤버로 두면 안전하다.
	 */
	FVector PathHandoffOffset = FVector::ZeroVector;

	/** 이번 갱신에서 요청된 감속 배율 (1 = 감속 없음). Director가 채우고 틱이 소비한다 */
	float PendingSpeedScale = 1.f;

	/** 실제로 적용 중인 감속 배율. 급브레이크로 보이지 않게 보간한다 */
	float CurrentSpeedScale = 1.f;

	// [주의] 아래 멤버는 반드시 클래스 맨 끝에 둔다 (위쪽 프로퍼티 오프셋을 밀지 않도록).

	/** 추월할 때 옮겨 갈 목표 차선 오프셋 */
	float PassLaneTarget = 0.f;

	/** 이 시각까지 PassLaneTarget 쪽으로 옮겨 간다. Director가 갱신을 멈추면 자연히 끝난다 */
	float PassLaneUntil = -1.f;
};
