#include "CustomerCharacter.h"

#include "StoreSubsystem.h"
#include "SlotPoint.h"
#include "CounterTray.h"
// ⚠ TActorIterator 의 정의는 여기 있다. 없으면 "undefined class" 로만 터진다.
#include "EngineUtils.h"
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
	// 원래 손님은 스스로 틱할 게 없었다 — 시간 세기는 StateTree 태스크가 한다.
	//
	// ⚠⚠ 그런데 **로코모션을 몽타주로 돌리면서 틱이 필요해졌다.** 걷기 시작·정지는
	//    이벤트가 없고 속도를 봐야만 알 수 있다. 상태 기계는 매 프레임 규칙을
	//    평가해 주지만 몽타주는 **우리가 알아채고 걸어 줘야** 한다
	//    (`Import/PLAN_Montage_Locomotion.md` 의 「매 프레임 반응」).
	//
	// ⚠ 그래서 30Hz 로 낮춘다. 블렌드가 0.2초라 33ms 지연은 안 보인다.
	//   `bMontageLocomotion` 이 꺼져 있으면 `BeginPlay` 가 틱을 끈다 — 예전처럼
	//   **한 번도 안 도는** 상태로 돌아간다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f / 30.f;

	// 트레이에 얹히는 것들을 만드는 작은 헬퍼 — 넷 다 설정이 같다.
	auto MakeTrayItem = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		if (C)
		{
			// ⚠ 콜리전을 켜면 손에 든 음식이 손님 캡슐을 밀거나 내비를 막는다.
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
			// ⚠ 납작한 것의 그림자는 VSM 마킹을 먹는다 (트레이와 같은 이유).
			C->SetCastShadow(false);
		}
		return C;
	};

	Tray = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tray"));
	if (Tray)
	{
		Tray->SetupAttachment(GetMesh(), TraySocket);
		// 트레이는 장식이다. 콜리전을 주면 손님이 자기 트레이에 걸린다.
		Tray->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tray->SetGenerateOverlapEvents(false);
		// ⚠ 납작한 것의 그림자는 VSM 마킹을 먹는다 (UE 함정 19).
		Tray->SetCastShadow(false);

	}

	// 트레이 위에 얹히는 것들. **전부 `Tray` 의 자식**이라 트레이를 옮기면 따라간다.
	//
	// ⚠ 메시·오프셋은 여기서 안 박는다 — `ll_tray_setup.py` 가 BP 에 꽂는다.
	//   C++ 에 경로를 박으면 아티스트가 못 바꾼다.
	BurgerBox = MakeTrayItem(TEXT("BurgerBox"));
	Fries = MakeTrayItem(TEXT("Fries"));
	Receipt = MakeTrayItem(TEXT("Receipt"));
	Coke = MakeTrayItem(TEXT("Coke"));
	Ketchup = MakeTrayItem(TEXT("Ketchup"));
	FriesLoose = MakeTrayItem(TEXT("FriesLoose"));
	for (UStaticMeshComponent* C : { BurgerBox.Get(), Fries.Get(), Receipt.Get(),
									 Coke.Get(), Ketchup.Get(), FriesLoose.Get() })
	{
		if (C)
		{
			C->SetupAttachment(Tray);
		}
	}

	// ## ⚠⚠ 숨기기는 **자식을 붙인 뒤**다
	//
	// `bPropagateToChildren` 은 **그 순간 붙어 있는 자식**에게만 내려간다. 예전에는
	// 이 줄이 `Tray` 를 만든 직후, 음식 세 개를 만들기 **전**에 있었다. 그래서
	// 트레이만 숨고 **버거·감자·영수증은 처음부터 보였다.**
	if (Tray)
	{
		Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
	}

	// ⚠ 폰은 트레이의 자식이 **아니다** — 손에 붙고 트레이와 무관하다.
	Phone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Phone"));
	if (Phone)
	{
		Phone->SetupAttachment(GetMesh());
		Phone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Phone->SetGenerateOverlapEvents(false);
		Phone->SetCastShadow(false);
		Phone->SetHiddenInGame(true);
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

		// ## ⚠⚠ 마주보고 앉으면 트레이가 **겹친다**
		//
		// 둘 다 자기 앞 `TrayTableOffset`(45cm) 에 놓으므로 테이블 한가운데서
		// 만난다 — 실측으로 **6.5cm** 떨어져 있었다.
		//
		// 그래서 **그 자리에 이미 트레이 액터가 있으면 내 것은 안 놓는다.** 거기에
		// 놓인 것이 2인용 세트(`ACounterTray::bTwoPersonSet`)다.
		//
		// ⚠ 「누가 먼저냐」를 따지지 않는다. 레벨에 놓인 액터가 늘 이긴다 —
		//   사람이 눈으로 맞춘 배치가 자동 배치보다 정확하기 때문이다.
		const FVector Where = Use.TransformPosition(TrayTableOffset);
		const AActor* Shared = nullptr;
		for (TActorIterator<ACounterTray> It(GetWorld()); It; ++It)
		{
			if (*It && FVector::Dist(It->GetActorLocation(), Where) <= SharedTrayRadius)
			{
				Shared = *It;
				break;
			}
		}

		if (Shared)
		{
			UE_LOG(LogBurgerHouseNPC, Log,
				TEXT("%s 트레이를 안 놓는다 — %s 가 %.0fcm 안에 있다 (공용 트레이)"),
				*GetName(), *Shared->GetName(), FVector::Dist(Shared->GetActorLocation(), Where));
		}
		else
		{
			// 프리필 손님은 `Pickup` 을 건너뛰므로 손에 든 적이 없다. 들려 준 뒤
			// 테이블에 놓아야 트레이가 보인다 (`PlaceTrayAt` 이 `bHasTray` 를 본다).
			HoldTray();
			PlaceTrayAt(Use);
		}

		// 장식 손님은 트리를 안 타므로 여기서 몸짓을 걸어 준다.
		//
		// ⚠ **좌석 번호를 넘긴다.** 이 값으로 몸짓을 결정적으로 고르므로 같은
		//    의자는 늘 같은 몸짓이 된다 (`DecorationSeed` 주석).
		StartDecorationLoop(Seat->SourceIndex);

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
		StartDecorationLoop(Kiosk->SourceIndex);

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
		// 줄에는 슬롯이 없다 — 줄 순번을 자리 대신 쓴다 (같은 자리 = 같은 몸짓).
		StartDecorationLoop(Index);

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

// ─────────────────────────────────────────── 로코모션을 몽타주로 (`LocoSlot`)

void ACustomerCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ## ⚠⚠ 트레이를 **여기서 다시 붙인다** — 생성자의 소켓 이름은 못 믿는다
	//
	// 생성자의 `Tray->SetupAttachment(GetMesh(), TraySocket)` 이 쓰는 `TraySocket`
	// 은 **C++ 기본값**이다. 블루프린트가 덮어쓴 값은 생성자가 끝난 뒤에 들어온다.
	//
	// 그래서 손님을 마네퀸으로 바꾸고 BP 에서 `spine_05` 로 고쳤는데도, 실제
	// 부착은 옛 기본값 `mixamorig:RightHand` 로 일어났다. 마네퀸에 없는 이름이라
	// `GetSocketTransform` 이 **컴포넌트 원점으로 폴백**하는데, 메시 컴포넌트는
	// 캡슐 기준 `z −90` 이라 그 자리가 곧 **발밑**이다.
	//
	// ⚠⚠ 죽지 않는다. 경고도 없다. 「버거가 발밑에 붙어 있다」로만 보인다.
	if (Tray)
	{
		Tray->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, TraySocket);
		Tray->SetRelativeTransform(TrayHandOffset);
		Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
	}

	// ⚠ 트레이와 **같은 이유**로 여기서 다시 붙인다 — 생성자의 소켓 이름은
	//    C++ 기본값이라 블루프린트가 덮어쓴 값이 안 들어가 있다 (§9-20).
	if (Phone)
	{
		Phone->AttachToComponent(GetMesh(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale, PhoneSocket);
		Phone->SetRelativeTransform(PhoneHandOffset);
		Phone->SetHiddenInGame(true);
	}

	// ⚠ 스위치가 꺼져 있으면 **틱을 아예 안 돈다.** 예전 동작(상태 기계)이 그대로다.
	SetActorTickEnabled(bMontageLocomotion);
}

void ACustomerCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// ⚠ **보간보다 먼저** 읽는다 — 이 틱에 좌석 보간이 옮긴 거리는 「도약」이
	//    아니라 의도된 것이므로 아래에서 따로 표시한다.
	const bool bBlendingThisTick = (SeatBlendLeft > 0.f);

	// ⚠ **로코모션보다 먼저**다. 여기서 위치를 옮긴 뒤 속도를 봐야 한다.
	if (SeatBlendLeft > 0.f)
	{
		SeatBlendLeft = FMath::Max(SeatBlendLeft - DeltaSeconds, 0.f);

		// ⚠ 등속으로 옮기면 출발과 도착에서 툭 끊긴다. SmoothStep 이 양끝을 죽인다.
		const float Alpha = (SeatBlendTime > 0.f)
			? FMath::SmoothStep(0.f, 1.f, 1.f - (SeatBlendLeft / SeatBlendTime))
			: 1.f;

		SetActorLocationAndRotation(
			FMath::Lerp(SeatBlendFrom.GetLocation(), SeatBlendTo.GetLocation(), Alpha),
			FQuat::Slerp(SeatBlendFrom.GetRotation(), SeatBlendTo.GetRotation(), Alpha),
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

		if (SeatBlendLeft <= 0.f)
		{
			if (bSeatBlendRestoreWalking)
			{
				if (UCharacterMovementComponent* Move = GetCharacterMovement())
				{
					Move->SetMovementMode(MOVE_Walking);
				}
				bSeatBlendRestoreWalking = false;
			}
			// ⚠ **다 앉은 뒤에** 트레이를 테이블에 놓는다 (`bPendingTrayOnTable` 주석).
			if (bPendingTrayOnTable)
			{
				bPendingTrayOnTable = false;
				PlaceTrayAt(SitSeatTransform);
			}

			// 보간하려고 켰던 틱을 원래대로 (`BeginSeatBlend` 주석)
			SetActorTickEnabled(bMontageLocomotion);
		}
	}

	if (bMontageLocomotion)
	{
		UpdateLocomotionMontage();
	}

	// ## 소품 — **몽타주가 도는 동안만** 보인다
	//
	// ⚠ 행동이 끝났는지로 판단하면 안 된다. 대기 상태는 몽타주가 끝나도
	//   `Order.Ready` 를 기다리며 남아 있어서 폰이 빈손에 떠 있게 된다.
	if (Phone)
	{
		const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const bool bShow = (PhoneMontage != nullptr) && Anim
			&& const_cast<UAnimInstance*>(Anim)->Montage_IsPlaying(PhoneMontage);
		if (!bShow)
		{
			PhoneMontage = nullptr;
		}
		if (Phone->bHiddenInGame == bShow)
		{
			Phone->SetHiddenInGame(!bShow);
		}
	}

	// ## 도약 검출 (`TeleportWarnDistance`)
	//
	// ⚠ 「누가 옮겼는지」를 못 찍는다 — 옮긴 쪽이 아니라 **결과**를 보기 때문이다.
	//    대신 그 순간의 상태(앉음·보간·속도)를 같이 찍어 후보를 좁힌다.
	const FVector Now = GetActorLocation();
	if (TeleportWarnDistance > 0.f && bHasLastTickLocation)
	{
		const float Moved = FVector::Dist2D(LastTickLocation, Now);
		if (Moved > TeleportWarnDistance)
		{
			UE_LOG(LogBurgerHouseNPC, Warning,
				TEXT("도약 %s — 한 틱에 %.1fcm (%s -> %s) | 앉음=%d 좌석보간=%d 속도=%.0f 역할=%s"),
				*GetName(), Moved,
				*LastTickLocation.ToCompactString(), *Now.ToCompactString(),
				bSitting ? 1 : 0, bBlendingThisTick ? 1 : 0,
				GetVelocity().Size2D(), *StartRole.ToString());
		}
	}
	LastTickLocation = Now;
	bHasLastTickLocation = true;
}

