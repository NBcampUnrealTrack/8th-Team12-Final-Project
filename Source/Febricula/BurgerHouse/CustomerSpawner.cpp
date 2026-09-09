#include "CustomerSpawner.h"

#include "Algo/BinarySearch.h"
#include "CustomerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

ACustomerSpawner::ACustomerSpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
}

void ACustomerSpawner::BeginPlay()
{
	Super::BeginPlay();

	// 클라이언트에서 타이머를 돌리면 중복 스폰된다.
	if (!HasAuthority())
	{
		return;
	}

	BuildDistribution();

	if (bPrefillOnBeginPlay)
	{
		Prefill();
	}

	// 첫 회는 바로 돌리고, 이후는 `TrySpawn` 이 스스로 다음 주기를 흔들어 다시 건다.
	TrySpawn();
}

void ACustomerSpawner::RebuildDistribution()
{
	BuildDistribution();
}

void ACustomerSpawner::BuildDistribution()
{
	SampleDistances.Reset();
	CDF.Reset();
	DesiredAlive = 0;

	if (!Spline)
	{
		return;
	}

	const float Length = Spline->GetSplineLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: 스플라인 길이가 0 — 포인트를 두 개 이상 놓을 것"), *GetName());
		return;
	}

	const int32 Count = FMath::Max(2, FMath::CeilToInt(Length / SampleSpacing));

	SampleDistances.Reserve(Count);
	CDF.Reserve(Count);

	// 스플라인 위 **균등 분포.** 샘플마다 같은 가중이라 CDF 는 등차수열이 된다 —
	// 굳이 CDF 를 남겨 둔 이유는 `SampleLocation` 의 이진 탐색 경로를 그대로
	// 쓰기 위해서다 (가중을 다시 넣고 싶어지면 여기만 고치면 된다).
	float Running = 0.f;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Dist = (Length * i) / (Count - 1);
		Running += 1.f;
		SampleDistances.Add(Dist);
		CDF.Add(Running);
	}

	// `FixedDesiredAlive` 가 있으면 길이 공식을 건너뛴다 — 문 앞 짧은 스플라인은
	// `Length/10000` 이 0.02 라 공식이 사실상 0 을 낸다 (헤더 주석 ②).
	DesiredAlive = (FixedDesiredAlive > 0)
		? FMath::Min(FixedDesiredAlive, HardMaxAlive)
		: FMath::Clamp(
			FMath::RoundToInt((Length / 10000.f) * NPCsPer100m),
			0, HardMaxAlive);

	UE_LOG(LogTemp, Log, TEXT("%s: length=%.0f samples=%d desiredAlive=%d%s"),
		*GetName(), Length, Count, DesiredAlive,
		FixedDesiredAlive > 0 ? TEXT(" (고정)") : TEXT(""));

	if (DesiredAlive == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: DesiredAlive 가 0 — 스폰되지 않는다. 짧은 스플라인이면 FixedDesiredAlive 를 쓸 것"),
			*GetName());
	}
}

bool ACustomerSpawner::SampleLocation(FVector& OutLocation) const
{
	const float Total = CDF.Num() > 0 ? CDF.Last() : 0.f;
	if (Total <= KINDA_SMALL_NUMBER)
	{
		// 스플라인 전체가 히트 0 구역이다. (경고는 BuildDistribution 에서 한 번만)
		return false;
	}

	const float R = FMath::FRandRange(0.f, Total);
	const int32 Idx = FMath::Clamp(Algo::LowerBound(CDF, R), 0, CDF.Num() - 1);

	// 인접 샘플 사이에서 한 번 더 랜덤. 계단 현상 제거.
	const float D0 = SampleDistances[FMath::Max(Idx - 1, 0)];
	const float D1 = SampleDistances[Idx];
	const float Dist = FMath::FRandRange(D0, D1);

	const FVector Center = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
	const FVector Right  = Spline->GetRightVectorAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
	const FVector Candidate = Center + Right * FMath::FRandRange(-LateralWidth, LateralWidth);

	if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation Projected;
		if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(200.f, 200.f, 500.f)))
		{
			OutLocation = Projected.Location + FVector(0.f, 0.f, SpawnZOffset);
			return true;
		}
	}

	// 네비메시 부재 또는 LateralWidth 과대.
	return false;
}

