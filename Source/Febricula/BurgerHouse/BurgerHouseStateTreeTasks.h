// 버거리아 — StateTree 태스크 (네이티브 C++)
//
// LL_PROJECT.md §9.5 손님 루프.
//
// ## 여기 없는 것 — MoveTo
//
// 엔진이 이미 준다 (`FStateTreeMoveToTask`, "Move To", `AI|Action`). 인스턴스
// 데이터에 `Destination`(FVector) 파라미터가 있으므로 **아래 태스크들의
// `UseLocation` 출력을 거기 바인딩**하면 된다. 직접 쓸 이유가 없다.
//
// ## 규약 셋
//
// ① **`Context` 카테고리 프로퍼티는 스키마가 자동으로 채운다.**
//    `UStateTreeAIComponentSchema` 가 `AAIController` 와 조종 폰을 보장한다.
//    (`ACustomerAIController` 가 `UStateTreeAIComponent` 를 들고 있다.)
//
//    ⚠⚠ **`Customer` 를 `AActor*` 로 두면 안 된다 — AI 컨트롤러에 붙는다.**
//    자동 바인딩(`UStateTreeEditorData::FindContextData`)은 ① 타입으로 후보를
//    거르고 ② 후보가 둘 이상이면 **이름의 Levenshtein 거리**로 고른다.
//    `AActor*` 면 스키마의 `Actor` 와 `AIController` 가 **둘 다** 후보가 되고
//    (`AAIController` 도 `AActor` 의 자식이다), 이름 점수는
//        "customer" ↔ "actor"        = 1 − 5/13 = 0.615
//        "customer" ↔ "aicontroller" = 1 − 7/20 = 0.650   ← 이쪽이 이긴다
//    이라 **컨트롤러가 꽂힌다.** 증상은 모든 태스크가 "손님이 아니다" 로 실패
//    하는 것이고, 원인이 이름 유사도라 눈으로 찾기가 거의 불가능하다.
//
//    → **구체 타입(`ACustomerCharacter*`)으로 못박는다.** 그러면 `AIController`
//      가 후보에서 아예 빠져 후보가 하나뿐이라 이름 점수를 타지 않는다.
//      엔진 예제들이 전부 구체 타입인 것도 같은 이유다
//      (`FStateTreeMoveToTask` 는 `AAIController*`, Combat 은 `ACharacter*`).
//
//    ⚠ 그 대신 **스키마의 `Context Actor Class` 를 `BP_Customer` 로 맞춰야**
//      한다. 안 맞으면 후보가 0이 되어 컴파일 에러가 난다 — 조용히 틀리는
//      것보다 낫다.
//
// ② **`Output` 카테고리는 다른 태스크가 바인딩해 읽는다.** 바인딩은 상태에
//    **들어갈 때 한 번** 복사된다 — 서 있는 동안 값을 바꿔도 아무도 안 본다.
//    그래서 값이 변하는 것(대기열 자리)은 출력이 아니라 **이벤트**로 민다
//    (`BurgerHouseTags::Queue_Advanced`).
//
// ⚠ **BP 태스크로 가지 말 것** (§9.1) — 섞으면 디버깅 경로가 둘로 갈린다.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"

#include "BurgerHouseStateTreeTasks.generated.h"

class AActor;
class ASlotPoint;
class ACustomerCharacter;

////////////////////////////////////////////////////////////////////
// 슬롯 예약

USTRUCT()
struct FBurgerHouseClaimSlotInstanceData
{
	GENERATED_BODY()

	/** 예약하는 액터 — 손님 폰 */
	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** `Seat` / `Kiosk` / `Pickup`. 비우면 아무 종류나. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Kind = TEXT("Seat");

