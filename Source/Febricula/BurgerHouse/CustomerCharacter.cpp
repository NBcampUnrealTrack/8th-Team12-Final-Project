#include "CustomerCharacter.h"

#include "StoreSubsystem.h"
#include "SlotPoint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogBurgerHouseNPC, Log, All);

ACustomerCharacter::ACustomerCharacter()
{
	// 손님은 스스로 틱할 게 없다. 시간 세기는 StateTree 태스크가 한다.
	PrimaryActorTick.bCanEverTick = false;

	Tray = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tray"));
	if (Tray)
	{
		Tray->SetupAttachment(GetMesh(), TraySocket);
		// 트레이는 장식이다. 콜리전을 주면 손님이 자기 트레이에 걸린다.
		Tray->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tray->SetGenerateOverlapEvents(false);
		// ⚠ 납작한 것의 그림자는 VSM 마킹을 먹는다 (UE 함정 19).
		Tray->SetCastShadow(false);
		Tray->SetHiddenInGame(true);
	}
}

void ACustomerCharacter::OnSpawnedInGroup(int32 GroupId, int32 GroupSize, int32 IndexInGroup)
{
	// ⚠ **혼자 와도 불린다** (`GroupSize == 1`). 그래도 일행으로 등록한다 —
	//    「혼자」와 「일행」의 분기를 여기서 만들지 않으면 아래 좌석 배정 코드가
	//    한 갈래로 끝난다. 1명짜리 일행은 자연스럽게 아무 테이블이나 잡는다.
	PartyId = GroupId;
	PartySize = FMath::Max(GroupSize, 1);

	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UStoreSubsystem* Store = GI->GetSubsystem<UStoreSubsystem>())
		{
			Store->JoinParty(PartyId, PartySize);
		}
	}

	// ⚠ **여기서 부른다 — `OnAssignedSpawnRole` 이 아니다.** 좌석 배정이
	//    `PartyId` 를 보기 때문이다 (`ClaimSlot` → `ResolvePartyTable`). 역할
	//    통보가 먼저 오므로 그쪽에서 부르면 `PartyId` 가 아직 0 이고, 일행이
	//    **같은 테이블에 안 앉는다.** 증상은 「일행이 흩어진다」로만 보인다.
	ApplyStartRole();
}

