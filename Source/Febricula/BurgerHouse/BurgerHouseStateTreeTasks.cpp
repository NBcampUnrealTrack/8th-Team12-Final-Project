#include "BurgerHouseStateTreeTasks.h"

#include "CustomerCharacter.h"
#include "SlotPoint.h"
#include "StoreSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogBurgerHouseST, Log, All);

namespace
{
	/**
	 *	서브시스템을 잡는다.
	 *
	 *	⚠ **못 잡으면 조용히 성공하지 않는다** (§6.0 ②-c — "예외를 삼키면 진단이
	 *	  거꾸로 나온다"). 널이면 호출부가 전부 `Failed` 를 돌려주고 경고를 찍는다.
	 */
	UStoreSubsystem* GetStore(const FStateTreeExecutionContext& Context)
	{
		const UWorld* World = Context.GetWorld();
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UStoreSubsystem>() : nullptr;
	}
}

////////////////////////////////////////////////////////////////////
// 슬롯 예약

EStateTreeRunStatus FBurgerHouseClaimSlotTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store || !Data.Customer)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Claim Slot — 서브시스템(%s) 또는 손님(%s) 없음"),
			Store ? TEXT("O") : TEXT("X"), *GetNameSafe(Data.Customer));
		return EStateTreeRunStatus::Failed;
	}

	ASlotPoint* Slot = Store->ClaimSlot(Data.Customer, Data.Kind, Data.bPickRandom);
	if (!Slot)
	{
		// 빈 자리가 없는 것은 **정상적인 실패**다. 트리가 기다리거나 나가면 된다.
		Data.Slot = nullptr;
		return EStateTreeRunStatus::Failed;
	}

	Data.Slot = Slot;

	// ⚠ 슬롯 원점이 아니라 **사용 지점**을 낸다 (§9.2 "좌석 지점 ≠ 의자 원점").
	//    원점을 그대로 MoveTo 에 주면 의자 한복판으로 걸어 들어간다.
	const FTransform Use = Slot->GetUseTransform();
	Data.UseLocation = Use.GetLocation();
	Data.UseRotation = Use.GetRotation().Rotator();

	// 프리필 전용 (헤더 주석). 평소 손님에게 켜면 문에서 자리로 순간이동한다.
	if (Data.bTeleportOnEnter)
	{
		Data.Customer->TeleportToPoint(Data.UseLocation, Data.UseRotation);
	}

	return EStateTreeRunStatus::Running;
}

void FBurgerHouseClaimSlotTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// ⚠ **1번 버그의 첫 번째 방어선** (§9.5). 성공했든 실패했든 여기서 놓는다.
	if (UStoreSubsystem* Store = GetStore(Context))
	{
		Store->ReleaseSlot(Data.Slot, Data.Customer);
	}
	Data.Slot = nullptr;
}

#if WITH_EDITOR
FText FBurgerHouseClaimSlotTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Claim Slot</b> %s"),
		Data ? *Data->Kind.ToString() : TEXT("?")));
}
#endif

////////////////////////////////////////////////////////////////////
// 주문 발행

EStateTreeRunStatus FBurgerHousePlaceOrderTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store || !Data.Customer)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Place Order — 서브시스템 또는 손님 없음"));
		return EStateTreeRunStatus::Failed;
	}

	Data.OrderNumber = Store->PlaceOrder(Data.Customer);
	if (Data.OrderNumber == 0)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 캐시. **판정 근거가 아니다** — 서브시스템이 진실이다 (§9.1).
	if (ACustomerCharacter* C = Data.Customer)
	{
		C->OrderNumber = Data.OrderNumber;
	}

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHousePlaceOrderTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Place Order</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 주문 수령

EStateTreeRunStatus FBurgerHouseClaimOrderTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store || !Data.Customer)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Claim Order — 서브시스템 또는 손님 없음"));
		return EStateTreeRunStatus::Failed;
	}

	int32 Number = Data.OrderNumber;
	if (Number == 0)
	{
		if (const ACustomerCharacter* C = Data.Customer)
		{
			Number = C->OrderNumber;
		}
	}
	if (Number == 0)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Claim Order — %s 는 기다리는 번호가 없다"),
			*GetNameSafe(Data.Customer));
		return EStateTreeRunStatus::Failed;
	}

	// ⚠ 중복 수령 차단은 **서브시스템이** 한다. 여기서 상태를 미리 보고
	//    판단하면 둘이 동시에 손을 뻗었을 때 둘 다 통과한다.
	if (!Store->ClaimOrder(Number, Data.Customer))
	{
		return EStateTreeRunStatus::Failed;
	}

	if (ACustomerCharacter* C = Data.Customer)
	{
		C->OrderNumber = 0;
	}
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHouseClaimOrderTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Claim Order</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 대기열