	/**
	 *	빈 자리 중 **아무거나** 고른다.
	 *
	 *	끄면 배열에서 처음 만난 빈 자리 — `TActorIterator` 순서라 사실상 고정이고,
	 *	손님이 몇 명이 오든 **늘 같은 순서로 같은 자리**를 채운다.
	 *
	 *	⚠ **좌석 히트맵이 물려 있으면 이 값과 무관하게 히트 가중이 이긴다.**
	 *	  히트맵은 「어디가 인기 있는가」를 그림으로 주는 것이라 균등 무작위보다
	 *	  나은 답이다. 이 스위치는 히트맵 없이 다양성만 원할 때 쓴다.
	 *
	 *	⚠ 켜면 재현이 깨진다. 한 손님을 쫓아 디버깅할 때는 꺼 두는 편이 낫다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bPickRandom = false;

	/**
	 *	잡자마자 그 자리로 **순간이동**한다. 걸어가지 않는다.
	 *
	 *	## 프리필 전용이다
	 *
	 *	레벨이 열릴 때 「이미 앉아 있던 손님」을 만들려면 문 앞에서 자리까지
	 *	걸어가는 3초를 없애야 한다. 그 3초 동안은 명백히 **방금 들어온 손님**으로
	 *	보이기 때문이다.
	 *
	 *	⚠ **평소 손님에게 켜지 말 것.** 손님이 문에서 좌석으로 순간이동한다.
	 *	  프리필 갈래(`Start Role Is` 로 갈린 쪽)에서만 켠다.
	 *
	 *	⚠ `Sit At Seat` 은 **이미 스냅한다.** 좌석 갈래에서는 이 스위치가 없어도
	 *	  결과가 같으므로 굳이 켜지 않아도 된다. 키오스크·픽업처럼 서 있는
	 *	  자리에서만 의미가 있다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bTeleportOnEnter = false;

	/** 잡은 슬롯 */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	TObjectPtr<ASlotPoint> Slot;

	/** **여기를 MoveTo 의 `Destination` 에 묶는다.** 슬롯 원점이 아니라 사용 지점이다. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector UseLocation = FVector::ZeroVector;

	/** 사용 지점의 방향 — 앉을 때·손 뻗을 때 몸을 돌리는 값 */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FRotator UseRotation = FRotator::ZeroRotator;
};

/**
 *	슬롯 하나를 예약한다. 빈 자리가 없으면 **실패**한다 (기다리지 않는다 —
 *	기다리는 것은 트리가 정할 일이다).
 *
 *	⚠ **`ExitState` 에서 반드시 놓는다** (§9.5 "릴리스 누락이 이 시스템의 1번
 *	  버그다"). 상태가 어디로 튀든 이 경로는 탄다. 그래도 못 잡는 경로(파괴·빙의
 *	  해제)는 `ACustomerCharacter::EndPlay` 와 서브시스템의 좀비 청소가 받는다 —
 *	  **세 겹**이다.
 */
USTRUCT(meta = (DisplayName = "Claim Slot", Category = "BurgerHouse"))
struct FBurgerHouseClaimSlotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseClaimSlotTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseClaimSlotInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 주문 발행

USTRUCT()
struct FBurgerHousePlaceOrderInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** 발급된 번호. 0 이면 실패. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	int32 OrderNumber = 0;
};

/**
 *	주문을 넣고 번호를 받는다. `ACustomerCharacter::OrderNumber` 에도 캐시한다.
 *
 *	⚠ 번호의 **소유자는 서브시스템**이다 (§9.1). 캐시는 화면 표시·디버그용이고
 *	  수령 판정은 `ClaimOrder` 가 한다.
 */
USTRUCT(meta = (DisplayName = "Place Order", Category = "BurgerHouse"))
struct FBurgerHousePlaceOrderTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHousePlaceOrderTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHousePlaceOrderInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 주문 수령

USTRUCT()
struct FBurgerHouseClaimOrderInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/**
	 *	비워 두면 손님이 캐시한 번호를 쓴다. 채우면 그 번호로 시도한다.
	 *
	 *	⚠ 0 이 「아무거나」가 **아니다.** 남의 트레이를 집어 가는 버그를 만들지
	 *	  않으려고 일부러 「내 번호」로만 좁혔다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	int32 OrderNumber = 0;
};

/**
 *	트레이 수령. **실패하면 태스크가 실패한다** — 트리는 Wait 로 되돌아가면 된다
 *	(§9.5 "ClaimOrder 실패 시 → Wait 복귀").
 */