bool ACustomerCharacter::ApplyStartRole()
{
	if (StartRole.IsNone())
	{
		return false;		// 문으로 들어온 손님 — 할 일 없다
	}

	const UGameInstance* GI = GetGameInstance();
	UStoreSubsystem* Store = GI ? GI->GetSubsystem<UStoreSubsystem>() : nullptr;
	if (!Store)
	{
		UE_LOG(LogBurgerHouseNPC, Warning, TEXT("%s 역할 %s — 서브시스템 없음"),
			*GetName(), *StartRole.ToString());
		return false;
	}

	// ---------------------------------------------------------- 앉아서 먹는 중
	if (StartRole == TEXT("Eating"))
	{
		// ⚠ **서 있는 그 의자**를 잡는다. 무작위·히트맵으로 잡으면 A 의자에
		//    앉아 있는데 예약은 B 에 걸려, 나중에 다른 손님이 A 로 걸어와 겹친다.
		//    목록에 안 적힌(스플라인에서 태어난) 프리필이면 근처에 의자가 없어
		//    실패하고, 그때는 아래에서 역할을 비운다.
		ASlotPoint* Seat = Store->ClaimNearestSlot(this, BurgerHouseSlot::Seat);
		if (!Seat)
		{
			// ⚠ 자리가 없으면 **역할을 지운다.** 안 지우면 트리가 프리필 갈래로
			//    들어가는데 앉을 자리가 없어 그 자리에서 먹는 시늉만 한다.
			//    역할을 비우면 기본 갈래로 떨어져 평범한 손님이 된다.
			UE_LOG(LogBurgerHouseNPC, Warning,
				TEXT("%s 역할 Eating — 근처에 빈 좌석이 없다. 역할을 비우고 평범한 손님으로 시작한다. ")
				TEXT("스포너의 PrefillPlacements 가 좌석 위를 가리키는지 볼 것"),
				*GetName());
			StartRole = NAME_None;
			return false;
		}

		const FTransform Use = Seat->GetUseTransform();
		SitAt(Use);

		// 프리필 손님은 `Pickup` 을 건너뛰므로 손에 든 적이 없다. 들려 준 뒤
		// 테이블에 놓아야 트레이가 보인다 (`PlaceTrayAt` 이 `bHasTray` 를 본다).
		HoldTray();
		PlaceTrayAt(Use);

		// 장식 손님은 트리를 안 타므로 여기서 몸짓을 걸어 준다.
		StartDecorationLoop();

		UE_LOG(LogBurgerHouseNPC, Log, TEXT("%s 프리필 착석 — %s (일행 %d)%s"),
			*GetName(), *Seat->GetName(), PartyId,
			bDecoration ? TEXT(" [장식]") : TEXT(""));
		return true;
	}

	// ---------------------------------------------------------- 키오스크 앞
	if (StartRole == TEXT("Ordering"))
	{
		// 키오스크는 **2대뿐**이다. 목록에 적힌 자리 위에 낳았다면 최근접이
		// 정확히 그 대를 잡는다.
		ASlotPoint* Kiosk = Store->ClaimNearestSlot(this, BurgerHouseSlot::Kiosk, /*MaxDistance=*/300.f);
		if (!Kiosk)
		{
			UE_LOG(LogBurgerHouseNPC, Warning,
				TEXT("%s 역할 Ordering — 쓸 수 있는 키오스크가 없다 (2대뿐이다). 역할을 비운다"),
				*GetName());
			StartRole = NAME_None;
			return false;
		}

		const FTransform Use = Kiosk->GetUseTransform();
		TeleportToPoint(Use.GetLocation(), Use.GetRotation().Rotator());
		StartDecorationLoop();

		UE_LOG(LogBurgerHouseNPC, Log, TEXT("%s 프리필 키오스크 — %s%s"),
			*GetName(), *Kiosk->GetName(), bDecoration ? TEXT(" [장식]") : TEXT(""));
		return true;
	}

	// ---------------------------------------------------------- 카운터 줄
	if (StartRole == TEXT("Waiting"))
	{
		// ⚠ **장식 손님은 주문을 넣지 않는다.** 트리를 안 타서 아무도 찾아가지
		//    않으므로, 넣으면 12초 뒤 `Ready` 가 되어 픽업대에 **영원히 남는다.**
		if (!bDecoration)
		{
			// 주문 번호가 없으면 `Order.Ready` 가 영영 안 와서 줄에 붙박이가 된다.
			OrderNumber = Store->PlaceOrder(this);
		}
		const int32 Index = Store->EnterWaitArea(this);
		StartDecorationLoop();

		// ⚠ **자리 좌표는 여기서 못 만든다.** 줄의 기하(`QueueStart`·`QueueStep`)는
		//    StateTree 태스크의 파라미터로 **레벨마다** 들어 있고, 서브시스템은
		//    줄의 **순서**만 안다. 그래서 위치는 트리가 잡는다 —
		//    `PW_Wait ▸ Enter Wait Area ▸ Teleport On Enter` 가 첫 틱에 세워 준다.
		//
		//    ⚠ 그 스위치를 안 켜면 이 손님만 문 앞에서 걸어온다.
		// ⚠ **장식 손님은 줄 자리로 옮겨 주는 코드가 없다.** 위 주석대로 자리를
		//    잡는 것은 트리의 `Enter Wait Area ▸ Teleport On Enter` 인데 트리를
		//    안 타기 때문이다. `PrefillPlacements` 에 줄 좌표를 직접 적어야 한다.
		UE_LOG(LogBurgerHouseNPC, Log,
			TEXT("%s 프리필 대기 — 주문 %d · 줄 %d번%s"),
			*GetName(), OrderNumber, Index,
			bDecoration ? TEXT(" [장식 — 자리는 PrefillPlacements 가 정한다]")
			            : TEXT(" (위치는 트리가 잡는다)"));

		// ⚠ 장식 손님은 주문을 안 넣으므로 `OrderNumber` 가 0 이다. 그걸 실패로
		//    치면 역할이 지워져 평범한 손님이 된다.
		return bDecoration || OrderNumber != 0;
	}

	// ⚠ 스포너의 `Prefill Roles` 에 이 이름이 있는데 여기 분기가 없다.
	//    조용히 넘어가면 「그 역할만 안 먹는다」가 되어 원인을 못 찾는다.
	UE_LOG(LogBurgerHouseNPC, Warning,
		TEXT("%s 역할 %s — 아는 역할이 아니다. Eating · Waiting · Ordering 중 하나여야 한다"),
		*GetName(), *StartRole.ToString());
	return false;
}