void ACustomerCharacter::UpdateLocomotionMontage()
{
	// ## 어떤 상태여야 하나
	//
	// ⚠ 앉음이 **속도보다 먼저**다. 앉은 손님은 속도가 0 이라 어차피 Idle 로
	//    가겠지만, `SitAt` 이 순간이동으로 자리를 잡는 프레임에는 속도가 튄다.
	//    순서를 뒤집으면 앉는 순간 한 프레임 걷는 그림이 나온다.
	FName Desired;
	if (bSitting)
	{
		Desired = LocoSitAction;
	}
	else
	{
		// ## ⚠⚠ 히스테리시스 — 임계값 하나로 판정하면 안 된다
		//
		// 목표에 다 와서 감속할 때 속도가 5 근처에서 떨린다. 임계값이 하나면
		// Idle↔Walk 가 초당 수십 번 뒤집히고, 그때마다 몽타주를 새로 걸어
		// **블렌드인이 계속 0 에서 다시 시작해 애님이 얼어붙는다.**
		//
		// 지금 걷는 중이면 더 낮은 값까지 버티고, 서 있으면 더 높은 값을 넘어야
		// 걷기 시작한다.
		// ⚠ **트레이를 들었으면 다른 애님**이다 — 양손이 앞으로 모여야 한다.
		//   몸통에 붙이는 것만으로는 손이 안 따라온다 (`LocoCarryWalkAction` 주석).
		const FName WalkName = (bHasTray && !LocoCarryWalkAction.IsNone())
			? LocoCarryWalkAction : LocoWalkAction;
		const FName IdleName = (bHasTray && !LocoCarryIdleAction.IsNone())
			? LocoCarryIdleAction : LocoIdleAction;

		const bool bWalkingNow = (CurrentLocoAction == WalkName);
		const float Threshold = bWalkingNow
			? FMath::Max(LocoWalkSpeed - LocoSpeedHysteresis, 0.f)
			: LocoWalkSpeed + LocoSpeedHysteresis;

		Desired = (GetVelocity().Size2D() > Threshold) ? WalkName : IdleName;
	}

	if (Desired == CurrentLocoAction)
	{
		return;			// ⚠ 바뀔 때만 건다 (헤더 주석)
	}

	if (PlayLoopingMontageAction(Desired))
	{
		CurrentLocoAction = Desired;
	}
	// ⚠ 실패하면 `CurrentLocoAction` 을 **안 바꾼다.** 다음 틱에 다시 시도하고,
	//    그동안 슬롯은 상태 기계 포즈를 통과시키므로 화면이 비지 않는다.
}

