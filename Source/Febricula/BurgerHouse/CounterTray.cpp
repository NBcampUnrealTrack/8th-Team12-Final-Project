#include "CounterTray.h"

#include "StoreSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogBurgerHouseCounter, Log, All);

ACounterTray::ACounterTray()
{
	// ⚠ 틱을 켜지 않는다. 서브시스템이 주문 변화를 알려 준다 (헤더 주석).
	PrimaryActorTick.bCanEverTick = false;

	Tray = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tray"));
	RootComponent = Tray;

	auto MakeItem = [this](const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		if (C)
		{
			C->SetupAttachment(Tray);
			C->SetCastShadow(false);
		}
		return C;
	};
	BurgerBox = MakeItem(TEXT("BurgerBox"));
	BurgerBox2 = MakeItem(TEXT("BurgerBox2"));
	Fries = MakeItem(TEXT("Fries"));
	Coke = MakeItem(TEXT("Coke"));
	Ketchup = MakeItem(TEXT("Ketchup"));
	FriesLoose = MakeItem(TEXT("FriesLoose"));
	Receipt = MakeItem(TEXT("Receipt"));

	// ⚠ 콜리전을 **전부 끈다.** 카운터 위에 놓이는 장식인데 켜 두면 손님이
	//    픽업대 앞에서 트레이에 걸려 길을 돌아간다 — 증상이 「내비가 이상하다」로
	//    보여 원인 찾기가 고약하다 (손님 트레이와 같은 이유).
	for (UStaticMeshComponent* C : { Tray.Get(), BurgerBox.Get(), Fries.Get(), Receipt.Get() })
	{
		if (C)
		{
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetGenerateOverlapEvents(false);
		}
	}
}

void ACounterTray::BeginPlay()
{
	Super::BeginPlay();

	if (!bBindToStore)
	{
		// 테이블 위 트레이 — 주문 상태와 무관하다 (`bBindToStore` 주석)
		UE_LOG(LogBurgerHouseCounter, Log, TEXT("%s — 매장 연동 없음(테이블 트레이)"), *GetName());
	}
	else if (UStoreSubsystem* Store = GetStore())
	{
		// ⚠ 폴링하지 않는다. 주문이 준비되거나 수령될 때만 다시 본다.
		Store->OnOrderReady.AddDynamic(this, &ACounterTray::HandleOrderChanged);
		Store->OnOrderClaimed.AddDynamic(this, &ACounterTray::HandleOrderChanged);
	}
	else
	{
		UE_LOG(LogBurgerHouseCounter, Warning,
			TEXT("%s — StoreSubsystem 이 없다. 트레이가 영원히 숨어 있을 것이다"),
			*GetName());
	}

	Refresh();
}

void ACounterTray::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// ⚠ **떼지 않으면 파괴된 액터로 콜백이 날아간다.** 서브시스템은 게임 인스턴스
	//    수명이라 레벨보다 오래 산다.
	if (UStoreSubsystem* Store = GetStore())
	{
		Store->OnOrderReady.RemoveDynamic(this, &ACounterTray::HandleOrderChanged);
		Store->OnOrderClaimed.RemoveDynamic(this, &ACounterTray::HandleOrderChanged);
	}
	Super::EndPlay(EndPlayReason);
}

UStoreSubsystem* ACounterTray::GetStore() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UStoreSubsystem>() : nullptr;
}

void ACounterTray::HandleOrderChanged(int32 Number)
{
	Refresh();
}

void ACounterTray::Refresh()
{
	// 2인용이 아니면 두 번째 버거박스는 감춘다
	if (BurgerBox2)
	{
		BurgerBox2->SetVisibility(bTwoPersonSet, /*bPropagateToChildren=*/true);
	}

	if (!bBindToStore)
	{
		// ⚠ 테이블 트레이는 **늘 보인다.** 주문 상태를 보면 손님이 먹고 있는데도
		//    트레이가 사라진다.
		SetActorHiddenInGame(false);
		return;
	}

	const UStoreSubsystem* Store = GetStore();
	// ⚠⚠ **손님을 뒤지지 않는다.** 준비된 번호의 주인은 서브시스템이다 (§9.1).
	const int32 Ready = Store ? Store->GetReadyNumbers().Num() : 0;
	const bool bShow = (Ready >= ShowWhenReadyCount);

	// ⚠ 자식까지 같이 숨긴다. 빠뜨리면 트레이만 사라지고 **음식이 공중에 뜬다.**
	SetActorHiddenInGame(!bShow);

	UE_LOG(LogBurgerHouseCounter, Verbose, TEXT("%s — 준비 %d건, 표시 %s"),
		*GetName(), Ready, bShow ? TEXT("O") : TEXT("X"));
}
