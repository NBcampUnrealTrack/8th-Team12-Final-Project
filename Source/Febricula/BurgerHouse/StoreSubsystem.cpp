#include "StoreSubsystem.h"

#include "BurgerHouseTags.h"
#include "CustomerCharacter.h"
#include "SlotPoint.h"
#include "EngineUtils.h"
#include "Components/StateTreeComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogBurgerHouse, Log, All);

// ---------------------------------------------------------------- 이벤트 밀기
//
// §9.5 — "★ Wait 의 전이를 「On Event」로 잡는 것이 이 설계의 요점이다. 조건
// 폴링이 아니라 Subsystem 이 `SendStateTreeEvent` 로 민다."
//
// ## 왜 서브시스템이 「직접」 미는가
//
// NPC 가 `OnOrderReady` 를 구독해 자기 번호인지 걸러도 되지만, 그러면 손님마다
// 구독·해지 경로가 생기고 **그게 또 하나의 「릴리스 누락」 자리**가 된다
// (§9.5 1번 버그와 같은 부류). 주문은 이미 담당 손님을 알고 있으므로
// **그 한 명에게만** 밀면 거르기도 구독도 필요 없다.

namespace
{
	/**
	 *	액터의 StateTree 컴포넌트를 찾는다.
	 *
	 *	손님은 폰이고 트리는 **AI 컨트롤러**가 들고 있으므로 (Combat 변형과 같은
	 *	구조) 폰에서 못 찾으면 컨트롤러를 본다. `UStateTreeAIComponent` 는
	 *	`UStateTreeComponent` 파생이라 이 한 번의 조회로 둘 다 잡힌다.
	 */
	UStateTreeComponent* FindTree(AActor* A)
	{
		if (!A)
		{
			return nullptr;
		}
		if (UStateTreeComponent* C = A->FindComponentByClass<UStateTreeComponent>())
		{
			return C;
		}
		if (const APawn* P = Cast<APawn>(A))
		{
			if (AController* Ctrl = P->GetController())
			{
				return Ctrl->FindComponentByClass<UStateTreeComponent>();
			}
		}
		return nullptr;
	}

	/**
	 *	한 명에게만 민다. 트리가 없으면 넘어간다 — 디버그 주문은 주인이 없다.
	 *
	 *	## ⚠⚠ **보낸 태그를 반드시 찍는다**
	 *
	 *	여기가 조용하면 진단이 막힌다. 실제로 한 번 당했다 — 트리의
	 *	`RequiredEventTag` 가 개명 전 이름(`Burgeria.*`)으로 남아 있어서
	 *	`BurgerHouse.Order.Ready` 를 아무리 보내도 **아무도 안 받았다.**
	 *
	 *	**전송 자체는 성공한다.** 에러도 경고도 없다. 증상은 「손님이 줄에서 안
	 *	나간다」 하나뿐이라, 이 줄이 없으면 어느 태그가 오갔는지 알 길이 없다.
	 *
	 *	⚠ `Verbose` 다. 보려면 `Log LogBurgerHouse Verbose`.
	 */
	void PushEvent(AActor* To, const FGameplayTag& Tag)
	{
		UStateTreeComponent* Tree = FindTree(To);
		if (!Tree)
		{
			UE_LOG(LogBurgerHouse, Verbose, TEXT("이벤트 %s — %s 에 트리가 없다"),
				*Tag.ToString(), *GetNameSafe(To));
			return;
		}
		UE_LOG(LogBurgerHouse, Verbose, TEXT("이벤트 %s -> %s"),
			*Tag.ToString(), *GetNameSafe(To));
		Tree->SendStateTreeEvent(Tag);
	}
}

// ---------------------------------------------------------------- Subsystem

void UStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogBurgerHouse, Log, TEXT("StoreSubsystem 시작"));
}

void UStoreSubsystem::Deinitialize()
{
	Orders.Reset();
	WaitQueue.Reset();
	Slots.Reset();
	bSlotsCached = false;
	Super::Deinitialize();
}

UWorld* UStoreSubsystem::GetTickableGameObjectWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetWorld() : nullptr;
}

void UStoreSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetTickableGameObjectWorld();
	if (!World || World->IsPaused())
	{
		return;
	}

	// ---- 조리 ----
	// 배열 하나를 도는 게 주문마다 타이머를 거는 것보다 취소·정리 경로가 짧다.
	for (FOrder& O : Orders)
	{
		if (O.State != EOrderState::Queued && O.State != EOrderState::Preparing)
		{
			continue;
		}
		O.State = EOrderState::Preparing;
		O.CookRemaining -= DeltaTime;
		if (O.CookRemaining <= 0.f)
		{
			MarkReady(O);
		}
	}

	// ---- 좀비 청소 ----
	SweepAccum += DeltaTime;
	if (SweepAccum >= SweepSeconds)
	{
		SweepAccum = 0.f;
		Sweep();
	}
}

// ---------------------------------------------------------------- 주문

FOrder* UStoreSubsystem::FindOrder(int32 Number)
{
	return Orders.FindByPredicate([Number](const FOrder& O) { return O.Number == Number; });
}

const FOrder* UStoreSubsystem::FindOrder(int32 Number) const
{
	return Orders.FindByPredicate([Number](const FOrder& O) { return O.Number == Number; });
}

void UStoreSubsystem::MarkReady(FOrder& O)
{
	O.State = EOrderState::Ready;
	O.CookRemaining = 0.f;

	if (bDebugLog)
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("주문 %d 준비됨"), O.Number);
	}

	// ⚠ 브로드캐스트는 상태를 바꾼 **뒤에** 한다. 구독자가 곧바로
	//    `GetOrderState` 를 물어볼 수 있다.
	OnOrderReady.Broadcast(O.Number);				// 디스플레이(Phase 5) 용 — 전체 공지
	PushEvent(O.Customer.Get(), BurgerHouseTags::Order_Ready);	// ★ 담당 손님 한 명에게만
}

int32 UStoreSubsystem::PlaceOrder(AActor* Customer)
{
	FOrder O;
	O.Number = NextNumber++;
	O.State = EOrderState::Queued;
	O.Customer = Customer;
	O.bHasCustomer = (Customer != nullptr);
	O.CookRemaining = CookSeconds;
	Orders.Add(O);

	if (bDebugLog)
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("주문 %d 접수 (%s) — 조리 %.1fs"),
			O.Number, *GetNameSafe(Customer), CookSeconds);
	}
	return O.Number;
}

bool UStoreSubsystem::ClaimOrder(int32 Number, AActor* By)
{
	FOrder* O = FindOrder(Number);
	if (!O)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("수령 실패 — 주문 %d 없음"), Number);
		return false;
	}
	// ⚠ **중복 수령 차단이 이 함수의 존재 이유다.** 둘이 동시에 손을 뻗어도
	//    먼저 온 쪽만 성공한다.
	if (O->State != EOrderState::Ready)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("수령 실패 — 주문 %d 상태 %d"),
			Number, (int32)O->State);
		return false;
	}
	// 남의 주문을 가져가는 것도 막는다. 담당자가 이미 사라졌으면 허용한다.
	if (O->Customer.IsValid() && O->Customer.Get() != By)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("수령 실패 — 주문 %d 는 %s 것"),
			Number, *GetNameSafe(O->Customer.Get()));
		return false;
	}

	O->State = EOrderState::Claimed;
	if (bDebugLog)
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("주문 %d 수령 (%s)"), Number, *GetNameSafe(By));
	}
	OnOrderClaimed.Broadcast(Number);
	PushEvent(By, BurgerHouseTags::Order_Claimed);
	return true;
}

TArray<int32> UStoreSubsystem::GetReadyNumbers() const
{
	TArray<int32> Out;
	for (const FOrder& O : Orders)
	{
		if (O.State == EOrderState::Ready)
		{
			Out.Add(O.Number);
		}
	}
	return Out;
}

EOrderState UStoreSubsystem::GetOrderState(int32 Number) const
{
	const FOrder* O = FindOrder(Number);
	return O ? O->State : EOrderState::Claimed;
}

// ---------------------------------------------------------------- 대기열