APawn* ACustomerSpawner::SpawnOne()
{
	FVector Loc;
	if (!NPCClass || !SampleLocation(Loc))
	{
		return nullptr;
	}

	// 스플라인에서 뽑은 자리 — 방향은 아무렇게나. 어차피 곧 목적지를 향해 돈다.
	return SpawnAt(FTransform(
		FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Loc));
}

APawn* ACustomerSpawner::SpawnAt(const FTransform& Where)
{
	if (!NPCClass)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* NPC = GetWorld()->SpawnActor<APawn>(
		NPCClass, Where.GetLocation(), Where.GetRotation().Rotator(), Params);
	if (!NPC)
	{
		return nullptr;
	}

	// ⚠⚠ **컨트롤러를 여기서 붙이지 않는다** — `StartControllers` 가 나중에 한다.
	//
	//    `SpawnDefaultController()` 는 `OnPossess` 를 거쳐 `StartLogic()` 을 부르고,
	//    그것이 `StateTreeComponent.cpp:160` → `StartTree()` → `Context.Start()` 로
	//    **초기 상태 선택을 그 자리에서 동기로 끝낸다.**
	//
	//    그런데 역할·일행 통보(`OnAssignedSpawnRole`·`OnSpawnedInGroup`)는 무리가
	//    **다 태어난 뒤에야** 옳다. 여기서 컨트롤러를 붙이면 그 통보보다 선택이
	//    먼저 일어나고, 트리는 **역할이 비어 있는 상태로** 갈래를 고른다 →
	//    프리필 손님 전원이 조건 없는 기본 갈래(`Enter`)로 떨어진다.
	//
	//    ⚠ 이 실패는 **조용하다.** 역할은 나중에 정상적으로 붙고 로그에도 찍히지만
	//      트리는 이미 딴 데로 갔다. 증상이 「자식 순서를 잘못 놨다」와 똑같아서
	//      트리만 몇 번을 들여다봐도 안 나온다.

	// 사망 시 래그돌로 남기는 설계라면 이 델리게이트가 오지 않는다.
	// 그 경우 캐릭터에 OnDeath를 따로 만들어 구독할 것.
	//
	// ⚠ LL 손님은 §9.5 의 `Leave` 에서 **`Destroy` 로 끝난다** — 그래서 이
	//   델리게이트가 정상적으로 온다. 래그돌 문제는 해당 없다.
	NPC->OnDestroyed.AddDynamic(this, &ACustomerSpawner::HandleNPCDestroyed);
	Alive.Add(NPC);

	return NPC;
}

void ACustomerSpawner::TrySpawn()
{
	Alive.RemoveAll([](const TWeakObjectPtr<APawn>& P) { return !P.IsValid(); });

	// ⚠ **다음 주기를 먼저 다시 건다.** 아래에서 일찍 빠져나가도 타이머는
	//    계속 돌아야 한다. 마지막에 걸면 `return` 경로에서 스포너가 죽는다.
	RescheduleTimer();

	if (Alive.Num() >= DesiredAlive)
	{
		return;
	}

	// 이번에 몇 명을 내보낼지. `DesiredAlive` 를 넘지 않게 자른다.
	const int32 Room = DesiredAlive - Alive.Num();

	// 문으로 들어오는 손님에게는 역할이 없다 — 트리의 기본 갈래로 떨어진다.
	SpawnGroup(FMath::Min(PickBurstCount(), Room), NAME_None);
}

