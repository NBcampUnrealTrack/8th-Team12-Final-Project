#include "BurgerHouseStateTreeTasks.h"

#include "CustomerCharacter.h"
#include "SlotPoint.h"
#include "StoreSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"
// ⚠ `EPathFollowingRequestResult` 의 **정의**는 여기 있다.
//    `AIController.h` 는 전방 선언만 해서 멤버를 못 쓴다.
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeExecutionTypes.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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

namespace
{
	/**
	 *	**멈추고** 그 yaw 로 돌려 놓는다.
	 *
	 *	## ⚠⚠ 먼저 멈춰야 회전이 남는다
	 *
	 *	`K_Move` 가 끝나고 `K_Act` 가 들어오는 순간 손님은 아직 **감속 중**이다.
	 *	그 상태에서 회전만 넣으면 `bOrientRotationToMovement = true` 가 다음 몇
	 *	프레임에 걸쳐 캐릭터를 **속도 방향으로 되돌린다.**
	 *
	 *	⚠ `StopMovementImmediately` 만으로는 부족하다. 그건 **속도**만 0 으로
	 *	  만들고 `Acceleration` 은 남기는데, orient-to-movement 가 보는 것이 바로
	 *	  그 가속도다. `StopActiveMovement` 가 가속도와 요청 속도를 지운다.
	 *
	 *	⚠ 경로 추종도 끊는다. 안 끊으면 다음 프레임에 또 가속을 넣는다.
	 */
	void FaceYaw(ACustomerCharacter* C, float Yaw)
	{
		if (AAIController* AI = Cast<AAIController>(C->GetController()))
		{
			AI->StopMovement();
		}
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			Move->StopMovementImmediately();	// 속도 0
			Move->StopActiveMovement();			// 가속도 · 요청 속도 0
		}

		const FRotator R(0.f, Yaw, 0.f);
		C->SetActorRotation(R);