int32 UStoreSubsystem::EnterWaitArea(AActor* Customer)
{
	if (!Customer)
	{
		return INDEX_NONE;
	}
	const int32 Existing = GetWaitSlot(Customer);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}
	const int32 Index = WaitQueue.Add(Customer);
	OnWaitSlotChanged.Broadcast(Customer, Index);
	return Index;
}

void UStoreSubsystem::LeaveWaitArea(AActor* Customer)
{
	const int32 Index = GetWaitSlot(Customer);
	if (Index == INDEX_NONE)
	{
		return;
	}
	WaitQueue.RemoveAt(Index);
	// ⚠ **뒷사람 전원을 당긴다.** Smart Object 로는 못 하는 것이 이 부분이고,
	//    대기열을 Subsystem 이 소유하는 이유다.
	//    빠진 자리 **뒤부터**다 — 앞사람들의 자리는 그대로다.
	ReindexQueue(Index);
}

void UStoreSubsystem::ReindexQueue(int32 FromIndex)
{
	for (int32 i = FMath::Max(FromIndex, 0); i < WaitQueue.Num(); ++i)
	{
		if (AActor* A = WaitQueue[i].Get())
		{
			OnWaitSlotChanged.Broadcast(A, i);
			// ★ 자리가 바뀐 사람만 다시 걷게 한다.
			//   ⚠ 이벤트여야 하는 이유 — MoveTo 의 `Destination` 바인딩은 상태에
			//     **들어갈 때 한 번** 복사된다. 값만 바꾸면 아무도 안 움직인다.
			PushEvent(A, BurgerHouseTags::Queue_Advanced);
		}
	}
}