EStateTreeRunStatus FBurgerHouseWaitAreaTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store || !Data.Customer)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Enter Wait Area — 서브시스템 또는 손님 없음"));
		return EStateTreeRunStatus::Failed;
	}

	Data.QueueIndex = Store->EnterWaitArea(Data.Customer);
	if (Data.QueueIndex == INDEX_NONE)
	{
		return EStateTreeRunStatus::Failed;
	}

	Data.WaitLocation = Data.QueueStart + Data.QueueStep * static_cast<float>(Data.QueueIndex);

	// 프리필 전용 (헤더 주석). 줄 방향을 그대로 바라보게 세운다.
	if (Data.bTeleportOnEnter)
	{
		// ⚠ 줄이 뻗는 **반대쪽**을 본다 — `QueueStep` 은 뒤로 물러나는 방향이라
		//    그대로 쓰면 손님 전원이 카운터에 등을 돌린다.
		const FRotator Facing = (-Data.QueueStep).IsNearlyZero()
			? Data.Customer->GetActorRotation()
			: (-Data.QueueStep).Rotation();
		Data.Customer->TeleportToPoint(Data.WaitLocation, Facing);
	}

	// 대기는 스스로 끝나지 않는다. **`Order.Ready` 이벤트가 빼낸다** (§9.5 ★).
	return EStateTreeRunStatus::Running;
}

void FBurgerHouseWaitAreaTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// ⚠ **이 한 줄이 「앞사람이 빠지면 한 칸 당겨라」의 방아쇠다.**
	//    `LeaveWaitArea` 안에서 뒷사람 전원이 재색인된다.
	if (UStoreSubsystem* Store = GetStore(Context))
	{
		Store->LeaveWaitArea(Data.Customer);
	}
}

#if WITH_EDITOR
FText FBurgerHouseWaitAreaTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Enter Wait Area</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 대기열 — 지금 내 자리

EStateTreeRunStatus FBurgerHouseQueueSpotTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store || !Data.Customer)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Get Queue Spot — 서브시스템(%s) 또는 손님(%s) 없음"),
			Store ? TEXT("O") : TEXT("X"), *GetNameSafe(Data.Customer));
		return EStateTreeRunStatus::Failed;
	}

	const int32 Index = Store->GetWaitSlot(Data.Customer);
	if (Index == INDEX_NONE)
	{
		// ⚠ **조용히 넘어가지 않는다.** 줄에 없는데 자리를 묻는 것은 배선 실수다
		//    (같은 상태에 `Enter Wait Area` 가 없거나, 이 태스크가 그것보다
		//    **위에** 있다). 0 을 돌려주면 전원이 줄 맨 앞으로 겹쳐 서고,
		//    그 증상은 「대기열 좌표가 틀렸다」로 보여 원인을 못 찾는다.
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Get Queue Spot — %s 는 대기열에 없다. 같은 상태에 `Enter Wait Area` 를 **이 태스크보다 위에** 둘 것"),
			*GetNameSafe(Data.Customer));
		return EStateTreeRunStatus::Failed;
	}

	Data.QueueIndex = Index;
	Data.WaitLocation = Data.QueueStart + Data.QueueStep * static_cast<float>(Index);

	// 즉시 끝난다. ⚠ 그래서 같은 상태의 `Tasks Completion` 이 `All` 이어야 한다
	//    (헤더 주석) — `Any` 면 이 성공이 `Wait` 를 통째로 끝낸다.
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHouseQueueSpotTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Get Queue Spot</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 행동