		// ⚠ 컨트롤 회전도 같이 맞춘다 — 안 맞추면 다음 이동 첫 프레임에 홱 돈다
		//   (`ACustomerCharacter::TeleportToPoint` 가 같은 이유로 그렇게 한다).
		if (AController* Ctrl = C->GetController())
		{
			Ctrl->SetControlRotation(R);
		}
	}

	/**
	 *	손님이 **이미 잡고 있는** 그 종류의 슬롯 쪽으로 돌려 놓는다.
	 *
	 *	슬롯 종류가 비어 있으면 `bFaceWorldYaw` 를 본다 — 대기열처럼 **슬롯이
	 *	없는 자리**를 위한 길이다.
	 *
	 *	## ⚠⚠ 이것을 왜 여기서 하나
	 *
	 *	`bOrientRotationToMovement = true` 라 손님은 이동이 끝나면 **마지막 진행
	 *	방향 그대로** 선다. `ClaimSlot` 이 `UseRotation` 을 내놓지만 트리가 쓰지
	 *	않아서, 키오스크에 도착한 손님이 키오스크를 등지는 일이 있었다 (§9).
	 *
	 *	⚠ 속도가 0 일 때만 붙는다 — `Play Action` 은 `Move To` 뒤에 오므로 안전하다.
	 */
	void FaceClaimedSlot(ACustomerCharacter* C, const FBurgerHouseActionInstanceData& Data)
	{
		if (!C)
		{
			return;
		}

		if (Data.FaceSlotKind.IsNone())
		{
			// 슬롯이 없는 자리 — 절대 각도로 돌린다
			if (Data.bFaceWorldYaw)
			{
				FaceYaw(C, Data.FaceWorldYaw);
				UE_LOG(LogBurgerHouseST, Log,
					TEXT("Play Action %s — %s 를 월드 yaw %.1f 로 돌렸다"),
					*Data.ActionName.ToString(), *C->GetName(), Data.FaceWorldYaw);
			}
			return;
		}

		const UWorld* World = C->GetWorld();
		if (!World)
		{
			return;
		}

		// ⚠ 서브시스템에 「이 액터가 잡은 슬롯」 조회가 없다. 슬롯은 많아야 수십
		//    개고 이 코드는 상태 진입에 한 번만 도므로 훑어도 무해하다.
		const ASlotPoint* Mine = nullptr;
		for (TActorIterator<ASlotPoint> It(const_cast<UWorld*>(World)); It; ++It)
		{
			ASlotPoint* S = *It;
			if (S && S->Kind == Data.FaceSlotKind && S->ClaimedBy.Get() == C)
			{
				Mine = S;
				break;
			}
		}

		if (!Mine)
		{
			// ⚠ 경고만 찍고 **그냥 재생한다.** 방향 때문에 손님 루프가 깨지면 안 된다.
			UE_LOG(LogBurgerHouseST, Warning,
				TEXT("Play Action %s — 손님이 잡고 있는 '%s' 슬롯이 없다. 회전은 건너뛴다"),
				*Data.ActionName.ToString(), *Data.FaceSlotKind.ToString());
			return;
		}

		// ⚠ **양수 yaw 가 시계 방향**이다 (UE 는 X 정면 · Y 오른쪽).
		const float Yaw = Mine->GetUseTransform().Rotator().Yaw + Data.FaceYawOffset;
		FaceYaw(C, Yaw);

		// ⚠ **성공도 찍는다.** 「회전이 안 먹는다」를 진단할 때, 이 줄이 안 보이면
		//    여기까지 안 온 것이고 보이는데도 화면이 그대로면 **뒤에서 누가
		//    되돌리고 있는 것**이다 (루트모션·orient-to-movement). 둘은 고치는
		//    곳이 완전히 다르므로 이 한 줄이 갈림길이 된다.
		UE_LOG(LogBurgerHouseST, Log,
			TEXT("Play Action %s — %s 를 '%s' 기준 yaw %.1f (+%.1f) 로 돌렸다"),
			*Data.ActionName.ToString(), *C->GetName(), *Mine->GetName(),
			Yaw, Data.FaceYawOffset);
	}
}

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

	// ⚠ **재생보다 먼저 돌린다.** 몽타주가 시작된 뒤에 돌리면 첫 프레임이 옛
	//    방향으로 한 번 그려진다.
	FaceClaimedSlot(C, Data);

	// 몽타주가 있으면 그 길이, 없으면 기본 시간. **호출부는 구별하지 않는다.**
	Data.Remaining = C->PlayAction(Data.ActionName, Data.FallbackSeconds, -1, Data.PlayRate)
		+ RandomExtra(Data);

	// ⚠ **횟수는 여기서 한 번만 뽑는다.** Tick 마다 다시 뽑으면 확률적으로
	//    끝나지 않는 상태가 생긴다.
	//
	// ⚠ `Plays` 는 **총 재생 횟수**라 남은 반복은 그보다 하나 적다.
	Data.RepeatsLeft = FMath::Max(Data.Plays - 1, 0)
		+ ((Data.RandomExtraRepeats > 0) ? FMath::RandRange(0, Data.RandomExtraRepeats) : 0);

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
		Data.Remaining = C->PlayAction(Data.ActionName, Data.FallbackSeconds, -1, Data.PlayRate)
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

namespace
{
	/** 좌석까지 남은 수평 거리 */
	float SeatGap(const ACustomerCharacter* C, const FVector& SeatLocation)
	{
		return C ? FVector::Dist2D(C->GetActorLocation(), SeatLocation) : 0.f;
	}

	/** 아직 경로를 따라가는 중인가 */
	bool AI_IsMoving(const ACustomerCharacter* C)
	{
		const AAIController* AI = C ? Cast<AAIController>(C->GetController()) : nullptr;
		const UPathFollowingComponent* PF = AI ? AI->GetPathFollowingComponent() : nullptr;
		return PF && PF->GetStatus() != EPathFollowingStatus::Idle;
	}
}