int32 UStoreSubsystem::GetWaitSlot(AActor* Customer) const
{
	for (int32 i = 0; i < WaitQueue.Num(); ++i)
	{
		if (WaitQueue[i].Get() == Customer)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

// ---------------------------------------------------------------- 슬롯

void UStoreSubsystem::RefreshSlots()
{
	Slots.Reset();
	UWorld* World = GetTickableGameObjectWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ASlotPoint> It(World); It; ++It)
	{
		Slots.Add(*It);
	}
	bSlotsCached = true;
	UE_LOG(LogBurgerHouse, Log, TEXT("슬롯 %d개 수집"), Slots.Num());
}

// ---------------------------------------------------------------- 일행

void UStoreSubsystem::JoinParty(int32 PartyId, int32 Size)
{
	if (PartyId <= 0)
	{
		return;			// 0 은 「일행 아님」
	}
	FParty& P = Parties.FindOrAdd(PartyId);
	if (P.Size == 0)
	{
		P.Size = FMath::Max(Size, 1);
	}
	++P.Living;
}

void UStoreSubsystem::LeaveParty(int32 PartyId)
{
	FParty* P = Parties.Find(PartyId);
	if (!P)
	{
		return;
	}
	if (--P->Living <= 0)
	{
		// 마지막 한 명이 나갔다. 테이블 배정을 풀어 다음 일행이 쓸 수 있게 한다.
		Parties.Remove(PartyId);
	}
}

int32 UStoreSubsystem::GetPartyTable(int32 PartyId) const
{
	const FParty* P = Parties.Find(PartyId);
	return P ? P->TableId : -1;
}

int32 UStoreSubsystem::ResolvePartyTable(int32 PartyId, const TArray<ASlotPoint*>& FreeSeats)
{
	FParty* P = Parties.Find(PartyId);
	if (!P)
	{
		return -1;
	}
	if (P->TableId >= 0)
	{
		return P->TableId;			// 먼저 앉은 사람이 이미 정했다
	}

	// ## ⚠⚠ **남이 앉은 테이블은 후보에서 뺀다**
	//
	// 빈 자리만 세면 「프리필 손님이 이미 앉아 있는 테이블」이 자리가 남았다는
	// 이유로 후보가 된다. 그러면 걸어 들어온 손님이 **모르는 사람 옆에 합석**한다.
	//
	// ⚠ 프리필은 `ClaimNearestSlot` 으로 자리를 잡아서 `ResolvePartyTable` 을
	//   거치지 않는다. 그래서 여기서 걸러 주지 않으면 겹치는 것을 막을 곳이 없다.
	//
	// ⚠ 판정은 **좌석의 `ClaimedBy`** 로 한다. 빈 자리 목록만 봐서는 「비어
	//   있다」만 알 뿐 「누가 옆에 앉아 있나」를 알 수 없다.
	TSet<int32> TablesTakenByOthers;
	for (TActorIterator<ASlotPoint> It(GetWorld()); It; ++It)
	{
		const ASlotPoint* S = *It;
		if (!S || S->Kind != BurgerHouseSlot::Seat || S->GroupId < 0)
		{
			continue;
		}
		const AActor* By = S->ClaimedBy.Get();
		if (!By)
		{
			continue;
		}
		// 같은 일행이면 괜찮다 — 오히려 그 테이블로 가야 한다.
		const ACustomerCharacter* C = Cast<ACustomerCharacter>(By);
		if (C && C->PartyId == PartyId)
		{
			continue;
		}
		TablesTakenByOthers.Add(S->GroupId);
	}

	// 테이블별로 빈 자리를 센다.
	TMap<int32, int32> FreePerTable;
	for (const ASlotPoint* S : FreeSeats)
	{
		if (S->GroupId >= 0 && !TablesTakenByOthers.Contains(S->GroupId))
		{
			++FreePerTable.FindOrAdd(S->GroupId);
		}
	}

	// ⚠ 남이 다 차지해서 후보가 없으면 **겹치는 것을 허용한다.** 아예 못 앉는
	//   것보다는 낫다 — 좌석 37개에 정원 8명이라 평소에는 안 걸린다.
	if (FreePerTable.Num() == 0 && TablesTakenByOthers.Num() > 0)
	{
		UE_LOG(LogBurgerHouse, Log,
			TEXT("일행 %d — 빈 테이블이 없다. 합석을 허용한다 (점유 테이블 %d개)"),
			PartyId, TablesTakenByOthers.Num());
		for (const ASlotPoint* S : FreeSeats)
		{
			if (S->GroupId >= 0)
			{
				++FreePerTable.FindOrAdd(S->GroupId);
			}
		}
	}
	if (FreePerTable.Num() == 0)
	{
		// ⚠ 좌석에 `GroupId` 가 안 박혀 있다 — `ll_slots.py` 를 안 돌렸거나
		//   옛 버전으로 돌렸다. 조용히 아무 데나 앉히면 「일행이 왜 흩어지지」로
		//   보이므로 여기서 말해 준다.
		UE_LOG(LogBurgerHouse, Warning,
			TEXT("일행 %d — 빈 좌석에 GroupId 가 하나도 없다. ll_slots.py 를 다시 돌릴 것"),
			PartyId);
		return -1;
	}

	// ⚠ **일행이 다 앉을 수 있는 테이블만 후보로 친다.** 자리가 모자란 곳을
	//    골라 놓으면 뒤에 오는 멤버가 앉을 데가 없어 흩어진다 — 그럴 거면
	//    처음부터 안 묶는 게 낫다.
	//
	//    같은 조건이면 **가장 딱 맞는 테이블**을 고른다. 2명이 왔는데 6인석을
	//    차지하면 뒤에 오는 4인 일행이 갈 곳을 잃는다.
	int32 Best = -1;
	int32 BestFree = MAX_int32;
	for (const TPair<int32, int32>& Pair : FreePerTable)
	{
		if (Pair.Value < P->Size)
		{
			continue;
		}
		if (Pair.Value < BestFree)
		{
			BestFree = Pair.Value;
			Best = Pair.Key;
		}
	}

	if (Best < 0)
	{
		// 일행 전원이 앉을 테이블이 없다.
		UE_LOG(LogBurgerHouse, Log,
			TEXT("일행 %d(%d명) — %d명이 앉을 테이블이 없다. %s"),
			PartyId, P->Size, P->Size,
			bSplitPartyWhenTableFull ? TEXT("흩어져 앉는다") : TEXT("자리를 안 준다"));
		return -1;
	}

	P->TableId = Best;
	if (bDebugLog)
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("일행 %d(%d명) → 테이블 %d (빈자리 %d)"),
			PartyId, P->Size, Best, BestFree);
	}
	return Best;
}

