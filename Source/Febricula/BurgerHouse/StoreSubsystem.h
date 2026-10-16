// 버거리아 — 매장 상태 소유자
//
// UE_NPC_PLAN.md Phase 3.
//
// 설계의 요점 하나 — **주문 번호는 매장의 상태지 손님의 상태가 아니다.**
// NPC 가 번호를 들고 있으면 디스플레이가 NPC 를 뒤져야 하고, NPC 가 사라지면
// 주문이 유령이 된다. 그래서 주문·대기열·슬롯 예약을 전부 여기가 소유하고,
// NPC 와 디스플레이는 서로를 모른 채 이것만 본다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "StoreSubsystem.generated.h"

class ASlotPoint;

/** 주문의 생애 */
UENUM(BlueprintType)
enum class EOrderState : uint8
{
	Queued		UMETA(DisplayName = "접수됨"),
	Preparing	UMETA(DisplayName = "조리중"),
	Ready		UMETA(DisplayName = "준비됨"),
	Claimed		UMETA(DisplayName = "수령됨")
};

USTRUCT(BlueprintType)
struct FOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	int32 Number = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Order")
	EOrderState State = EOrderState::Queued;

	/** ⚠ 약참조여야 한다. NPC 가 파괴돼도 주문이 유령으로 남지 않는다. */
	TWeakObjectPtr<AActor> Customer;

	/**
	 *	손님이 실제로 있었는가.
	 *
	 *	⚠ `Customer.IsValid()` 만으로는 **「디버그로 손님 없이 넣은 주문」과
	 *	  「손님이 파괴된 주문」을 구별할 수 없다.** 전자는 살려 둬야 하고
	 *	  후자는 버려야 한다. 그래서 발급 시점에 기록해 둔다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Order")
	bool bHasCustomer = false;

	/** 남은 조리 시간(초) */
	float CookRemaining = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderReady, int32, Number);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOrderClaimed, int32, Number);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWaitSlotChanged, AActor*, Customer, int32, SlotIndex);

/**
 *	매장의 주문·대기열·슬롯 예약을 소유한다.
 *
 *	`UGameInstanceSubsystem` 이라 레벨 전환에도 살아남는다. 레벨마다 초기화가
 *	필요해지면 `UWorldSubsystem` 으로 내릴 것.
 */