EStateTreeRunStatus FBurgerHouseActionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Play Action %s — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"),
			*Data.ActionName.ToString());
		return EStateTreeRunStatus::Failed;
	}

	// 몽타주가 있으면 그 길이, 없으면 기본 시간. **호출부는 구별하지 않는다.**
	Data.Remaining = C->PlayAction(Data.ActionName, Data.FallbackSeconds)
		+ RandomExtra(Data);

	// ⚠ **횟수는 여기서 한 번만 뽑는다.** Tick 마다 다시 뽑으면 확률적으로
	//    끝나지 않는 상태가 생긴다.
	Data.RepeatsLeft = (Data.RandomExtraRepeats > 0)
		? FMath::RandRange(0, Data.RandomExtraRepeats)
		: 0;

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBurgerHouseActionTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	Data.Remaining -= DeltaTime;
	if (Data.Remaining > 0.f)
	{
		return EStateTreeRunStatus::Running;
	}

	const bool bAgain = Data.bLoop || Data.RepeatsLeft > 0;
	if (bAgain)
	{
		ACustomerCharacter* C = Data.Customer;
		if (!C)
		{
			// 손님이 사라졌는데 루프를 계속 돌면 상태가 영원히 안 끝난다
			return EStateTreeRunStatus::Failed;
		}
		if (!Data.bLoop)
		{
			--Data.RepeatsLeft;
		}
		Data.Remaining = C->PlayAction(Data.ActionName, Data.FallbackSeconds)
			+ RandomExtra(Data);
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Succeeded;
}

float FBurgerHouseActionTask::RandomExtra(const FInstanceDataType& Data)
{
	return (Data.RandomExtraSeconds > 0.f)
		? FMath::FRandRange(0.f, Data.RandomExtraSeconds)
		: 0.f;
}

void FBurgerHouseActionTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// 상태가 튀었는데 몽타주가 계속 돌면 다음 행동과 겹친다
	if (ACustomerCharacter* C = Data.Customer)
	{
		C->StopAction();
	}
	Data.Remaining = 0.f;
}

#if WITH_EDITOR
FText FBurgerHouseActionTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Play Action</b> %s%s"),
		Data ? *Data->ActionName.ToString() : TEXT("?"),
		(Data && Data->bLoop) ? TEXT(" (loop)") : TEXT("")));
}
#endif

////////////////////////////////////////////////////////////////////
// 착석

EStateTreeRunStatus FBurgerHouseSitTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Sit At Seat — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"));
		return EStateTreeRunStatus::Failed;
	}

	C->SitAt(FTransform(Data.SeatRotation, Data.SeatLocation, FVector::OneVector));

	// 스스로 끝나지 않는다 — **부모 상태에 걸고 자식에서 먹는 시늉을 돌린다.**
	return EStateTreeRunStatus::Running;
}

void FBurgerHouseSitTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// ⚠ 여기를 빼먹으면 손님이 **앉은 채로 미끄러져 나간다.** `MOVE_None` 이
	//    풀리지 않기 때문이다.
	if (ACustomerCharacter* C = Data.Customer)
	{
		C->StandUp();
	}
}

#if WITH_EDITOR
FText FBurgerHouseSitTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Sit At Seat</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 트레이

EStateTreeRunStatus FBurgerHouseTrayTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Tray — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"));
		return EStateTreeRunStatus::Failed;
	}

	switch (Data.Op)
	{
	case EBurgerHouseTrayOp::Hold:
		C->HoldTray();
		break;
	case EBurgerHouseTrayOp::PlaceOnTable:
		C->PlaceTrayAt(FTransform(Data.TableRotation, Data.TableLocation, FVector::OneVector));
		break;
	case EBurgerHouseTrayOp::Drop:
		C->DropTray();
		break;
	}

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHouseTrayTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	const TCHAR* OpName = TEXT("?");
	if (Data)
	{
		switch (Data->Op)
		{
		case EBurgerHouseTrayOp::Hold:         OpName = TEXT("Hold"); break;
		case EBurgerHouseTrayOp::PlaceOnTable: OpName = TEXT("Place On Table"); break;
		case EBurgerHouseTrayOp::Drop:         OpName = TEXT("Drop"); break;
		}
	}
	return FText::FromString(FString::Printf(TEXT("<b>Tray</b> %s"), OpName));
}
#endif

////////////////////////////////////////////////////////////////////
// 매장 지점 조회