ASlotPoint* UStoreSubsystem::ClaimNearestSlot(AActor* By, FName Kind, float MaxDistance)
{
	if (!By)
	{
		return nullptr;
	}
	if (!bSlotsCached)
	{
		RefreshSlots();
	}

	// 이미 그 종류를 들고 있으면 그것 (`ClaimSlot` 과 같은 멱등 규칙).
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		ASlotPoint* S = W.Get();
		if (S && S->Kind == Kind && S->ClaimedBy.Get() == By)
		{
			return S;
		}
	}

	// ⚠ **사용 지점** 기준으로 잰다. 슬롯 원점은 의자 한복판이고 손님은 그
	//    앞쪽 사용 지점에 서 있으므로, 원점으로 재면 옆 의자가 더 가깝게 나온다.
	const FVector From = By->GetActorLocation();
	ASlotPoint* Best = nullptr;
	float BestSq = MAX_flt;

	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		ASlotPoint* S = W.Get();
		if (!S || (!Kind.IsNone() && S->Kind != Kind) || !S->IsFree())
		{
			continue;
		}
		// Z 는 무시한다 — 캡슐 반높이만큼 손님이 위에 있다.
		const float DistSq = FVector::DistSquared2D(From, S->GetUseTransform().GetLocation());
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = S;
		}
	}

	if (!Best)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("가까운 빈 슬롯 없음 — 종류 %s"), *Kind.ToString());
		return nullptr;
	}

	if (MaxDistance > 0.f && BestSq > MaxDistance * MaxDistance)
	{
		// ⚠ 조용히 먼 자리를 잡아 주지 않는다. 손님이 A 에 서 있는데 B 를
		//    예약하면 나중에 다른 손님이 A 로 걸어와 겹쳐 앉는다.
		UE_LOG(LogBurgerHouse, Warning,
			TEXT("%s 에게 가장 가까운 %s 가 %.0fcm 떨어져 있다 (한계 %.0f) — 안 잡는다. 스폰 자리가 슬롯과 어긋났는지 볼 것"),
			*By->GetName(), *Kind.ToString(), FMath::Sqrt(BestSq), MaxDistance);
		return nullptr;
	}

	Best->ClaimedBy = By;
	UE_LOG(LogBurgerHouse, Log, TEXT("슬롯 %s(%s) 예약 — %s (최근접 %.0fcm)"),
		*Best->GetName(), *Kind.ToString(), *By->GetName(), FMath::Sqrt(BestSq));
	return Best;
}