EStateTreeRunStatus FBurgerHouseSitTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		UE_LOG(LogBurgerHouseST, Warning, TEXT("Sit At Seat — Customer 바인딩이 비었다. 스키마의 Context Actor Class 를 볼 것"));
		return EStateTreeRunStatus::Failed;
	}

	Data.bApproaching = false;
	Data.Elapsed = 0.f;

	// ## ⚠⚠ 여기서 **직접 걸어간다** — `S_Move` 를 못 믿는다
	//
	// `Root/Seat/S_Move` 의 `Move To` 는 진입한 프레임에 **그대로 `Failed`** 로
	// 끝난다. 목적지·`AIController` 바인딩·`TargetActor`·`bRequireNavigableEnd
	// Location` 을 하나씩 바꿔 가며 PIE 로 재 봤지만 전부 그대로였다. 같은 트리의
	// `K_Move`·`P_Move`·`W_Move` 는 멀쩡히 도는데 이것만 그렇다.
	//
	// 그런데 `S_Move → S_Eat` 전이가 성공/실패를 안 가려서 **실패해도 그냥 앉는다.**
	// 그래서 손님이 카운터에서 좌석까지 5~6m 를 날아갔다 — 「자리로 순간이동한다」.
	//
	// ⚠ 이 태스크의 `SeatLocation` 은 **제대로 묶여 있다** (앉는 자리가 정확했다).
	//   그래서 그 값으로 여기서 이동을 건다. 상태 하나가 죽어 있어도 손님은 걷는다.
	//
	// ⚠ 근본 수정은 `S_Move` 의 바인딩을 에디터에서 보는 것이다. 파이썬 API 로는
	//   바인딩을 **읽을 수가 없어서** 여기까지가 한계였다.
	const float Gap = SeatGap(C, Data.SeatLocation);
	if (Gap > Data.ApproachRadius)
	{
		if (AAIController* AI = Cast<AAIController>(C->GetController()))
		{
			// ## ⚠⚠ 좌석 지점 **그대로는 못 간다** — 의자 위이고 내비 구멍이다
			//
			// 좌석의 사용 지점은 `ASlotPoint::LocalOffset` 이 좌면 높이만큼 올려
			// 놓은 자리다 (§9.2). 의자는 내비메시 구멍이라 그 좌표는 **내비 밖**
			// 이고, `MoveToLocation` 은 `bProjectDestinationToNavigation` 을 켜도
			// **`Failed`(0)** 를 돌려준다 — 실측으로 확인했다.
			//
			// ⚠ 엔진 기본 투영 범위가 좁아서 그렇다. `TeleportToPoint` 가 쓰는
			//   것과 같은 **넉넉한 범위**로 우리가 직접 투영한다.
			//
			// ⚠ 투영된 자리와 진짜 좌석 사이의 거리는 **없앨 수 없다** — 손님이
			//   의자 위에 설 수는 없기 때문이다. 그 몇십 cm 는
			//   `ACustomerCharacter::SeatBlendTime` 이 미끄러져 메운다.
			FVector Goal = Data.SeatLocation;
			if (const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(C->GetWorld()))
			{
				FNavLocation Projected;
				if (Nav->ProjectPointToNavigation(Goal, Projected, FVector(150.f, 150.f, 300.f)))
				{
					UE_LOG(LogBurgerHouseST, Log,
						TEXT("Sit At Seat — 좌석(%s) 을 내비로 %.0fcm 옮겨 목적지로 삼는다"),
						*Goal.ToCompactString(), FVector::Dist(Goal, Projected.Location));
					Goal = Projected.Location;
				}
				else
				{
					UE_LOG(LogBurgerHouseST, Warning,
						TEXT("Sit At Seat — 좌석(%s) 근처에 내비메시가 없다. 그냥 앉는다"),
						*Goal.ToCompactString());
				}
			}

			// ⚠ 앉았다 일어난 직후면 이동 모드가 아직 `MOVE_None` 일 수 있다.
			//    그 상태로 이동을 걸면 명령은 나가는데 캡슐이 안 움직인다.
			if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
			{
				if (Move->MovementMode == MOVE_None)
				{
					Move->SetMovementMode(MOVE_Walking);
				}
			}

			// ⚠ 좌석 지점은 의자 위(내비 밖)다. **투영 + 부분 경로**를 둘 다 켜야
			//   「거기까지는 못 간다」로 실패하지 않고 닿는 데까지 걸어간다.
			const EPathFollowingRequestResult::Type R = AI->MoveToLocation(
				Goal,
				// ⚠ **`ApproachRadius` 를 쓰지 않는다.** 그것은 「이미 다 왔으니 걷지
				//   말자」를 가르는 값이고, 여기에 넣으면 그만큼 **더 멀리서 멈춘다.**
				//   실측: 투영 56cm + 반경 60cm = 116cm 를 미끄러졌다.
				/*AcceptanceRadius=*/10.f,
				/*bStopOnOverlap=*/false,
				/*bUsePathfinding=*/true,
				/*bProjectDestinationToNavigation=*/true,
				/*bCanStrafe=*/false,
				/*FilterClass=*/nullptr,
				/*bAllowPartialPath=*/true);

			if (R == EPathFollowingRequestResult::RequestSuccessful)
			{
				Data.bApproaching = true;
				UE_LOG(LogBurgerHouseST, Log, TEXT("Sit At Seat — %s 가 좌석까지 %.0fcm, 걸어간다"),
					*C->GetName(), Gap);
			}
			else
			{
				// ⚠ **소리 내서 찍는다.** 여기가 조용하면 다시 「순간이동」이 된다.
				UE_LOG(LogBurgerHouseST, Warning,
					TEXT("Sit At Seat — %s 가 좌석까지 %.0fcm 인데 이동 요청이 실패했다(%d). 그냥 앉는다"),
					*C->GetName(), Gap, static_cast<int32>(R));
			}
		}
		else
		{
			UE_LOG(LogBurgerHouseST, Warning,
				TEXT("Sit At Seat — %s 에 AIController 가 없다. 걸어갈 수 없어 그냥 앉는다"), *C->GetName());
		}
	}

	if (!Data.bApproaching)
	{
		C->SitAt(FTransform(Data.SeatRotation, Data.SeatLocation, FVector::OneVector));
	}

	// 스스로 끝나지 않는다 — **부모 상태에 걸고 자식에서 먹는 시늉을 돌린다.**
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FBurgerHouseSitTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	if (!Data.bApproaching)
	{
		return EStateTreeRunStatus::Running;
	}

	ACustomerCharacter* C = Data.Customer;
	if (!C)
	{
		Data.bApproaching = false;
		return EStateTreeRunStatus::Running;
	}

	Data.Elapsed += DeltaTime;

	// ⚠ **이동이 끝났는지**로 판정한다. 좌석까지의 거리로 보면, 투영된 자리와
	//    좌석 사이의 거리가 `ApproachRadius` 보다 크면 **영원히 도착 못 한다.**
	const float Gap = SeatGap(C, Data.SeatLocation);
	const bool bMoving = AI_IsMoving(C);
	const bool bArrived = (!bMoving || Gap <= Data.ApproachRadius);
	// ⚠ **시간 제한이 있어야 한다.** 경로가 막히면 손님이 영원히 서 있고, 그 자리는
	//   이미 예약돼 있어 다른 손님도 못 앉는다. 늦더라도 앉히는 편이 낫다.
	const bool bTimedOut = (Data.ApproachTimeout > 0.f && Data.Elapsed >= Data.ApproachTimeout);

	if (!bArrived && !bTimedOut)
	{
		return EStateTreeRunStatus::Running;
	}

	if (bTimedOut && !bArrived)
	{
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Sit At Seat — %s 가 %.1f초 안에 좌석에 못 갔다 (%.0fcm 남음). 그 자리에서 앉힌다"),
			*C->GetName(), Data.ApproachTimeout, Gap);
	}

	if (AAIController* AI = Cast<AAIController>(C->GetController()))
	{
		AI->StopMovement();
	}
	Data.bApproaching = false;
	C->SitAt(FTransform(Data.SeatRotation, Data.SeatLocation, FVector::OneVector));

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

	// ## ⚠⚠ **`Running`** 이다 — `Succeeded` 를 돌려주면 `Leave` 가 그 자리에서 끝난다
	//
	// `Root/Leave` 는 완료 정책이 `All` 이고 태스크가 둘이다 —
	// `Tray[Drop]`(즉시 성공) + 이것. 이게 성공을 돌려주면 **둘 다 완료**라
	// `Root/Leave` 가 진입한 프레임에 끝나고 `-> Root` 전이가 걸린다. `Root` 는
	// 첫 자식 `Enter` 를 고르므로 손님이 **처음부터 다시** 돈다.
	//
	// ⚠⚠ 실측: 좌석에서 일어난 손님이 `L_Move`(출구까지 걷기)와
	//   `L_Gone`(디스폰)을 **둘 다 건너뛰고** 같은 밀리초에 `Enter` 로 돌아갔다.
	//   그래서 워크인이 영원히 안 나가고 정원을 한 자리 계속 물고 있었다.
	//
	// ⚠ `Root/Wait` 의 `Enter Wait Area` 가 같은 이유로 `Running` 이다. 자리를
	//   내놓는 **데이터 제공** 태스크는 상태를 끝내지 않는다 — 상태를 끝내는 것은
	//   자식 상태와 전이다.
	return EStateTreeRunStatus::Running;
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

	// ## ⚠⚠ 출구까지 **직접 걸어간다** — `L_Move` 는 죽어 있다
	//
	// `Root/Leave/L_Move` 의 `Move To` 는 진입한 프레임에 그대로 끝난다 (`S_Move`
	// 와 같은 증상). 그래서 손님이 자리에서 그대로 증발했다.
	//
	// ⚠ 출구 지점은 서브시스템에 물으면 되므로 **바인딩이 필요 없다.**
	Data.bWalkingOut = false;
	Data.Elapsed = 0.f;

	if (Data.WalkOutTimeout <= 0.f)
	{
		// ⚠ **`Destroy()` 를 바로 부르지 않는다** (헤더 주석). 지금 이 코드는 이
		//    손님의 컨트롤러가 돌리는 트리 안에서 실행 중이다.
		// ⚠ `SetLifeSpan(0)` 은 「즉시」가 아니라 「수명 없음」이다. 올려 준다.
		C->SetLifeSpan(FMath::Max(Data.Delay, 0.05f));
	}

	// ## ⚠⚠ **`Running`** 이다 — `Succeeded` 를 돌려주면 루프가 한 바퀴 더 돈다
	//
	// 성공을 돌려주면 `Root/Leave` 가 완료되고 `-> Root` 전이가 걸린다. `Root` 는
	// 첫 자식 `Enter` 를 고르므로, 수명이 다하기 전 그 짧은 사이에 손님이 **키오스크
	// 슬롯을 다시 잡았다가** 사라진다. 실측으로 0.2초 동안 그랬다.
	return EStateTreeRunStatus::Running;
}