USTRUCT(meta = (DisplayName = "Claim Order", Category = "BurgerHouse"))
struct FBurgerHouseClaimOrderTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseClaimOrderTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseClaimOrderInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 대기열

USTRUCT()
struct FBurgerHouseWaitAreaInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/**
	 *	줄의 첫 자리(월드).
	 *
	 *	⚠ 슬롯 액터로 두지 않은 이유 — 줄은 **자리가 아니라 배열**이다 (§9.5).
	 *	  자리마다 액터를 놓으면 줄 길이가 액터 수에 묶이고, 「한 칸 당기기」가
	 *	  다시 슬롯 재예약 문제가 된다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector QueueStart = FVector::ZeroVector;

	/** 한 사람 뒤로 물러나는 양(월드 델타). 줄이 뻗는 방향 × 간격. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector QueueStep = FVector(-70.f, 0.f, 0.f);

	/**
	 *	줄 선 자리로 **순간이동**한다. **프리필 전용** — `Claim Slot` 의 같은
	 *	이름 스위치와 뜻이 같다.
	 *
	 *	⚠ 여기는 순간이동이 **필수에 가깝다.** 좌석과 달리 대기열에는 스냅하는
	 *	  코드가 따로 없어서, 안 켜면 프리필 손님이 문 앞에서부터 걸어온다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bTeleportOnEnter = false;

	/** 내 줄 위치. 0 이 맨 앞. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	int32 QueueIndex = 0;

	/**
	 *	줄에 **처음 설 때**의 자리.
	 *
	 *	⚠⚠ **`Move To` 를 여기에 묶지 말 것 — `Get Queue Spot` 에 묶는다.**
	 *	  이 값은 상태에 처음 들어올 때 한 번만 계산된다. 이 태스크는
	 *	  `bShouldStateChangeOnReselect = false` 라 「한 칸 당기기」로 상태를
	 *	  다시 태워도 **여기는 다시 계산되지 않는다.** 여기에 묶으면 줄이
	 *	  영원히 안 줄어드는데 이벤트는 정상적으로 나가므로 원인을 못 찾는다.
	 *
	 *	쓰이는 곳은 둘뿐이다 — `bTeleportOnEnter` 의 목적지, 그리고 디버깅.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector WaitLocation = FVector::ZeroVector;
};

/**
 *	대기열에 서고, 상태를 벗어날 때 뺀다.
 *
 *	⚠ **`ExitState` 의 `LeaveWaitArea` 가 「뒷사람 전원을 당기는」 방아쇠다.**
 *	  빼먹으면 앞사람이 사라져도 줄이 안 줄어든다.
 */
USTRUCT(meta = (DisplayName = "Enter Wait Area", Category = "BurgerHouse"))
struct FBurgerHouseWaitAreaTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseWaitAreaTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseWaitAreaInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 대기열 — 지금 내 자리가 어디인가

USTRUCT()
struct FBurgerHouseQueueSpotInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** `Enter Wait Area` 와 **같은 값을 넣어야 한다.** 둘이 다르면 줄이 두 줄로 보인다. */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector QueueStart = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector QueueStep = FVector(-70.f, 0.f, 0.f);

	/** 내 줄 위치. 0 이 맨 앞. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	int32 QueueIndex = 0;

	/** **MoveTo 의 `Destination` 을 여기에 묶는다** — `Enter Wait Area` 가 아니라. */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector WaitLocation = FVector::ZeroVector;
};