int32 ACustomerSpawner::SpawnGroup(int32 Count, FName InRole, bool bAsDecoration)
{
	// ⚠ `Role` 이라는 이름은 못 쓴다 — `AActor::Role`(네트 롤)을 가린다 (C4458).
	if (Count <= 0)
	{
		return 0;
	}

	// ⚠ **번호를 먼저 뽑는다.** 아래에서 한 명이라도 실패해 인원이 줄어도
	//    같이 태어난 것들은 **같은 번호**를 들고 있어야 한다.
	const int32 GroupId = NextGroupId++;

	TArray<APawn*> Born;
	Born.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		APawn* P = SpawnOne();
		if (!P)
		{
			// ⚠ 한 명이 실패하면 나머지도 실패한다 (내비 없음·CDF 0 등 같은 원인).
			//    무리해서 반복하지 않는다 — 다음 주기에 다시 온다.
			break;
		}
		Born.Add(P);
	}

	// ⚠ **전부 태어난 뒤에 통보한다.** 스폰 도중에 알리면 `GroupSize` 가 아직
	//    확정되지 않아, 먼저 태어난 쪽이 "우리는 3명" 이라고 믿었는데 실제로는
	//    2명만 나오는 일이 생긴다. 일행 인원으로 테이블을 고르는 쪽에서는
	//    그 한 명 차이가 「자리 부족」으로 바로 드러난다.
	for (int32 i = 0; i < Born.Num(); ++i)
	{
		ACustomerCharacter* C = Cast<ACustomerCharacter>(Born[i]);
		if (!C)
		{
			// ⚠ 조용히 넘어가면 「프리필이 안 먹는다」로만 보인다. NPCClass 를
			//    엉뚱한 폰으로 둔 것이 원인이므로 그걸 그대로 찍는다.
			UE_LOG(LogTemp, Warning,
				TEXT("%s: %s 는 ACustomerCharacter 가 아니다 — 일행·역할 통보를 못 한다. NPCClass 를 볼 것"),
				*GetName(), *GetNameSafe(Born[i]));
			continue;
		}

		// ⚠ **통보보다 먼저 표시한다** — `ApplyStartRole` 이 이 값을 보고 몸짓을 건다.
		C->SetDecoration(bAsDecoration);

		// ⚠ **역할이 먼저다.** 받는 쪽이 `OnSpawnedInGroup` 에서 역할을 보고
		//    좌석을 고르는데, 순서가 반대면 그때 역할이 아직 비어 있다.
		if (!InRole.IsNone())
		{
			C->OnAssignedSpawnRole(InRole);
		}
		C->OnSpawnedInGroup(GroupId, Born.Num(), i);
	}

	// ⚠ **장식이면 AI 를 안 켠다** — 컨트롤러가 없으면 트리가 시작조차 안 한다.
	//    켤 때는 통보가 끝난 뒤여야 한다 (`StartControllers` 주석).
	if (!bAsDecoration)
	{
		StartControllers(Born);
	}

	return Born.Num();
}

void ACustomerSpawner::StartControllers(const TArray<APawn*>& Pawns)
{
	for (APawn* P : Pawns)
	{
		if (P && !P->GetController())
		{
			P->SpawnDefaultController();
		}
	}
}