bool ACustomerCharacter::PlayLoopingMontageAction(FName ActionName)
{
	const FCustomerAction* Found = Actions.Find(ActionName);
	if (!Found || Found->Variants.Num() == 0)
	{
		// ⚠ 조용히 넘어가면 「몽타주로 바꿨는데 아무것도 안 변한다」가 된다.
		//    원인이 대개 `Actions` 키 철자다.
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 로코모션 행동 %s 가 Actions 에 없다 — 철자를 볼 것"),
			*GetName(), *ActionName.ToString());
		return false;
	}

	const int32 Idx = PickVariant(ActionName, *Found);
	UAnimMontage* M = Found->Variants.IsValidIndex(Idx) ? Found->Variants[Idx].Montage : nullptr;
	if (!M)
	{
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 로코모션 행동 %s 에 몽타주가 비어 있다"), *GetName(), *ActionName.ToString());
		return false;
	}

	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		// ⚠ AnimBP 가 없으면 여기로 온다 — `BP_Customer > Mesh > Anim Class` 를 볼 것.
		return false;
	}

	if (Anim->Montage_Play(M) <= 0.f)
	{
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 로코모션 몽타주 %s 재생 실패"), *GetName(), *GetNameSafe(M));
		return false;
	}

	// ## ⚠⚠ 여기가 루프를 거는 곳이다
	//
	// 몽타주는 기본이 1회 재생이라 끝나면 블렌드아웃되어 슬롯이 비고, 상태 기계
	// 포즈가 드러난다 — 「걷다가 갑자기 Idle 로 튄다」로 보인다.
	//
	// 섹션의 **다음 섹션을 자기 자신**으로 만들면 끊김 없이 영원히 돈다.
	// 세그먼트의 `LoopingCount` 는 이 용도가 아니다 (0 은 무한이 아니라 길이 0).
	//
	// ⚠ 섹션 이름은 박아 넣지 않는다. 팩토리 기본값이 `Default` 지만 손으로 만든
	//    몽타주는 다를 수 있다.
	if (M->GetNumSections() > 0)
	{
		const FName Sec = M->GetSectionName(0);
		Anim->Montage_SetNextSection(Sec, Sec, M);
	}
	return true;
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