namespace
{
	/** 출구로 이동을 건다. 성공하면 true. */
	bool RequestWalkToExit(UStoreSubsystem* Store, ACustomerCharacter* C)
	{
		FTransform ExitT;
		if (!Store || !C || !Store->FindPoint(BurgerHouseSlot::Exit, ExitT))
		{
			return false;
		}
		AAIController* AI = Cast<AAIController>(C->GetController());
		if (!AI)
		{
			return false;
		}
		// 앉았다 일어난 직후면 이동 모드가 아직 `MOVE_None` 이다
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			if (Move->MovementMode == MOVE_None)
			{
				Move->SetMovementMode(MOVE_Walking);
			}
		}
		FVector Goal = ExitT.GetLocation();
		if (const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(C->GetWorld()))
		{
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Goal, Projected, FVector(150.f, 150.f, 300.f)))
			{
				Goal = Projected.Location;
			}
		}
		const EPathFollowingRequestResult::Type R = AI->MoveToLocation(
			Goal, /*AcceptanceRadius=*/20.f, /*bStopOnOverlap=*/false,
			/*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true,
			/*bCanStrafe=*/false, /*FilterClass=*/nullptr, /*bAllowPartialPath=*/true);
		return R == EPathFollowingRequestResult::RequestSuccessful;
	}
}