UCLASS()
class UStoreSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ---- Subsystem ----
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- FTickableGameObject ----
	// 조리 타이머와 좀비 청소를 여기서 돈다. 타이머 매니저를 안 쓰는 이유는
	// 주문마다 타이머를 걸면 취소·정리 경로가 늘어나기 때문이다.
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UStoreSubsystem, STATGROUP_Tickables); }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	// ---------------------------------------------------------------- 주문
	/** 번호를 발급하고 조리를 시작한다. 실패하면 0. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Order")
	int32 PlaceOrder(AActor* Customer);

	/** 수령. **중복 수령을 여기서 막는다.** 성공하면 true. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Order")
	bool ClaimOrder(int32 Number, AActor* By);

	/** 디스플레이용 — 준비된 번호들 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Order")
	TArray<int32> GetReadyNumbers() const;

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Order")
	EOrderState GetOrderState(int32 Number) const;

	// ---------------------------------------------------------------- 대기열
	/** 대기열에 넣고 슬롯 인덱스를 돌려준다. 이미 있으면 기존 인덱스. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Queue")
	int32 EnterWaitArea(AActor* Customer);

	/** 빼고 **뒷사람 전원의 인덱스를 당긴다.** */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Queue")
	void LeaveWaitArea(AActor* Customer);

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Queue")
	int32 GetWaitSlot(AActor* Customer) const;

	// ---------------------------------------------------------------- 슬롯 예약
	// ⚠ Smart Objects 로 갈아탈 자리다 (Phase 4). 지금은 플러그인을 켜지 않고
	//   같은 계약(claim/release)만 여기서 제공한다 — 전환할 때 호출부가 안 바뀐다.
	/**
	 *	성공하면 그 슬롯, 실패하면 nullptr. `Kind` 가 비면 아무 종류나.
	 *
	 *	고르는 순서는 둘 중 하나다 —
	 *	  ① `bRandom` 이면 **균등 무작위**
	 *	  ② 아니면 **첫 빈 자리** (결정적 — 디버깅용)
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	ASlotPoint* ClaimSlot(AActor* By, FName Kind, bool bRandom = false);

	/**
	 *	`By` 에게 **가장 가까운** 빈 슬롯을 예약한다. 일행·무작위를 안 탄다.
	 *
	 *	## 이것이 필요한 이유 — 「서 있는 그 의자」를 잡아야 한다
	 *
	 *	프리필 손님은 **의자 위에 직접 낳는다.** 그때 `ClaimSlot` 을 쓰면 무작위로
	 *	**엉뚱한 의자**를 잡고, 손님은 A 의자에 앉아 있는데 예약은 B 의자에
	 *	걸린다. 그러면 나중에 다른 손님이 A 로 걸어와 같은 자리에 겹쳐 앉는다.
	 *
	 *	@param MaxDistance 이보다 멀면 안 잡고 nullptr. 0 이하면 거리 제한 없음
	 *
	 *	⚠ **일행 테이블 배정을 타지 않는다.** 자리를 이미 정해 놓고 부르는
	 *	  함수이므로 「같은 테이블로 모으기」가 개입할 이유가 없다 — 목록을 만든
	 *	  쪽이 이미 같은 테이블을 골라 놨다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	ASlotPoint* ClaimNearestSlot(AActor* By, FName Kind, float MaxDistance = 150.f);

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	void ReleaseSlot(ASlotPoint* Slot, AActor* By);

	/** 그 액터가 잡고 있는 슬롯을 전부 놓는다 (사망·상태 이탈 시). */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	void ReleaseAllSlotsOf(AActor* By);

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	int32 CountFreeSlots(FName Kind) const;

	/**
	 *	**예약하지 않고** 그 종류의 지점 하나를 찾는다. 문(`Entry`/`Exit`)처럼
	 *	여럿이 같이 쓰는 자리에 쓴다.
	 *
	 *	같은 종류가 여러 개면 **무작위로 하나**를 준다 — 문이 2차선이면 손님이
	 *	한 줄로만 드나들지 않는다. 예약이 아니라 조회라 무작위여도 경합이 없다.
	 *
	 *	@return 찾으면 true. 못 찾으면 false 이고 `OutTransform` 은 안 건드린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	bool FindPoint(FName Kind, FTransform& OutTransform);

	// ---------------------------------------------------------------- 일행
	/**
	 *	일행에 한 명 들어온다. 스폰 직후 손님이 스스로 부른다.
	 *
	 *	`Size` 는 **처음 한 번만** 기록한다 — 뒤에 오는 멤버가 다른 값을 들고
	 *	와도 무시한다. 스포너가 전원 태어난 뒤에 통보하므로 같은 값이 오지만,
	 *	그걸 믿지 않고 첫 값을 붙든다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Party")
	void JoinParty(int32 PartyId, int32 Size);

	/** 한 명 나간다. **마지막 한 명이 나가면 테이블 배정이 풀린다.** */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Party")
	void LeaveParty(int32 PartyId);

	/** 그 일행이 앉기로 한 테이블. 아직 안 정했으면 −1. */
	UFUNCTION(BlueprintPure, Category = "BurgerHouse|Party")
	int32 GetPartyTable(int32 PartyId) const;

	/**
	 *	일행이 **같은 테이블에 못 앉을 때** 어떻게 할 것인가.
	 *
	 *	true  — 아무 자리나 잡는다 (흩어져 앉는다). 손님이 멈추지 않는다
	 *	false — 자리를 못 잡고 실패한다. 트리가 기다리거나 나가야 한다
	 *
	 *	⚠ 기본 true. 좌석 37개에 최대 8명이라 갈릴 일이 드물지만, 갈렸을 때
	 *	  **일행이 흩어지는 것**과 **손님이 문 앞에 굳는 것** 중에는 전자가 낫다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Config")
	bool bSplitPartyWhenTableFull = true;

	// ---------------------------------------------------------------- 좌석 선호도
	//
	// ⚠ **좌석 히트맵 가중은 이식하지 않았다.** `ClaimSlot` 은 **배열에서 처음
	//   만난 빈 슬롯**을 돌려준다 — `TActorIterator` 순서라 사실상 고정이고,
	//   손님이 몇 명이 오든 **늘 같은 순서로 같은 자리**를 채운다. 창가가 늘
	//   비고 구석이 늘 먼저 찬다.
	//
	//   다양성이 필요하면 `ClaimSlot(bRandom=true)` 로 균등 무작위를 켠다.
	//   ⚠ 무작위는 재현을 깨므로 한 손님을 쫓아 디버깅할 때는 꺼 둘 것.

	// ---------------------------------------------------------------- 이벤트
	UPROPERTY(BlueprintAssignable, Category = "BurgerHouse|Order")
	FOnOrderReady OnOrderReady;

	UPROPERTY(BlueprintAssignable, Category = "BurgerHouse|Order")
	FOnOrderClaimed OnOrderClaimed;

	UPROPERTY(BlueprintAssignable, Category = "BurgerHouse|Queue")
	FOnWaitSlotChanged OnWaitSlotChanged;

	// ---------------------------------------------------------------- 설정
	/** 조리 시간(초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Config")
	float CookSeconds = 12.f;

	/** 좀비 청소 주기(초) — 파괴된 액터가 잡은 슬롯·주문을 회수한다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Config")
	float SweepSeconds = 3.f;

	/** 디버그 — 화면에 상태를 찍는다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Config")
	bool bDebugLog = true;

	/** 레벨에 있는 슬롯을 다시 훑는다. 슬롯 액터를 런타임에 스폰했으면 호출. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	void RefreshSlots();

	/** 디버그용 — NPC 없이 주문 하나를 밀어 넣는다 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Debug")
	int32 DebugPlaceOrder();

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Debug")
	FString DebugDump() const;

	// ---------------------------------------------------------------- 콘솔
	// ⚠ `UGameInstance::ProcessConsoleExec` 가 서브시스템으로 넘겨 주므로
	//   여기 `Exec` 를 달면 **PIE 콘솔(`~`)에서 바로 부를 수 있다.**
	//   블루프린트를 하나도 안 만들고 Phase 3 의 통과 기준을 확인하려는 것이다.
	//   (`UE_NPC_PLAN.md` — "디버그 키로 PlaceOrder 를 눌러 번호가 찍히고
	//    N초 뒤 OnOrderReady 가 발화한다. NPC 도 디스플레이도 없는 상태에서.")

	/** `BurgerHouseOrder` — 손님 없이 주문 하나 발행 */
	UFUNCTION(Exec)
	void BurgerHouseOrder();

	/** `BurgerHouseDump` — 주문·대기열·슬롯 현황 */
	UFUNCTION(Exec)
	void BurgerHouseDump();

	/** `BurgerHouseSlots` — 슬롯 다시 훑고 종류별 집계 */
	UFUNCTION(Exec)
	void BurgerHouseSlots();

	/** `BurgerHouseClaim Seat` — 종류 하나를 잡아 본다 (경합 확인용) */
	UFUNCTION(Exec)
	void BurgerHouseClaim(const FString& Kind);

	/** `BurgerHouseReleaseAll` — 잡힌 슬롯 전부 회수 */
	UFUNCTION(Exec)
	void BurgerHouseReleaseAll();

	/**
	 *	`BurgerHouseReady <번호>` — 조리를 건너뛰고 즉시 준비 상태로 만든다.
	 *
	 *	Wait → Pickup 전이(§9.5 ★)를 손볼 때 매번 `CookSeconds` 를 기다리면
	 *	반복이 느려진다. 번호를 0 으로 주면 조리중인 것 전부.
	 */
	UFUNCTION(Exec)
	void BurgerHouseReady(int32 Number);