void ACustomerCharacter::OnAssignedSpawnRole(FName InRole)
{
	// ⚠ `Role` 로 쓰면 `AActor::Role`(네트 롤)을 가려 C4458 로 빌드가 깨진다.
	StartRole = InRole;

	// ⚠ **Verbose 가 아니라 Log 다.** 이 값이 안 찍히면 스포너 쪽 문제이고,
	//    찍히는데 `Consume Start Role` 이 안 찍히면 **트리 쪽 문제**다
	//    (조건 이름 오타이거나, 프리필 상태가 `Enter` 아래에 있어 선택이 안 됨).
	//    둘의 증상이 「프리필이 안 먹는다」로 똑같아서 갈라 주는 줄이 필요하다.
	UE_LOG(LogBurgerHouseNPC, Log, TEXT("%s 시작 역할 = %s"), *GetName(), *InRole.ToString());
}

FName ACustomerCharacter::ConsumeStartRole()
{
	const FName Was = StartRole;
	StartRole = NAME_None;
	return Was;
}

void ACustomerCharacter::TeleportToPoint(const FVector& Location, const FRotator& Rotation)
{
	// ⚠ 이동 중이면 먼저 끊는다. 안 끊으면 `MoveTo` 가 「밀렸다」고 보고 원래
	//    경로로 되돌아간다 (`SitAt` 과 같은 이유).
	if (AController* C = GetController())
	{
		C->StopMovement();
	}
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}

	FVector Target = Location;

	// ⚠ **내비에 올려놓는다.** 슬롯의 사용 지점은 손으로 놓은 좌표라 몇 cm
	//    어긋날 수 있는데, 걸어서 갈 때는 `MoveTo` 가 알아서 맞춰 주지만
	//    순간이동은 준 자리에 그대로 꽂는다. 내비 밖에 떨어지면 그 손님은
	//    이후 `MoveTo` 가 전부 실패해 **영영 안 움직인다.**
	if (const UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Projected;
		if (Nav->ProjectPointToNavigation(Target, Projected, FVector(150.f, 150.f, 300.f)))
		{
			Target = Projected.Location;
		}
		else
		{
			UE_LOG(LogBurgerHouseNPC, Warning,
				TEXT("%s 순간이동 — (%s) 가 내비메시 밖이다. 그대로 놓는다 (이후 이동이 실패할 수 있다)"),
				*GetName(), *Target.ToCompactString());
		}
	}

	// 캡슐 원점은 발밑이 아니라 가운데다 (`SitAt` 과 같은 규약).
	if (const UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Target.Z += Cap->GetScaledCapsuleHalfHeight();
	}

	const FRotator Yaw(0.f, Rotation.Yaw, 0.f);
	SetActorLocationAndRotation(Target, Yaw.Quaternion(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// 컨트롤 회전도 같이 돌린다 — 안 돌리면 다음 이동 첫 프레임에 홱 돈다.
	if (AController* C = GetController())
	{
		C->SetControlRotation(Yaw);
	}
}

void ACustomerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ⚠ 반복 타이머를 안 끄면 파괴된 손님의 타이머가 남아 계속 돈다.
	StopActionLoop();

	// ⚠ **릴리스 누락이 이 시스템의 1번 버그다** (§9.5). 태스크의 `ExitState` 가
	//    정석이지만 파괴 경로는 그걸 안 탄다. 좀비 청소(`Sweep`)가 최후의 보루로
	//    남아 있어도 최대 `SweepSeconds` 동안 자리가 죽는다 — 여기서 즉시 놓는다.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UStoreSubsystem* Store = GI->GetSubsystem<UStoreSubsystem>())
		{
			Store->ReleaseAllSlotsOf(this);
			Store->LeaveWaitArea(this);
			// ⚠ 마지막 한 명이 나가야 일행의 테이블 배정이 풀린다. 안 풀면
			//    `Parties` 가 손님이 들고 날 때마다 한 칸씩 자란다.
			Store->LeaveParty(PartyId);
		}
	}

	// ⚠ **AI 컨트롤러는 폰이 파괴돼도 혼자 남는다.** 손님이 들고 날 때마다
	//    빈 컨트롤러가 하나씩 쌓이고, 그것들이 계속 틱한다. 눈에 안 보여서
	//    한참 뒤에야 프로파일러로 발견하게 되는 종류의 누수다.
	//
	//    ⚠ 레벨 전환·에디터 종료(`EndPlayReason` 이 `Destroyed` 가 아닌 경우)에는
	//      건드리지 않는다. 그때는 엔진이 알아서 정리하고, 여기서 손대면
	//      종료 순서를 흔든다.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (AController* C = GetController())
		{
			C->Destroy();
		}
	}

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------- 행동