/**
 *	**지금** 내 줄 자리를 계산해 낸다. 줄에 넣거나 빼지 **않는다.**
 *
 *	## 이 태스크가 푸는 문제 — 「한 칸 당기기」가 아무 일도 안 했다
 *
 *	앞사람이 빠지면 서브시스템이 `Queue.Advanced` 를 밀고, 트리는 `Wait` 로
 *	자기 자신 전이를 한다. 그런데 **아무도 안 움직였다.**
 *
 *	원인은 `Enter Wait Area` 의 `bShouldStateChangeOnReselect = false` 다.
 *	엔진은 자기 자신 전이(`ChangeType == Sustained`)에서 이 값이 거짓이면
 *	`EnterState` 를 **아예 안 부른다** (`StateTreeExecutionContext.cpp:3839`).
 *	그래서 `WaitLocation` 이 옛 값 그대로고, `Move To` 는 그 옛 값을 다시
 *	복사해 가서 **이미 서 있는 자리로** 걸어간다.
 *
 *	⚠ **그 플래그를 참으로 바꾸면 더 나빠진다.** 그러면 `ExitState` 도 같이
 *	  불리는데, 거기서 `LeaveWaitArea` 가 줄에서 빼고 `EnterState` 의
 *	  `EnterWaitArea` 가 **맨 뒤에 다시 붙인다** (`StoreSubsystem.cpp` —
 *	  `WaitQueue.Add`). 앞으로 당겨져야 할 사람이 꼴찌가 된다.
 *
 *	→ **둘로 쪼갠다.** 줄 **소속**은 `Enter Wait Area` 가 붙들고(재진입 안 함),
 *	  줄 **자리**는 이 태스크가 낸다(재진입 함). 같은 상태에 나란히 두면
 *	  플래그가 태스크마다 달라 각자 맞게 동작한다.
 *
 *	## 쓰는 법 — `Enter Wait Area` **바로 아래**에 같은 상태로
 *
 *	    Wait                Tasks: Enter Wait Area   ← 위. 줄에 넣는다
 *	                               Get Queue Spot    ← 아래. 자리를 낸다
 *	                        ⚠ Tasks Completion = All
 *	    └─ W_Move           Move To ← ⛓ Wait ▸ Get Queue Spot ▸ Wait Location
 *
 *	⚠ **순서가 중요하다.** `Enter Wait Area` 가 위에 있어야 첫 진입에서 줄에
 *	  먼저 들어가고, 그 다음 이 태스크가 자리를 읽는다. 뒤집으면 첫 프레임에
 *	  「줄에 없다」로 실패한다.
 *
 *	⚠ **`Tasks Completion` 을 `All` 로 바꿔야 한다.** 이 태스크는 즉시 성공하고
 *	  `Enter Wait Area` 는 끝나지 않는다. 기본값 `Any` 면 이것이 성공하는
 *	  순간 `Wait` 가 완료돼 **줄에서 빠져 버린다.**
 */
USTRUCT(meta = (DisplayName = "Get Queue Spot", Category = "BurgerHouse"))
struct FBurgerHouseQueueSpotTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseQueueSpotTask()
	{
		bShouldCallTick = false;
		// ⚠ **여기가 이 태스크의 존재 이유다.** 참이라 자기 자신 전이에서도
		//    `EnterState` 가 다시 불리고, 그래야 자리가 다시 계산된다.
		bShouldStateChangeOnReselect = true;
	}

	using FInstanceDataType = FBurgerHouseQueueSpotInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 행동 (애님 또는 시간)

USTRUCT()
struct FBurgerHouseActionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** `Interact_Reach` · `Sit_Down` · `Sit_Idle` · `Sit_Eat` · `Stand_Up` · `Idle_Look` */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName ActionName = TEXT("Idle_Look");

	/** 몽타주가 없을 때 대신 세는 시간 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (Units = "s", ClampMin = "0.0"))
	float FallbackSeconds = 3.f;

	/**
	 *	여기에 값을 주면 **매번 `0 ~ RandomExtraSeconds` 를 더한다.**
	 *
	 *	## 왜 필요한가
	 *
	 *	StateTree 는 「어느 갈래로 갈지」를 랜덤하게 고를 수 있지만
	 *	(`Try Select Children At Random`) **「얼마나 오래」는 못 흔든다.**
	 *	그래서 같은 상태에 들어간 손님 여덟이 **초 단위로 같이 움직인다** —
	 *	행동이 갈려도 군무처럼 보이는 원인이 이것이다.
	 *
	 *	⚠ 몽타주가 있어도 더한다. 애님이 끝난 뒤 그만큼 더 서 있게 되므로
	 *	  「먹고 나서 잠깐 앉아 있기」 같은 여유가 공짜로 생긴다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (Units = "s", ClampMin = "0.0"))
	float RandomExtraSeconds = 0.f;

	/**
	 *	끝나면 다시 시작하는 횟수를 흔든다 — `0` 이면 안 흔든다.
	 *
	 *	`bLoop` 가 꺼진 상태에서 `2` 면 **1~3번** 반복한다.
	 *	「몇 입 먹는가」를 손님마다 다르게 하는 값이다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (ClampMin = "0"))
	int32 RandomExtraRepeats = 0;

	/** 끝나면 다시 시작한다 — `Idle_Look` 루프용 (§9.5 "Idle_Look 루프") */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bLoop = false;

	/** 남은 시간 */
	UPROPERTY()
	float Remaining = 0.f;

	/** 남은 반복 횟수 (`RandomExtraRepeats` 용) */
	UPROPERTY()
	int32 RepeatsLeft = 0;
};