ASlotPoint* UStoreSubsystem::ClaimSlot(AActor* By, FName Kind, bool bRandom)
{
	if (!bSlotsCached)
	{
		RefreshSlots();
	}

	// ---- 이미 그 종류를 잡고 있으면 **그것을 돌려준다** ----
	//
	// ## 왜 멱등이어야 하는가
	//
	// 프리필 손님은 스폰 시점에 C++ 이 자리를 잡아 앉혀 놓고
	// (`ACustomerCharacter::ApplyStartRole`), 그 뒤 StateTree 의 `Claim Slot` 이
	// **같은 종류를 또 잡으러 온다.** 멱등이 아니면 그때 **두 번째 자리**가
	// 예약되고, 첫 자리는 아무도 안 놓아 `SweepSeconds` 동안 죽는다.
	// 손님 넷이 앉으면 자리 여덟이 잠기는 셈이라 금방 「자리 없음」이 된다.
	//
	// ⚠ 증상이 조용하다 — 앉기는 앉으므로 눈에 안 보이고, 한참 뒤에
	//   `빈 슬롯 없음 — 종류 Seat` 로만 나타난다.
	//
	// ⚠ **한 종류에 하나만** 이 규칙이다. 손님은 좌석 하나·키오스크 하나를
	//   동시에 들 수 있다 (실제로 그런 구간이 없지만 막을 이유도 없다).
	if (By && !Kind.IsNone())
	{
		for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
		{
			ASlotPoint* S = W.Get();
			if (S && S->Kind == Kind && S->ClaimedBy.Get() == By)
			{
				return S;
			}
		}
	}

	// ---- 후보 수집 ----
	TArray<ASlotPoint*> Free;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		ASlotPoint* S = W.Get();
		if (!S)
		{
			continue;
		}
		if (!Kind.IsNone() && S->Kind != Kind)
		{
			continue;
		}
		if (!S->IsFree())
		{
			continue;
		}
		Free.Add(S);
	}

	if (Free.Num() == 0)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("빈 슬롯 없음 — 종류 %s"), *Kind.ToString());
		return nullptr;
	}

	// ---- 일행은 같은 테이블로 ----
	//
	// ⚠ 좌석에만 적용한다. 키오스크·픽업은 한 명씩 쓰는 자리라 묶을 이유가 없다.
	if (Kind == BurgerHouseSlot::Seat)
	{
		if (const ACustomerCharacter* C = Cast<ACustomerCharacter>(By))
		{
			if (C->PartyId > 0)
			{
				const int32 Table = ResolvePartyTable(C->PartyId, Free);
				if (Table >= 0)
				{
					// 그 테이블의 빈 자리만 남긴다. **의자는 다르게** 앉는다 —
					// 슬롯 하나에 한 명이라 예약만으로 자동으로 갈린다.
					Free.RemoveAll([Table](const ASlotPoint* S) { return S->GroupId != Table; });
				}
				else if (!bSplitPartyWhenTableFull)
				{
					// 같이 못 앉으면 아예 안 앉는다 (설정이 그렇게 돼 있다).
					return nullptr;
				}
				// Table < 0 이고 흩어져도 되면 그냥 아래로 — 아무 자리나 잡는다.
			}
		}
	}

	// ---- 고르기 ----
	//
	// ⚠ 아무 옵션도 없으면 **첫 번째**를 그대로 준다 — 결정적이라 디버깅에 쓴다.
	//    무작위는 재현을 깨므로 **명시적으로 켜는 것**이어야 한다 (헤더 주석).
	ASlotPoint* Chosen = bRandom ? Free[FMath::RandRange(0, Free.Num() - 1)] : Free[0];

	Chosen->ClaimedBy = By;
	if (bDebugLog)
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("슬롯 %s(%s) 예약 — %s (후보 %d)"),
			*Chosen->GetName(), *Chosen->Kind.ToString(), *GetNameSafe(By), Free.Num());
	}
	return Chosen;
}

void UStoreSubsystem::ReleaseSlot(ASlotPoint* Slot, AActor* By)
{
	if (!Slot)
	{
		return;
	}
	// 남이 잡은 것을 놓아 주지 않는다 — 그러면 두 명이 같은 자리에 앉는다
	if (Slot->ClaimedBy.IsValid() && Slot->ClaimedBy.Get() != By)
	{
		UE_LOG(LogBurgerHouse, Warning, TEXT("슬롯 %s 해제 거부 — %s 것이 아님"),
			*Slot->GetName(), *GetNameSafe(By));
		return;
	}
	Slot->ClaimedBy = nullptr;
}

void UStoreSubsystem::ReleaseAllSlotsOf(AActor* By)
{
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		if (ASlotPoint* S = W.Get())
		{
			if (S->ClaimedBy.Get() == By)
			{
				S->ClaimedBy = nullptr;
			}
		}
	}
}

bool UStoreSubsystem::FindPoint(FName Kind, FTransform& OutTransform)
{
	if (!bSlotsCached)
	{
		RefreshSlots();
	}

	TArray<const ASlotPoint*> Matches;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		if (const ASlotPoint* S = W.Get())
		{
			if (S->Kind == Kind)
			{
				Matches.Add(S);
			}
		}
	}

	if (Matches.Num() == 0)
	{
		// ⚠ 조용히 false 만 돌려주면 "손님이 안 들어온다" 로만 보인다.
		//    어떤 종류를 못 찾았는지 찍는다 — 오타인지 미배치인지 갈린다.
		UE_LOG(LogBurgerHouse, Warning,
			TEXT("지점 없음 — 종류 '%s'. ll_slots.py 를 돌렸는지 볼 것"),
			*Kind.ToString());
		return false;
	}

	// 예약이 아니라 조회다. 무작위여도 경합이 없고, 문이 2차선이면
	// 손님이 한 줄로만 드나들지 않는다.
	OutTransform = Matches[FMath::RandRange(0, Matches.Num() - 1)]->GetUseTransform();
	return true;
}