void ACustomerSpawner::PrefillFromPlacements()
{
	// ⚠ 정원을 넘겨 채우지 않는다 — 넘기면 문으로 들어오는 손님이 안 생긴다.
	const int32 Room = FMath::Max(DesiredAlive, 0);
	if (PrefillPlacements.Num() > Room)
	{
		// **자른 것을 반드시 알린다.** 조용히 자르면 「목록에 적었는데 안 나온다」가
		// 되고, 원인이 정원이라는 걸 짐작할 방법이 없다.
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 프리필 목록 %d개 중 %d개만 쓴다 — 정원(DesiredAlive)이 %d 다. FixedDesiredAlive 를 올릴 것"),
			*GetName(), PrefillPlacements.Num(), Room, Room);
	}
	const int32 Count = FMath::Min(PrefillPlacements.Num(), Room);

	// ---- 일행 번호로 묶는다 ----
	//
	// ⚠ **먼저 묶고 나중에 낳는다.** `GroupSize` 는 무리가 확정된 뒤에야 옳다
	//    (`SpawnGroup` 주석과 같은 이유) — 같은 테이블에 앉히는 쪽이 인원을
	//    보고 테이블을 고르기 때문에 한 명 차이가 「자리 부족」으로 바로 드러난다.
	//
	//    `PartyIndex < 0` 은 각자 혼자다. 음수를 한 덩어리로 묶으면 흩어져 앉아야
	//    할 손님들이 억지로 한 테이블에 몰린다.
	TArray<TArray<int32>> Parties;			// 각 원소 = PrefillPlacements 인덱스들
	TMap<int32, int32> IndexToParty;		// PartyIndex → Parties 위치

	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Key = PrefillPlacements[i].PartyIndex;
		if (Key < 0)
		{
			Parties.Add({ i });
			continue;
		}
		if (int32* Found = IndexToParty.Find(Key))
		{
			Parties[*Found].Add(i);
		}
		else
		{
			IndexToParty.Add(Key, Parties.Add({ i }));
		}
	}

	int32 Made = 0;
	for (const TArray<int32>& Party : Parties)
	{
		const int32 GroupId = NextGroupId++;

		// ⚠ 역할은 **무리의 첫 항목**에서 가져온다. 한 무리가 서로 다른 역할이면
		//    일행이 아니다 — 하나는 앉고 하나는 줄을 서게 된다.
		// ⚠ 이름을 `Role` 로 못 쓴다 — `AActor::Role`(네트 롤)을 가린다 (C4458).
		const FName PartyRole = PrefillPlacements[Party[0]].Role;

		TArray<APawn*> Born;
		Born.Reserve(Party.Num());
		for (const int32 Idx : Party)
		{
			const FSpawnPlacement& P = PrefillPlacements[Idx];

			// ⚠ 캡슐 반높이를 더한다. 목록의 자리는 `ASlotPoint` 에서 읽은
			//    **발밑** 좌표라 그대로 스폰하면 바닥에 반쯤 묻힌다.
			FTransform T = P.Transform;
			T.AddToTranslation(FVector(0.f, 0.f, SpawnZOffset));

			if (APawn* NPC = SpawnAt(T))
			{
				Born.Add(NPC);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("%s: 프리필 %d번 자리에 스폰 실패"),
					*GetName(), Idx);
			}
		}

		for (int32 i = 0; i < Born.Num(); ++i)
		{
			ACustomerCharacter* C = Cast<ACustomerCharacter>(Born[i]);
			if (!C)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("%s: %s 는 ACustomerCharacter 가 아니다 — 프리필 역할을 못 준다. NPCClass 를 볼 것"),
					*GetName(), *GetNameSafe(Born[i]));
				continue;
			}
			// ⚠ **역할·일행 통보보다 먼저 표시한다.** `ApplyStartRole` 이
			//    `OnSpawnedInGroup` 안에서 돌면서 이 값을 보고 몸짓을 건다.
			C->SetDecoration(!bPrefillRunsAI);

			// 역할이 먼저다 (`SpawnGroup` 의 같은 주석).
			if (!PartyRole.IsNone())
			{
				C->OnAssignedSpawnRole(PartyRole);
			}
			C->OnSpawnedInGroup(GroupId, Born.Num(), i);
		}

		// ⚠ **장식이면 AI 를 안 켠다.** 컨트롤러가 없으면 StateTree 가 시작조차
		//    안 하므로 손님이 자리에 영원히 남는다 (헤더 `bPrefillRunsAI`).
		//    켤 때는 위 통보와 `ApplyStartRole` 의 자리 잡기가 전부 끝난 뒤여야
		//    트리가 역할을 보고 갈래를 고른다.
		if (bPrefillRunsAI)
		{
			StartControllers(Born);
		}

		Made += Born.Num();
	}

	UE_LOG(LogTemp, Log, TEXT("%s: 프리필 %d명 · 무리 %d개 (목록 지정, 정원 %d)"),
		*GetName(), Made, Parties.Num(), DesiredAlive);
}