/**
 *	행동 하나를 재생하고 **끝날 때까지 기다린다.**
 *
 *	⚠ 몽타주가 없으면 **시간만 센다** — 그것이 정상 경로다
 *	  (`ACustomerCharacter` 헤더). 애님 6종(§9.4)이 없어도 이 태스크로 루프
 *	  전체를 오늘 돌려 볼 수 있고, 6종이 오면 `Actions` 맵에 꽂는 것으로 끝난다.
 *	  **StateTree 그래프는 손대지 않는다.**
 */
USTRUCT(meta = (DisplayName = "Play Action", Category = "BurgerHouse"))
struct FBurgerHouseActionTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseActionTask()
	{
		bShouldCallTick = true;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseActionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

private:
	static float RandomExtra(const FInstanceDataType& Data);

public:

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 착석

USTRUCT()
struct FBurgerHouseSitInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** `Claim Slot` 의 `UseLocation` 을 묶는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector SeatLocation = FVector::ZeroVector;

	/** `Claim Slot` 의 `UseRotation` 을 묶는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FRotator SeatRotation = FRotator::ZeroRotator;
};

/**
 *	좌석에 앉히고, 상태를 벗어날 때 일으킨다.
 *
 *	**부모 상태에 걸고 자식에서 `Sit_Eat` 등을 돌리는 용도다** — 계속 `Running`
 *	을 돌려주므로 스스로 끝나지 않는다.
 *
 *	§9.5 "착석 정렬 — 가장 까다로운 것 ... **1차는 스냅, 그림이 잡히면 Motion
 *	Warping**". 지금은 스냅이다. 좌면 45cm·앞 6cm 오프셋은 `ASlotPoint` 가 이미
 *	들고 있고, **애님이 붙은 뒤 눈으로 맞춰야 하는 값**이다 (§9.2).
 */
USTRUCT(meta = (DisplayName = "Sit At Seat", Category = "BurgerHouse"))
struct FBurgerHouseSitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseSitTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseSitInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 트레이

UENUM()
enum class EBurgerHouseTrayOp : uint8
{
	/** 손에 든다 */
	Hold		UMETA(DisplayName = "손에 들기"),
	/** 테이블에 내려놓는다 — §9.5 "★손에 든 채 안 먹는다" */
	PlaceOnTable UMETA(DisplayName = "테이블에 놓기"),
	/** 반납 */
	Drop		UMETA(DisplayName = "반납")
};

USTRUCT()
struct FBurgerHouseTrayInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	EBurgerHouseTrayOp Op = EBurgerHouseTrayOp::Hold;

	/** `PlaceOnTable` 일 때만 — 좌석의 `UseLocation` 을 묶는다 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FVector TableLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FRotator TableRotation = FRotator::ZeroRotator;
};

/** 트레이 한 동작. 즉시 끝난다. */
USTRUCT(meta = (DisplayName = "Tray", Category = "BurgerHouse"))
struct FBurgerHouseTrayTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseTrayTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseTrayInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 매장 지점 조회 — 문 같은 「예약하지 않는」 자리

USTRUCT()
struct FBurgerHouseGetPointInstanceData
{
	GENERATED_BODY()

	/** `Entry` / `Exit` — 또는 예약 없이 위치만 알고 싶은 아무 종류 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Kind = TEXT("Exit");

	/** **여기를 MoveTo 의 `Destination` 에 묶는다.** */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Output")
	FRotator Rotation = FRotator::ZeroRotator;
};

/**
 *	레벨에서 그 종류의 지점을 찾아 좌표를 낸다. **예약하지 않는다.**
 *
 *	## 왜 `Claim Slot` 이 아닌가
 *
 *	문은 들어오는 사람과 나가는 사람이 **동시에** 쓴다. 예약을 걸면 문이 한
 *	명에게 잠겨 뒤에 오는 손님이 영영 못 들어온다.
 *
 *	## 왜 좌표를 트리에 직접 안 박나
 *
 *	박으면 레벨과 트리가 **따로 논다.** 문을 옮기면 트리도 고쳐야 하고, 고치는
 *	걸 잊으면 손님이 벽으로 걸어간다. 레벨의 `ASlotPoint` 가 진실이 되게 한다.
 */
USTRUCT(meta = (DisplayName = "Get Store Point", Category = "BurgerHouse"))
struct FBurgerHouseGetPointTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseGetPointTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseGetPointInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 소멸

USTRUCT()
struct FBurgerHouseDespawnInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/**
	 *	몇 초 뒤에 지울 것인가.
	 *
	 *	⚠ **0 으로 두지 말 것.** `SetLifeSpan(0)` 은 「즉시」가 아니라
	 *	  **「수명 없음」**이라 영원히 안 지워진다. 0 이하면 최소값으로 올린다.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (Units = "s", ClampMin = "0.0"))
	float Delay = 0.2f;
};

/**
 *	손님을 지운다. 슬롯·대기열을 먼저 놓고 수명을 건다.
 *
 *	⚠ **`Destroy()` 를 바로 부르지 않는다.** 이 태스크는 손님의 AI 컨트롤러가
 *	  돌리는 StateTree 안에서 실행된다. 그 자리에서 폰을 파괴하면 실행 중인
 *	  트리의 발밑을 빼는 셈이라 재진입 사고가 난다. `SetLifeSpan` 으로 **다음
 *	  프레임 이후로 미룬다.**
 *
 *	⚠ AI 컨트롤러는 폰이 파괴돼도 **혼자 남는다.** `ACustomerCharacter::EndPlay`
 *	  가 같이 지운다 — 안 그러면 손님이 들고 날 때마다 컨트롤러가 쌓인다.
 */
USTRUCT(meta = (DisplayName = "Despawn Customer", Category = "BurgerHouse"))
struct FBurgerHouseDespawnTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseDespawnTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseDespawnInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 시작 역할 — 「중간부터 시작하는」 손님

USTRUCT()
struct FBurgerHouseConsumeStartRoleInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/** 지워지기 전의 값 — 디버깅용으로 본다 */
	UPROPERTY(VisibleAnywhere, Category = "Output")
	FName ConsumedRole;
};