EStateTreeRunStatus FBurgerHouseDespawnTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	ACustomerCharacter* C = Data.Customer;
	if (!C || Data.WalkOutTimeout <= 0.f)
	{
		return EStateTreeRunStatus::Running;		// 수명이 끝나기를 기다린다
	}

	const float Was = Data.Elapsed;
	Data.Elapsed += DeltaTime;
	const bool bTimedOut = (Data.Elapsed >= Data.WalkOutTimeout);

	// ## ⚠⚠ 이동 요청은 **될 때까지 다시 건다**
	//
	// `S_Eat` 를 나오면서 `StandUp()` 이 좌석 보간을 시작하는데, 그동안 캐릭터가
	// 매 틱 `TeleportPhysics` 로 옮겨져 **경로 요청이 그 자리에서 실패한다**
	// (실측: `MoveToLocation` 이 `Failed`(0)). 보간은 0.5초면 끝나므로 다음 틱에
	// 다시 걸면 붙는다.
	//
	// ⚠ 한 번 실패했다고 포기하면 손님이 자리에서 증발한다 — 그 그림이 바로
	//   고치려던 것이다.
	if (!Data.bWalkingOut && !bTimedOut)
	{
		if (RequestWalkToExit(GetStore(Context), C))
		{
			Data.bWalkingOut = true;
			UE_LOG(LogBurgerHouseST, Log, TEXT("Despawn Customer — %s 가 출구로 걸어간다 (%.2f초 만에 붙음)"),
				*C->GetName(), Data.Elapsed);
		}
		else if (Was <= 0.f)
		{
			// 첫 실패만 찍는다 — 매 틱 찍으면 로그가 묻힌다
			UE_LOG(LogBurgerHouseST, Verbose,
				TEXT("Despawn Customer — %s 출구 이동 요청이 아직 안 붙는다. 다시 건다"), *C->GetName());
		}
		return EStateTreeRunStatus::Running;
	}

	const AAIController* AI = Cast<AAIController>(C->GetController());
	const UPathFollowingComponent* PF = AI ? AI->GetPathFollowingComponent() : nullptr;
	const bool bStopped = Data.bWalkingOut && (!PF || PF->GetStatus() == EPathFollowingStatus::Idle);

	if (bStopped || bTimedOut)
	{
		if (bTimedOut && !bStopped)
		{
			UE_LOG(LogBurgerHouseST, Warning,
				TEXT("Despawn Customer — %s 가 %.0f초 안에 출구에 못 갔다. 그 자리에서 사라진다"),
				*C->GetName(), Data.WalkOutTimeout);
		}
		else
		{
			UE_LOG(LogBurgerHouseST, Log, TEXT("Despawn Customer — %s 가 출구에 닿았다 (%.1f초)"),
				*C->GetName(), Data.Elapsed);
		}
		Data.bWalkingOut = false;
		Data.WalkOutTimeout = 0.f;			// 이 뒤로는 수명만 기다린다
		C->SetLifeSpan(FMath::Max(Data.Delay, 0.05f));
	}

	return EStateTreeRunStatus::Running;
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