int32 UStoreSubsystem::CountFreeSlots(FName Kind) const
{
	int32 N = 0;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		const ASlotPoint* S = W.Get();
		if (S && (Kind.IsNone() || S->Kind == Kind) && S->IsFree())
		{
			++N;
		}
	}
	return N;
}

// ---------------------------------------------------------------- 청소

void UStoreSubsystem::Sweep()
{
	// ⚠ **릴리스 누락이 이 시스템의 1번 버그다.** 태스크의 `ExitState` 에서
	//    놓는 것이 정석이지만, NPC 가 파괴되거나 StateTree 가 다른 상태로 튀면
	//    그 경로를 안 탄다. 그래서 주기적으로 훑어 회수한다.
	int32 FreedSlots = 0;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		ASlotPoint* S = W.Get();
		if (S && !S->IsFree() && !S->ClaimedBy.IsValid())
		{
			S->ClaimedBy = nullptr;
			++FreedSlots;
		}
	}

	const int32 Before = WaitQueue.Num();
	WaitQueue.RemoveAll([](const TWeakObjectPtr<AActor>& A) { return !A.IsValid(); });
	const int32 DroppedQueue = Before - WaitQueue.Num();
	if (DroppedQueue > 0)
	{
		// 어디가 빠졌는지 모르므로 전원 재색인이 맞다
		ReindexQueue(0);
	}

	// ---- 이벤트 재송신 ----
	//
	// ⚠ **이벤트 하나를 놓치면 손님은 Wait 에서 영원히 선다.** 놓치는 경로가
	//    실제로 있다 — `PlaceOrder` 가 `StartLogic` 보다 먼저 돌거나, 빙의가
	//    다시 걸리면 그 사이의 `Order.Ready` 는 아무 데도 안 간다.
	//
	//    그래서 슬롯 회수와 같은 방식으로 **주기적으로 다시 민다.** 이미 넘어간
	//    손님에게는 아무 일도 안 일어난다 (Wait 상태가 아니면 전이가 없다) —
	//    다시 밀어도 안전한 것이 이 설계가 이벤트를 쓰는 덕이다.
	for (const TWeakObjectPtr<AActor>& W : WaitQueue)
	{
		AActor* A = W.Get();
		if (!A)
		{
			continue;
		}
		const FOrder* Mine = Orders.FindByPredicate(
			[A](const FOrder& O) { return O.Customer.Get() == A; });

		if (!Mine)
		{
			// 줄에 서 있는데 주문이 아예 없다 — 안 알리면 영원히 선다
			PushEvent(A, BurgerHouseTags::Order_Lost);
		}
		else if (Mine->State == EOrderState::Ready)
		{
			PushEvent(A, BurgerHouseTags::Order_Ready);
		}
	}

	// 담당자가 사라진 미수령 주문은 버린다 — 안 그러면 번호판에 영원히 남는다
	const int32 OrdersBefore = Orders.Num();
	Orders.RemoveAll([](const FOrder& O)
	{
		return O.State == EOrderState::Claimed
			|| (O.bHasCustomer && !O.Customer.IsValid());
	});
	const int32 DroppedOrders = OrdersBefore - Orders.Num();

	if (bDebugLog && (FreedSlots || DroppedQueue || DroppedOrders))
	{
		UE_LOG(LogBurgerHouse, Log, TEXT("청소 — 슬롯 %d / 대기열 %d / 주문 %d 회수"),
			FreedSlots, DroppedQueue, DroppedOrders);
	}
}

// ---------------------------------------------------------------- 디버그

int32 UStoreSubsystem::DebugPlaceOrder()
{
	return PlaceOrder(nullptr);
}

// ---------------------------------------------------------------- 콘솔