/**
 *	시작 역할을 **읽고 지운다.** 프리필 갈래의 **맨 위 상태**에 건다.
 *
 *	## 두 가지 쓰임새
 *
 *	**① 갈아타는 자리 (설계 A).** 이 태스크만 든 상태를 만들고 전이를
 *	   **`On Tick`** 으로 기존 상태에 건다. ⚠ `On State Completed` 는 안 걸린다 —
 *	   이 태스크가 끝나지 않기 때문이다 (아래).
 *
 *	**② 갈래의 부모 (설계 B).** 자식들이 일을 하는 동안 이 태스크가 부모에서
 *	   계속 돌고 있는다. `Sit At Seat` 과 같은 방식이다.
 *
 *	## 왜 필요한가 — 안 지우면 무한히 먹는다
 *
 *	`ACustomerCharacter::StartRole` 은 Root 의 갈래를 고르는 값이다. 그대로 두면
 *	「주문을 잃어 처음으로 되돌아가는」 전이가 걸렸을 때 **같은 프리필 갈래로
 *	다시 들어간다.** 프리필 손님 하나가 영원히 먹는 그림이 되고, 원인이 트리
 *	구조가 아니라 남은 값이라 눈으로 찾기 어렵다.
 *
 *	## ⚠ 왜 **부모 상태**에 걸어야 하는가
 *
 *	`UStateTreeState::TasksCompletion` 의 기본값이 **`Any`** 다 — 한 상태의
 *	태스크들은 **동시에** 돌고, **하나라도 끝나면 상태가 끝난다.** 즉시 성공하는
 *	태스크를 다른 태스크 옆에 두면 그 상태가 그 프레임에 통째로 끝난다
 *	(키오스크 예약→해제 무한 루프가 정확히 이 사고였다).
 *
 *	그래서 이 태스크는 **끝나지 않는다** (`Running` 을 계속 돌려준다).
 *	`Sit At Seat` 과 같은 방식이다 — 부모에 걸고 자식이 일을 한다.
 */