////////////////////////////////////////////////////////////////////
// 레벨의 소리 액터 울리기

EStateTreeRunStatus FBurgerHousePlaySoundTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);

	// ## ⚠⚠ 성공도 실패도 **`Running`** 이다 — `Succeeded` 를 돌려주면 안 된다
	//
	// 이 태스크는 아무것도 기다리지 않으므로 「끝났다」고 말할 것이 없다. 그런데
	// `Succeeded` 를 돌려주면 **상태를 통째로 끝내 버린다** — `Root/Pickup` 처럼
	// 완료 정책이 `Any` 인 상태에 붙으면 같은 프레임에 그 상태가 완료되어
	// 자식(`P_Move`·`P_Act`·`P_Take`)이 **하나도 안 돈다.**
	//
	// ⚠⚠ 실제로 그렇게 됐다. 손님이 픽업대로 걸어가지도, 트레이를 받지도 않고
	//   카운터에서 좌석으로 **4.5m 를 순간이동**했다. 「자리로 순간이동한다」의
	//   진짜 원인이 이것이었고, 도착 판정·좌석 보간을 두 번 고치는 동안 안 보였다.
	//
	// ⚠ `Claim Slot` 도 같은 이유로 영원히 `Running` 이다. 연출·예약처럼 **스스로
	//   끝날 일이 없는 태스크는 끝났다고 말하지 않는다** — 상태를 끝내는 것은
	//   자식 상태와 전이지 이런 태스크가 아니다.
	//
	// ⚠ 실패 경로도 `Running` 이다. 연출 때문에 손님 루프가 깨지면 안 된다
	//   (헤더 주석). 대신 **왜 안 울렸는지는 반드시 찍는다.**
	UWorld* World = Context.GetWorld();
	if (!World || Data.ActorTag.IsNone())
	{
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Play Sound — 월드(%s) 또는 ActorTag 가 없다"),
			World ? TEXT("있음") : TEXT("없음"));
		return EStateTreeRunStatus::Running;
	}

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(World, Data.ActorTag, Found);
	if (Found.Num() == 0)
	{
		// ⚠ 컴포넌트 태그에 넣고 액터 태그로 착각하는 실수가 잦다.
		UE_LOG(LogBurgerHouseST, Warning,
			TEXT("Play Sound — 태그 '%s' 를 가진 액터가 레벨에 없다. "
				 "액터의 Tags 인지(컴포넌트 Tags 가 아니라) 확인할 것"),
			*Data.ActorTag.ToString());
		return EStateTreeRunStatus::Running;
	}

	// ⚠ 여럿이면 전부 울린다 — 「하나만 울리겠지」로 두면 조용히 절반만 난다.
	if (Found.Num() > 1)
	{
		UE_LOG(LogBurgerHouseST, Log,
			TEXT("Play Sound — 태그 '%s' 액터가 %d개다. 전부 울린다"),
			*Data.ActorTag.ToString(), Found.Num());
	}

	for (AActor* A : Found)
	{
		if (!A)
		{
			continue;
		}

		if (Data.Sound)
		{
			// 소리를 직접 지정했으면 그 자리에서 한 번 낸다.
			UGameplayStatics::PlaySoundAtLocation(World, Data.Sound, A->GetActorLocation(),
				Data.VolumeMultiplier);
			continue;
		}

		// 안 지정했으면 액터가 들고 있는 것을 튼다 (`AAmbientSound` 의 경우).
		//
		// ⚠ `AAmbientSound` 는 `AudioComponent` 를 들고 있고 기본이
		//   `Auto Activate = true` 다. 그대로 두면 레벨이 열리자마자 울기
		//   시작하므로 **꺼 두어야** 이 태스크가 트리거가 된다.
		UAudioComponent* AC = A->FindComponentByClass<UAudioComponent>();
		if (!AC)
		{
			UE_LOG(LogBurgerHouseST, Warning,
				TEXT("Play Sound — %s 에 AudioComponent 가 없고 Sound 도 안 지정됐다"),
				*A->GetName());
			continue;
		}
		if (!AC->Sound)
		{
			// 실측으로 한 번 걸렸다 — 레벨의 AmbientSound 에 Sound 가 비어 있었다.
			UE_LOG(LogBurgerHouseST, Warning,
				TEXT("Play Sound — %s 의 AudioComponent 에 **Sound 가 비어 있다**. "
					 "디테일 패널에서 붙일 것"), *A->GetName());
			continue;
		}
		AC->SetVolumeMultiplier(Data.VolumeMultiplier);
		// ⚠ `Activate` 가 아니라 `Play` 다. 이미 한 번 울린 뒤에도 처음부터 다시 난다.
		AC->Play();
	}

	return EStateTreeRunStatus::Running;
}

#if WITH_EDITOR
FText FBurgerHousePlaySoundTask::GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting) const
{
	const FInstanceDataType* Data = InstanceDataView.GetPtr<FInstanceDataType>();
	const FString Tag = Data ? Data->ActorTag.ToString() : TEXT("?");
	return FText::FromString(FString::Printf(TEXT("<b>Play Sound</b> at '%s'"), *Tag));
}
#endif