int32 ACustomerCharacter::PickVariant(FName ActionName, const FCustomerAction& Action)
{
	const int32 Num = Action.Variants.Num();
	if (Num == 0)
	{
		return INDEX_NONE;
	}
	if (Num == 1)
	{
		// ⚠ 하나뿐이면 「연속 회피」가 성립하지 않는다. 그냥 준다.
		return Action.Variants[0].Montage ? 0 : INDEX_NONE;
	}

	const int32 Last = bAvoidRepeatVariant
		? LastVariant.FindRef(ActionName)		// 없으면 0 — 첫 회엔 0번을 살짝 피한다
		: INDEX_NONE;

	// 가중치 합. 몽타주가 빈 칸과 직전 것은 후보에서 뺀다.
	float Total = 0.f;
	for (int32 i = 0; i < Num; ++i)
	{
		if (!Action.Variants[i].Montage || i == Last)
		{
			continue;
		}
		Total += FMath::Max(Action.Variants[i].Weight, 0.f);
	}

	// ⚠ 직전 것을 뺐더니 후보가 없어졌다 (나머지 가중치가 전부 0이거나 빈 칸).
	//    그러면 **회피를 포기하고** 다시 센다 — 아무것도 재생 안 하는 것보다 낫다.
	bool bRelaxed = false;
	if (Total <= KINDA_SMALL_NUMBER)
	{
		bRelaxed = true;
		for (int32 i = 0; i < Num; ++i)
		{
			if (Action.Variants[i].Montage)
			{
				Total += FMath::Max(Action.Variants[i].Weight, 0.f);
			}
		}
	}
	if (Total <= KINDA_SMALL_NUMBER)
	{
		// 가중치가 전부 0 — 균등으로 떨어뜨린다. 설정 실수로 아무것도 안 나오는
		// 것을 막는다.
		for (int32 i = 0; i < Num; ++i)
		{
			if (Action.Variants[i].Montage)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	float R = FMath::FRandRange(0.f, Total);
	for (int32 i = 0; i < Num; ++i)
	{
		if (!Action.Variants[i].Montage || (!bRelaxed && i == Last))
		{
			continue;
		}
		R -= FMath::Max(Action.Variants[i].Weight, 0.f);
		if (R <= 0.f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

float ACustomerCharacter::PlayAction(FName ActionName, float FallbackSeconds)
{
	if (const FCustomerAction* Found = Actions.Find(ActionName))
	{
		const int32 Idx = PickVariant(ActionName, *Found);
		if (Idx != INDEX_NONE)
		{
			UAnimMontage* M = Found->Variants[Idx].Montage;
			if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				const float Len = Anim->Montage_Play(M);
				// `Montage_Play` 는 실패하면 0 을 준다 — 그때는 기다리기라도 해야
				// 상태가 멈추지 않는다. ⚠ 0 을 그대로 돌려주면 태스크가 즉시
				// 성공해 루프가 순식간에 지나간다 (「빨리 감기」 오진의 원인).
				if (Len > 0.f)
				{
					LastVariant.Add(ActionName, Idx);
					return Len;
				}
				UE_LOG(LogBurgerHouseNPC, Warning,
					TEXT("행동 %s — 몽타주 %s 재생 실패. 시간만 센다"),
					*ActionName.ToString(), *GetNameSafe(M));
			}
			else
			{
				// ⚠ **AnimBP 가 없으면 `GetAnimInstance()` 가 널이다.** 몽타주를
				//    꽂아 놨는데도 시간만 세는 원인이 대개 이것이다 —
				//    `BP_Customer > Mesh > Anim Class` 를 볼 것.
				UE_LOG(LogBurgerHouseNPC, Warning,
					TEXT("행동 %s — AnimInstance 가 없다. Mesh 의 Anim Class 를 볼 것"),
					*ActionName.ToString());
			}
		}
	}

	// 몽타주 없음 = 정상 경로다 (헤더 주석). 경고로 찍지 않는다 — 6종이 다 빌
	// 동안 로그가 도배되면 진짜 경고가 안 보인다.
	UE_LOG(LogBurgerHouseNPC, Verbose, TEXT("행동 %s — 애님 없이 %.1fs"),
		*ActionName.ToString(), FallbackSeconds);
	return FMath::Max(FallbackSeconds, 0.f);
}

// ---------------------------------------------------------------- 반복 재생

void ACustomerCharacter::StartActionLoop(FName ActionName)
{
	if (ActionName.IsNone())
	{
		return;
	}
	LoopingAction = ActionName;
	PlayLoopedAction();
}

void ACustomerCharacter::StartDecorationLoop()
{
	if (!bDecoration)
	{
		return;			// 트리가 도는 평범한 손님 — 행동은 트리가 시킨다
	}
	const FName* Action = DecorationActions.Find(StartRole);
	if (!Action || Action->IsNone())
	{
		// ⚠ 조용히 넘어가면 「장식 손님이 가만히 서 있다」로만 보인다.
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 장식 손님인데 역할 %s 에 맞는 행동이 DecorationActions 에 없다"),
			*GetName(), *StartRole.ToString());
		return;
	}
	StartActionLoop(*Action);
}

void ACustomerCharacter::StopActionLoop()
{
	LoopingAction = NAME_None;
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(LoopTimer);
	}
	StopAction();
}

void ACustomerCharacter::PlayLoopedAction()
{
	if (LoopingAction.IsNone())
	{
		return;
	}

	// ⚠ `PlayAction` 이 **매번 변형을 새로 뽑는다.** 그래서 같은 행동 이름으로
	//    반복해도 몸짓이 계속 바뀐다 (`PickVariant` 의 가중 무작위 +
	//    `bAvoidRepeatVariant` 로 연속 중복 회피).
	const float Len = PlayAction(LoopingAction, /*FallbackSeconds=*/4.f);

	// 쉬는 시간을 섞는다 — 안 섞으면 여럿이 같이 앉아 있을 때 몸짓이 동시에
	// 바뀌어 군무처럼 보인다.
	const float Next = Len + FMath::FRandRange(0.f, FMath::Max(DecorationGap, 0.f));

	if (UWorld* W = GetWorld())
	{
		// ⚠ 최소값을 둔다. `Len` 이 0 이면(몽타주도 없고 폴백도 0) 타이머가
		//    매 프레임 돌아 사실상 무한 루프가 된다.
		W->GetTimerManager().SetTimer(LoopTimer, this,
			&ACustomerCharacter::PlayLoopedAction, FMath::Max(Next, 0.25f), false);
	}
}

void ACustomerCharacter::StopAction()
{
	if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		if (Anim->IsAnyMontagePlaying())
		{
			Anim->Montage_Stop(0.15f);
		}
	}
}

// ---------------------------------------------------------------- 착석

void ACustomerCharacter::SitAt(const FTransform& SeatTransform)
{
	if (bSitting)
	{
		return;
	}

	// ⚠ 경로 추종을 먼저 끊는다. 안 끊고 순간이동시키면 MoveTo 가 「밀렸다」고
	//    보고 되돌아오려 한다 — 앉은 채로 미끄러지는 그림이 된다.
	if (AController* C = GetController())
	{
		C->StopMovement();
	}

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();		// MOVE_None — 캡슐을 밀지 않으므로 의자를 안 뚫는다
	}

	PreSitTransform = GetActorTransform();

	// 캡슐 원점은 발밑이 아니라 **가운데**라 반높이만큼 올린다.
	//
	// ⚠ 좌석 지점의 Z 는 **바닥(0)** 이어야 한다. 앉는 애님이 골반을 좌면
	//    높이로 올려 주므로, 여기서 또 올리면 두 번 올라간다 (헤더 주석).
	FVector Loc = SeatTransform.GetLocation();
	if (const UCapsuleComponent* Cap = GetCapsuleComponent())
	{
		Loc.Z += Cap->GetScaledCapsuleHalfHeight();
	}
	Loc.Z += SitZAdjust;

	SetActorLocationAndRotation(Loc, SeatTransform.GetRotation(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	bSitting = true;
}

void ACustomerCharacter::StandUp()
{
	if (!bSitting)
	{
		return;
	}
	bSitting = false;

	// 앉기 전 자리로 되돌린다 — 좌석 안에서 일어나면 의자를 뚫고 걸어 나온다
	SetActorLocationAndRotation(PreSitTransform.GetLocation(), PreSitTransform.GetRotation(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
	}
}

// ---------------------------------------------------------------- 트레이

void ACustomerCharacter::HoldTray()
{
	if (!Tray)
	{
		return;
	}
	Tray->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, TraySocket);
	// ⚠ 본에 직접 붙이면 손목 원점·회전 기준이라 어긋난다. 여기서 보정한다.
	Tray->SetRelativeTransform(TrayHandOffset);
	Tray->SetHiddenInGame(false);
	bHasTray = true;
}

void ACustomerCharacter::PlaceTrayAt(const FTransform& Where)
{
	if (!Tray || !bHasTray)
	{
		return;
	}
	// 월드에 떼어 놓는다. 손님이 일어나 걸어가도 트레이는 테이블에 남는다.
	Tray->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Tray->SetWorldLocationAndRotation(
		Where.TransformPosition(TrayTableOffset), Where.GetRotation());
	Tray->SetHiddenInGame(false);
}

void ACustomerCharacter::DropTray()
{
	if (!Tray)
	{
		return;
	}
	// 반납대에 놓는 그림은 아직 없다. 감추고 손으로 되돌려 다음 손님이 재활용한다.
	Tray->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, TraySocket);
	Tray->SetRelativeTransform(TrayHandOffset);
	Tray->SetHiddenInGame(true);
	bHasTray = false;
}