/** 화면과 로그에 동시에 — PIE 에서 콘솔을 닫아도 보이게 */
static void BurgerHouseTell(const FString& Msg)
{
	UE_LOG(LogBurgerHouse, Log, TEXT("%s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan, Msg);
	}
}

void UStoreSubsystem::BurgerHouseOrder()
{
	const int32 N = DebugPlaceOrder();
	BurgerHouseTell(FString::Printf(TEXT("주문 %d 발행 — %.0f초 뒤 준비됨"),
		N, CookSeconds));
}

void UStoreSubsystem::BurgerHouseDump()
{
	BurgerHouseTell(DebugDump());
}

void UStoreSubsystem::BurgerHouseSlots()
{
	RefreshSlots();
	BurgerHouseTell(FString::Printf(TEXT("슬롯 %d개 — 좌석 %d / 키오스크 %d / 픽업 %d (자유)"),
		Slots.Num(),
		CountFreeSlots(BurgerHouseSlot::Seat),
		CountFreeSlots(BurgerHouseSlot::Kiosk),
		CountFreeSlots(BurgerHouseSlot::Pickup)));
}

void UStoreSubsystem::BurgerHouseClaim(const FString& Kind)
{
	// 콘솔에서 부르는 것이라 손님 액터가 없다. 서브시스템 자신을 주인으로
	// 삼으면 `AActor*` 가 아니라 컴파일이 안 되므로 nullptr 로 잡는다 —
	// ⚠ nullptr 로 잡으면 `IsFree()` 가 곧바로 true 라 예약이 안 걸린다.
	//   그래서 경합 확인용으로는 **플레이어 폰**을 주인으로 쓴다.
	AActor* By = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* W = GI->GetWorld())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				By = PC->GetPawn() ? Cast<AActor>(PC->GetPawn()) : Cast<AActor>(PC);
			}
		}
	}
	if (!By)
	{
		BurgerHouseTell(TEXT("예약 실패 — 주인이 될 액터가 없다 (PIE 에서 부를 것)"));
		return;
	}
	ASlotPoint* S = ClaimSlot(By, FName(*Kind));
	BurgerHouseTell(S
		? FString::Printf(TEXT("%s 예약 성공 — %s"), *Kind, *S->GetName())
		: FString::Printf(TEXT("%s 예약 실패 — 빈 슬롯 없음"), *Kind));
}

void UStoreSubsystem::BurgerHouseReady(int32 Number)
{
	int32 N = 0;
	for (FOrder& O : Orders)
	{
		if (O.State != EOrderState::Queued && O.State != EOrderState::Preparing)
		{
			continue;
		}
		if (Number != 0 && O.Number != Number)
		{
			continue;
		}
		MarkReady(O);
		++N;
	}
	BurgerHouseTell(N > 0
		? FString::Printf(TEXT("주문 %d건 강제 준비"), N)
		: FString::Printf(TEXT("조리중인 주문 없음 (요청 %d)"), Number));
}

void UStoreSubsystem::BurgerHouseReleaseAll()
{
	int32 N = 0;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		if (ASlotPoint* S = W.Get())
		{
			if (!S->IsFree())
			{
				S->ClaimedBy = nullptr;
				++N;
			}
		}
	}
	BurgerHouseTell(FString::Printf(TEXT("슬롯 %d개 회수"), N));
}

FString UStoreSubsystem::DebugDump() const
{
	FString S = FString::Printf(TEXT("주문 %d / 대기열 %d / 슬롯 %d"),
		Orders.Num(), WaitQueue.Num(), Slots.Num());
	int32 Seat = 0, Kiosk = 0, Pickup = 0;
	for (const TWeakObjectPtr<ASlotPoint>& W : Slots)
	{
		const ASlotPoint* P = W.Get();
		if (!P) continue;
		if (P->Kind == BurgerHouseSlot::Seat) ++Seat;
		else if (P->Kind == BurgerHouseSlot::Kiosk) ++Kiosk;
		else if (P->Kind == BurgerHouseSlot::Pickup) ++Pickup;
	}
	S += FString::Printf(TEXT(" (좌석 %d / 키오스크 %d / 픽업 %d)"), Seat, Kiosk, Pickup);
	for (const FOrder& O : Orders)
	{
		S += FString::Printf(TEXT("\n  #%d 상태 %d 남은조리 %.1f"),
			O.Number, (int32)O.State, O.CookRemaining);
	}
	return S;
}