int32 ACustomerCharacter::PickStableVariant(FName ActionName, int32 StableKey) const
{
	const FCustomerAction* Found = Actions.Find(ActionName);
	if (!Found || Found->Variants.Num() == 0 || StableKey == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	// ⚠ **해시를 통과시킨다.** `StableKey % Num` 으로 하면 좌석 번호가 연속이라
	//    옆자리끼리 「버거·감자·버거·감자」가 줄줄이 나온다. 눈에 바로 띈다.
	const uint32 Hash = HashCombine(GetTypeHash(ActionName),
		GetTypeHash(static_cast<uint32>(StableKey) * 2654435761u
			+ static_cast<uint32>(DecorationSeed)));

	// 가중치는 그대로 쓴다 — 난수 자리에만 해시에서 뽑은 0~1 을 넣는다.
	float Total = 0.f;
	for (const FCustomerActionVariant& V : Found->Variants)
	{
		if (V.Montage)
		{
			Total += FMath::Max(V.Weight, 0.f);
		}
	}
	if (Total <= KINDA_SMALL_NUMBER)
	{
		// 가중치가 전부 0 — 몽타주가 있는 것 중에서 균등하게 고른다
		TArray<int32, TInlineAllocator<8>> Usable;
		for (int32 i = 0; i < Found->Variants.Num(); ++i)
		{
			if (Found->Variants[i].Montage)
			{
				Usable.Add(i);
			}
		}
		return Usable.Num() ? Usable[Hash % static_cast<uint32>(Usable.Num())] : INDEX_NONE;
	}

	float R = (static_cast<float>(Hash % 100000u) / 100000.f) * Total;
	for (int32 i = 0; i < Found->Variants.Num(); ++i)
	{
		if (!Found->Variants[i].Montage)
		{
			continue;
		}
		R -= FMath::Max(Found->Variants[i].Weight, 0.f);
		if (R <= 0.f)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

float ACustomerCharacter::PlayAction(FName ActionName, float FallbackSeconds, int32 ForceVariant, float PlayRate)
{
	if (const FCustomerAction* Found = Actions.Find(ActionName))
	{
		// ⚠ 범위를 꼭 검사한다. 고정 번호는 몽타주 목록이 바뀌면 남아 있던
		//    옛 번호가 그대로 들어올 수 있다.
		const int32 Idx = (Found->Variants.IsValidIndex(ForceVariant)
				&& Found->Variants[ForceVariant].Montage)
			? ForceVariant
			: PickVariant(ActionName, *Found);
		if (Idx != INDEX_NONE)
		{
			UAnimMontage* M = Found->Variants[Idx].Montage;
			if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			{
				// ⚠⚠ `Montage_Play` 가 돌려주는 것은 **재생 속도를 안 나눈 원본 길이**다.
				//    그대로 돌려주면 속도를 2배로 올렸을 때 태스크가 **두 배**를
				//    기다린다 — 「애님은 빨리 끝났는데 손님이 멍하니 서 있다」.
				const float Rate = FMath::Max(PlayRate, KINDA_SMALL_NUMBER);
				const float Len = Anim->Montage_Play(M, Rate) / Rate;

				// ⚠ 소품은 **이 몽타주가 도는 동안만** 보인다. 여기서 기억해 두고
				//    `Tick` 이 재생 여부를 보고 켜고 끈다.
				PhoneMontage = Found->Variants[Idx].bShowPhone ? M : nullptr;
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

void ACustomerCharacter::StartActionLoop(FName ActionName, int32 FixedVariant)
{
	if (ActionName.IsNone())
	{
		return;
	}
	LoopingAction = ActionName;
	LoopVariant = FixedVariant;

	// ⚠ **첫 재생만 흩는다.** 안 흩으면 같이 앉은 손님들이 같은 프레임에
	//    시작해 8초마다 **동시에 같은 동작**을 한다 — 군무처럼 보인다.
	//    한 번 어긋나 두면 그 위상차가 계속 유지된다.
	const float Delay = FMath::FRandRange(0.f, FMath::Max(DecorationGap, 0.f));
	if (Delay > KINDA_SMALL_NUMBER)
	{
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().SetTimer(LoopTimer, this,
				&ACustomerCharacter::PlayLoopedAction, Delay, false);
			return;
		}
	}
	PlayLoopedAction();
}

void ACustomerCharacter::StartDecorationLoop(int32 StableKey)
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
	// 한 사람은 한 가지 몸짓만 — 다양성은 손님들 **사이에서** 나온다
	// (`bDecorationSingleAction` 주석). 자리에서 결정적으로 뽑으므로 같은
	// 의자는 늘 같은 몸짓이고 옆자리와는 겹치지 않는다.
	const int32 Fixed = bDecorationSingleAction
		? PickStableVariant(*Action, StableKey)
		: INDEX_NONE;

	StartActionLoop(*Action, Fixed);

	UE_LOG(LogBurgerHouseNPC, Log, TEXT("%s 장식 몸짓 %s · 변형 %d (자리 %d · 씨앗 %d)"),
		*GetName(), *Action->ToString(), Fixed, StableKey, DecorationSeed);
}

void ACustomerCharacter::StopActionLoop()
{
	LoopingAction = NAME_None;
	LoopVariant = INDEX_NONE;
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

	// `LoopVariant` 가 있으면 **그 몽타주만** 계속 돈다 (장식 손님). 없으면
	// `PlayAction` 이 매번 새로 뽑아 몸짓이 바뀐다.
	const float Len = PlayAction(LoopingAction, /*FallbackSeconds=*/4.f, LoopVariant);

	// ---------------------------------------------------------- 이음매 감추기
	//
	// ⚠⚠ **몽타주가 끝나기 전에** 다음 것을 건다. 끝까지 두면 —
	//
	//     먹는 중 → (BlendOut) 앉은 Idle → 빈 시간 → (BlendIn) 먹는 중
	//
	//   즉 손님이 루프마다 **버거를 내려놨다 다시 든다.** `Auto Blend Out` 이
	//   기본 포즈로 되돌리기 때문인데, 증상이 「애님 첫·끝 프레임이 안 맞는다」
	//   와 똑같아 보여서 애님을 자르려 들기 쉽다.
	//
	// 겹쳐 걸면 기본 포즈를 안 거치고 **끝 자세 → 시작 자세로 직접** 블렌드된다.
	//
	// ⚠ 먹는 애님은 **순환 동작이 아니다** — 시작 자세로 돌아오는 지점이 애님
	//   안에 아예 없다 (실측: 가장 비슷한 지점도 지금 끝과 차이가 거의 같다).
	//   그래서 「첫·끝 프레임 맞추기」로는 못 고치고, 이렇게 덮어야 한다.
	//   팔뚝이 최대 68도 차이인데 0.35초에 걸치면 초당 195도 — 그냥 팔 동작이다.
	const float Next = FMath::Max(Len - FMath::Max(DecorationLoopCrossfade, 0.f), 0.25f);

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

void ACustomerCharacter::BeginSeatBlend(const FTransform& To, bool bRestoreWalkingWhenDone)
{
	bSeatBlendRestoreWalking = bRestoreWalkingWhenDone;

	if (SeatBlendTime <= 0.f)
	{
		// 예전 동작 — 그 자리에서 스냅
		SetActorLocationAndRotation(To.GetLocation(), To.GetRotation(),
			/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		if (bRestoreWalkingWhenDone)
		{
			if (UCharacterMovementComponent* Move = GetCharacterMovement())
			{
				Move->SetMovementMode(MOVE_Walking);
			}
		}
		return;
	}

	SeatBlendFrom = GetActorTransform();
	SeatBlendTo = To;
	SeatBlendLeft = SeatBlendTime;

	// ⚠ 틱이 꺼져 있을 수 있다 (`bMontageLocomotion` 이 false 면 `BeginPlay` 가
	//    껐다). 보간은 틱이 돌아야 하므로 여기서 켠다 — 끝나면 원래대로 돌린다.
	SetActorTickEnabled(true);
}

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

	// ## ⚠⚠ 여기까지 **걸어서** 왔어야 한다
	//
	// 남은 거리가 크면 아래 보간이 그 거리를 통째로 메운다 — 그것이 화면에서는
	// 「자리로 순간이동」이다. 보간은 **증상 완화**일 뿐 원인이 아니다.
	//
	// 실측으로 두 번 걸렸다 —
	//  1. `Root/Pickup` 에 붙인 알림음 태스크가 `Succeeded` 를 돌려줘 픽업 갈래가
	//     통째로 건너뛰어졌다.
	//  2. `S_Move` 의 `bRequireNavigableEndLocation` 이 켜져 있어, 의자 위(내비
	//     바깥)를 목적지로 받은 `MoveTo` 가 **즉시 Failed** 로 끝났다.
	//
	// 둘 다 「이동이 조용히 안 일어난다」라 화면만 봐서는 안 보인다. 그래서 여기서
	// 거리를 **소리 내서** 찍는다.
	const float Gap = FVector::Dist2D(GetActorLocation(), Loc);
	if (Gap > SeatWarnDistance)
	{
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 착석 — 좌석까지 **%.0fcm** 남은 채로 앉는다. 내비 투영 거리(보통 60~120cm)")
			TEXT("보다 훨씬 크면 **이동이 아예 안 일어난 것**이다 — 상태가 건너뛰어졌거나")
			TEXT("(태스크가 Succeeded 를 돌려줌) 경로가 막혔다."),
			*GetName(), Gap);
	}

	// ⚠ 트레이를 테이블에 놓을 때 쓴다 (`SitSeatTransform` 주석). **좌석 지점**
	//    그대로다 — 캡슐 높이를 더한 `Loc` 이 아니다. `TrayTableOffset` 이 좌석
	//    기준으로 맞춰져 있기 때문이다.
	SitSeatTransform = SeatTransform;

	// ⚠ **스냅이 아니라 미끄러진다** (`SeatBlendTime`). 남은 몇 cm 가 한 프레임에
	//    사라지면 그것도 눈에 띈다.
	BeginSeatBlend(FTransform(SeatTransform.GetRotation(), Loc), /*bRestoreWalkingWhenDone=*/false);

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
	//
	// ⚠ 걷기로 되돌리는 것은 **미끄러짐이 끝난 뒤**다. 도중에 켜면 캡슐이
	//    의자 안에서 움직이기 시작해 기물을 뚫는다.
	BeginSeatBlend(PreSitTransform, /*bRestoreWalkingWhenDone=*/true);
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
	// ⚠⚠ **자식까지** 전파해야 한다. 생성자가 `SetHiddenInGame(true, true)` 로
	//    내렸기 때문에 버거박스·감자·영수증은 **각자** 숨김 플래그를 들고 있다.
	//    여기서 전파를 빼면 트레이만 나타나고 **음식은 계속 안 보인다.**
	Tray->SetHiddenInGame(false, /*bPropagateToChildren=*/true);
	bHasTray = true;
}

void ACustomerCharacter::PlaceTrayAt(const FTransform& Where)
{
	if (!Tray || !bHasTray)
	{
		return;
	}

	FTransform Table = Where;

	// ## ⚠⚠ 빈 트랜스폼이 들어온다
	//
	// `S_Eat` 의 `Tray[PlaceOnTable]` 은 `TableLocation`/`TableRotation` 을
	// 바인딩으로 받는데 그것이 **비어 있다** (실측: 둘 다 0). 그대로 쓰면
	// 트레이가 **월드 원점**에 놓이고, 화면에서는 「트레이가 사라졌다」로 보인다.
	//
	// ⚠ 앉은 좌석을 알고 있으므로 그것으로 대신한다. 소리 내서 찍는다 — 조용히
	//   메우면 바인딩이 빈 채로 영원히 남는다.
	if (Table.GetLocation().IsNearlyZero())
	{
		UE_LOG(LogBurgerHouseNPC, Warning,
			TEXT("%s 트레이 놓기 — 받은 자리가 비어 있다(0,0,0). 앉은 좌석 %s 으로 대신한다. ")
			TEXT("트리의 `Tray[PlaceOnTable]` ▸ TableLocation 바인딩을 볼 것"),
			*GetName(), *SitSeatTransform.GetLocation().ToCompactString());
		Table = SitSeatTransform;
	}

	// ⚠ **다 앉은 뒤에** 놓는다. 아직 걸어가는 중이면 미뤄 둔다
	//   (`bPendingTrayOnTable` 주석) — 안 그러면 빈손으로 걸어온다.
	if (!bSitting || SeatBlendLeft > 0.f)
	{
		bPendingTrayOnTable = true;
		UE_LOG(LogBurgerHouseNPC, Log,
			TEXT("%s 트레이 놓기를 **미룬다** — 아직 앉는 중 (앉음=%d, 남은보간=%.2fs)"),
			*GetName(), bSitting ? 1 : 0, SeatBlendLeft);
		return;
	}

	// 월드에 떼어 놓는다. 손님이 일어나 걸어가도 트레이는 테이블에 남는다.
	Tray->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	const FVector Put = Table.TransformPosition(TrayTableOffset);
	Tray->SetWorldLocationAndRotation(Put, Table.GetRotation());
	Tray->SetHiddenInGame(false, /*bPropagateToChildren=*/true);

	// ⚠ **놓았다는 것도 찍는다.** 안 찍으면 「테이블에 안 올라간다」를 진단할 때
	//   여기까지 왔는지 못 왔는지를 못 가른다 — 실제로 한 번 헤맸다.
	UE_LOG(LogBurgerHouseNPC, Log,
		TEXT("%s 트레이를 테이블에 놓았다 — %s (좌석 %s + 오프셋 %s)"),
		*GetName(), *Put.ToCompactString(),
		*Table.GetLocation().ToCompactString(), *TrayTableOffset.ToCompactString());
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
	Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
	bHasTray = false;
}