EStateTreeRunStatus FBurgerHouseGetPointTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	UStoreSubsystem* Store = GetStore(Context);
	if (!Store)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Get Store Point — 서브시스템 없음"));
		return EStateTreeRunStatus::Failed;
	}

	FTransform T;
	if (!Store->FindPoint(Data.Kind, T))
	{
		// 서브시스템이 이미 어떤 종류를 못 찾았는지 찍었다.
		return EStateTreeRunStatus::Failed;
	}

	Data.Location = T.GetLocation();
	Data.Rotation = T.GetRotation().Rotator();
	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHouseGetPointTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Get Store Point</b> %s"),
		Data ? *Data->Kind.ToString() : TEXT("?")));
}
#endif

////////////////////////////////////////////////////////////////////
// 소멸

EStateTreeRunStatus FBurgerHouseDespawnTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Despawn Customer — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"));
		return EStateTreeRunStatus::Failed;
	}

	// 지우기 전에 먼저 놓는다. `EndPlay` 도 같은 일을 하지만 순서를 여기서
	// 확정해 두면 「지워졌는데 자리가 잠깐 잠긴」 구간이 안 생긴다.
	if (UStoreSubsystem* Store = GetStore(Context))
	{
		Store->ReleaseAllSlotsOf(C);
		Store->LeaveWaitArea(C);
	}

	// ⚠ **`Destroy()` 를 바로 부르지 않는다** (헤더 주석). 지금 이 코드는 이
	//    손님의 컨트롤러가 돌리는 트리 안에서 실행 중이다.
	// ⚠ `SetLifeSpan(0)` 은 「즉시」가 아니라 「수명 없음」이다. 올려 준다.
	C->SetLifeSpan(FMath::Max(Data.Delay, 0.05f));

	return EStateTreeRunStatus::Succeeded;
}

#if WITH_EDITOR
FText FBurgerHouseDespawnTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Despawn Customer</b>"));
}
#endif

////////////////////////////////////////////////////////////////////
// 시작 역할

EStateTreeRunStatus FBurgerHouseConsumeStartRoleTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Consume Start Role — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"));
		return EStateTreeRunStatus::Failed;
	}

	Data.ConsumedRole = C->ConsumeStartRole();

	// ⚠ **Verbose 가 아니라 Log 다** (`ACustomerCharacter::OnAssignedSpawnRole` 주석).
	//    「역할은 붙었는데 이 줄이 안 나온다」가 곧 「트리가 프리필 갈래를 안
	//    골랐다」이고, 그것이 이 기능의 유일한 조용한 실패 경로다.
	UE_LOG(LogBurgerHouseST, Log, TEXT("%s 시작 역할 %s 소비 — 프리필 갈래 진입"),
		*C->GetName(), *Data.ConsumedRole.ToString());

	if (Data.ConsumedRole.IsNone())
	{
		// 역할이 없는 손님이 프리필 갈래에 들어왔다 = 조건이 헐겁다.
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Consume Start Role — %s 는 역할이 없는데 프리필 갈래에 들어왔다. 그 상태의 `Start Role Is` 조건을 볼 것"),
			*C->GetName());
	}

	// ⚠ **끝나지 않는다** (헤더 주석). `TasksCompletion` 이 `Any` 라 즉시 성공하면
	//    부모 상태가 그 프레임에 통째로 끝난다.
	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FBurgerHouseConsumeStartRoleTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>Consume Start Role</b>"));
}
#endif

bool FBurgerHouseStartRoleCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	// ⚠ 손님이 없으면 **거짓**이다. 참을 주면 프리필 갈래로 잘못 들어간다.
	const bool bMatch = Data.Customer && Data.Customer->StartRole == Data.Role;
	return Data.bInvert ? !bMatch : bMatch;
}

#if WITH_EDITOR
FText FBurgerHouseStartRoleCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	return FText::FromString(FString::Printf(TEXT("<b>Start Role</b> %s <b>%s</b>"),
		(Data && Data->bInvert) ? TEXT("is not") : TEXT("is"),
		Data ? *Data->Role.ToString() : TEXT("?")));
}
#endif

////////////////////////////////////////////////////////////////////
// 조건

bool FBurgerHouseOrderReadyCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);

	const UStoreSubsystem* Store = GetStore(Context);
	const ACustomerCharacter* C = Data.Customer;
	if (!Store || !C || C->OrderNumber == 0)
	{
		return false;
	}
	return Store->GetOrderState(C->OrderNumber) == EOrderState::Ready;
}

#if WITH_EDITOR
FText FBurgerHouseOrderReadyCondition::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	return FText::FromString(TEXT("<b>My Order Is Ready</b>"));
}
#endif