private:
	UPROPERTY()
	TArray<FOrder> Orders;

	/** 대기열 — **인덱스가 곧 줄 위치**다 */
	TArray<TWeakObjectPtr<AActor>> WaitQueue;

	/** 레벨의 슬롯 액터. 약참조 — 레벨 언로드에 안 걸린다 */
	TArray<TWeakObjectPtr<ASlotPoint>> Slots;

	/** 일행 하나의 상태 */
	struct FParty
	{
		int32 TableId = -1;		// 앉기로 한 테이블 (`ASlotPoint::GroupId`). −1 = 미정
		int32 Size = 0;			// 처음 기록된 인원
		int32 Living = 0;		// 아직 남은 인원. 0 이 되면 항목을 지운다
	};
	TMap<int32, FParty> Parties;

	/**
	 *	그 일행이 앉을 테이블을 고른다. 이미 골랐으면 그것을 돌려준다.
	 *
	 *	@return 테이블 번호. 마땅한 곳이 없으면 −1.
	 */
	int32 ResolvePartyTable(int32 PartyId, const TArray<ASlotPoint*>& FreeSeats);

	int32 NextNumber = 1;
	float SweepAccum = 0.f;
	bool bSlotsCached = false;

	FOrder* FindOrder(int32 Number);
	const FOrder* FindOrder(int32 Number) const;
	void Sweep();

	/**
	 *	`FromIndex` **이후만** 다시 알린다.
	 *
	 *	⚠ 전원에게 알리면 안 움직인 사람까지 `Queue.Advanced` 를 받아 MoveTo 를
	 *	  다시 태운다 — 줄이 통째로 덜컥거린다. 앞사람이 빠졌을 때 자리가 바뀌는
	 *	  것은 **그 뒤 사람들뿐**이다.
	 */
	void ReindexQueue(int32 FromIndex = 0);

	/** 준비 상태로 바꾸고 알린다 (조리 완료 · 콘솔 강제 공용) */
	void MarkReady(FOrder& O);
};