USTRUCT(meta = (DisplayName = "Consume Start Role", Category = "BurgerHouse"))
struct FBurgerHouseConsumeStartRoleTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	FBurgerHouseConsumeStartRoleTask()
	{
		bShouldCallTick = false;
		bShouldStateChangeOnReselect = false;
	}

	using FInstanceDataType = FBurgerHouseConsumeStartRoleInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

USTRUCT()
struct FBurgerHouseStartRoleConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;

	/**
	 *	비교할 역할 이름. **스포너의 `PrefillRoles` 와 철자까지 같아야 한다.**
	 *
	 *	⚠ 틀리면 조건이 늘 거짓이 되어 전원이 기본 갈래(문으로 들어온 손님)로
	 *	  떨어진다. 안 죽고 조용히 틀린다 — 프리필이 안 먹는 것처럼 보이면
	 *	  여기부터 볼 것.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FName Role;

	/** 뒤집는다 — 「역할이 없는(문으로 들어온) 손님인가」를 명시하고 싶을 때 */
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bInvert = false;
};

/**
 *	시작 역할이 그것인가. **Root 의 자식마다 하나씩 건다.**
 *
 *	## 쓰는 법
 *
 *	    Root
 *	    ├─ [Start Role Is "Eating"]   Prefill_Eating    (Consume Start Role)
 *	    ├─ [Start Role Is "Waiting"]  Prefill_Waiting   (Consume Start Role)
 *	    ├─ [Start Role Is "Ordering"] Prefill_Ordering  (Consume Start Role)
 *	    └─ (조건 없음)                Normal            ← 문으로 들어온 손님
 *
 *	StateTree 는 부모에 들어갈 때 자식을 **위에서부터** 훑어 조건이 맞는 첫
 *	갈래를 고른다. 마지막을 조건 없이 두면 그것이 기본 갈래가 된다.
 *
 *	⚠ **기본 갈래를 맨 아래에 둘 것.** 위에 두면 조건이 없으니 늘 이기고
 *	  프리필 갈래는 영영 안 선택된다.
 */
USTRUCT(meta = (DisplayName = "Start Role Is", Category = "BurgerHouse"))
struct FBurgerHouseStartRoleCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBurgerHouseStartRoleConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FBurgerHouseStartRoleCondition() = default;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};

////////////////////////////////////////////////////////////////////
// 조건 — 내 주문이 준비됐나

USTRUCT()
struct FBurgerHouseOrderReadyConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Context")
	TObjectPtr<ACustomerCharacter> Customer;
};

/**
 *	내 번호가 준비 상태인가.
 *
 *	⚠ **이걸 Wait 의 전이 조건으로 쓰지 말 것.** §9.5 의 요점은 "Wait 의 전이를
 *	  **On Event** 로 잡는 것" 이다 — 조건 폴링이 아니라 서브시스템이 민다.
 *	  이 조건은 **이벤트를 놓쳤을 때의 복구용**이다 (Pickup 재진입 시 아직
 *	  유효한지 재확인 등).
 */
USTRUCT(meta = (DisplayName = "My Order Is Ready", Category = "BurgerHouse"))
struct FBurgerHouseOrderReadyCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FBurgerHouseOrderReadyConditionInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FBurgerHouseOrderReadyCondition() = default;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif
};