void ACustomerSpawner::Prefill()
{
	// ⚠ **목록이 있으면 뽑지 않는다.** 가중치 뽑기는 자리 수를 모르기 때문에
	//    「키오스크 2대에 Ordering 3명」이나 「Eating 0명」이 실제로 나온다.
	if (PrefillPlacements.Num() > 0)
	{
		PrefillFromPlacements();
		return;
	}

	// ⚠ **정원을 넘겨 채우지 않는다.** 프리필로 `DesiredAlive` 를 다 써 버리면
	//    문으로 들어오는 손님이 영영 안 생긴다 — 스포너가 죽은 것처럼 보인다.
	const int32 Target = FMath::Min(
		PrefillCount > 0 ? PrefillCount : DesiredAlive, DesiredAlive);

	if (Target <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("%s: 프리필을 켰는데 채울 수가 0 이다 — PrefillCount 또는 FixedDesiredAlive 를 볼 것"),
			*GetName());
		return;
	}

	if (PrefillRoles.Num() == 0)
	{
		// ⚠ 조용히 넘어가면 프리필 손님이 전부 문 앞에 겹쳐 생기고, 그 증상은
		//    「스폰 위치가 틀렸다」로 보인다. 원인은 여기다.
		UE_LOG(LogTemp, Warning,
			TEXT("%s: PrefillRoles 가 비었다 — 프리필 %d명이 역할 없이(문으로 막 들어온 손님과 같게) 생긴다"),
			*GetName(), Target);
	}

	int32 Made = 0;
	int32 Guard = 0;
	while (Made < Target)
	{
		// ⚠ **무한 루프 방지.** `SpawnOne` 이 계속 실패하면(내비 미굽기·CDF 0)
		//    `Made` 가 안 늘어 여기서 영원히 돈다 — BeginPlay 안이라 에디터가 멈춘다.
		if (++Guard > Target * 4 + 8)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: 프리필 %d/%d 에서 멈춤 — 스폰이 계속 실패한다. NavMesh 를 구웠는지 볼 것"),
				*GetName(), Made, Target);
			break;
		}

		// 한 무리는 한 역할이다 (헤더 주석). 남은 자리를 넘기지 않게 자른다.
		const int32 N = FMath::Min(PickBurstCount(), Target - Made);
		const int32 Got = SpawnGroup(N, PickPrefillRole(), !bPrefillRunsAI);
		if (Got == 0)
		{
			continue;		// Guard 가 받아 준다
		}
		Made += Got;
	}

	UE_LOG(LogTemp, Log, TEXT("%s: 프리필 %d명 (정원 %d)"), *GetName(), Made, DesiredAlive);
}

FName ACustomerSpawner::PickPrefillRole() const
{
	float Total = 0.f;
	for (const FSpawnRoleWeight& R : PrefillRoles)
	{
		Total += FMath::Max(R.Weight, 0.f);
	}

	// 비었거나 전부 0 — 역할 없음. 경고는 `Prefill` 이 한 번만 찍는다.
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return NAME_None;
	}

	float R = FMath::FRandRange(0.f, Total);
	for (const FSpawnRoleWeight& Entry : PrefillRoles)
	{
		R -= FMath::Max(Entry.Weight, 0.f);
		if (R <= 0.f)
		{
			return Entry.Role;
		}
	}
	return PrefillRoles.Last().Role;
}

int32 ACustomerSpawner::PickBurstCount() const
{
	// 가중치 합. 합이 1일 필요는 없다 — 여기서 정규화한다.
	float Total = 0.f;
	for (const float W : SpawnBurstWeights)
	{
		Total += FMath::Max(W, 0.f);
	}

	// ⚠ 배열이 비었거나 전부 0 이면 **1명**. 0을 돌려주면 스포너가 조용히
	//    아무것도 안 하게 되는데, 그건 설정 실수와 구별이 안 된다.
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return 1;
	}

	float R = FMath::FRandRange(0.f, Total);
	for (int32 i = 0; i < SpawnBurstWeights.Num(); ++i)
	{
		R -= FMath::Max(SpawnBurstWeights[i], 0.f);
		if (R <= 0.f)
		{
			return i + 1;			// [0] 이 1명이다
		}
	}
	return SpawnBurstWeights.Num();
}

void ACustomerSpawner::RescheduleTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// ⚠ 고정 주기는 메트로놈으로 보인다. 매번 흔들어 다시 건다.
	float Next = SpawnInterval;
	if (SpawnIntervalJitter > 0.f)
	{
		Next += FMath::FRandRange(-SpawnIntervalJitter, SpawnIntervalJitter);
	}
	Next = FMath::Max(Next, 0.05f);

	// 반복 없는 1회성 타이머를 매번 새로 건다 — 그래야 주기를 흔들 수 있다.
	World->GetTimerManager().SetTimer(
		SpawnTimer, this, &ACustomerSpawner::TrySpawn, Next, /*bLoop=*/false);
}

void ACustomerSpawner::HandleNPCDestroyed(AActor* DestroyedActor)
{
	Alive.RemoveAll([DestroyedActor](const TWeakObjectPtr<APawn>& P)
	{
		return !P.IsValid() || P.Get() == DestroyedActor;
	});
}
