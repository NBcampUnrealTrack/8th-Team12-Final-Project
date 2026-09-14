// 버거리아 — 인터랙션 슬롯 앵커
//
// UE_NPC_PLAN.md 2장.
//
// ## 왜 메쉬가 없는 액터인가
//
// 의자 29개·키오스크 2대는 UE 에서 **HISM 인스턴스**다 (드로콜 때문에).
// 그런데 인스턴스에는 컴포넌트를 붙일 수 없고, Smart Object 는 컴포넌트다.
//
// NPC 는 의자를 **앉기만** 한다 — 밀지도, 빼지도 않는다. 그러면 렌더 메쉬와
// 인터랙션 앵커가 같은 액터일 이유가 없다. 좌석 트랜스폼마다 **메쉬 없는
// 가벼운 액터**를 두고 거기에 예약을 건다. 렌더 비용 0, HISM 의 드로콜 이득
// 그대로.
//
// ⚠ 의자를 물리로 밀거나 NPC 가 빼고 앉게 하고 싶어지면 이 구조를 버리고
//   의자를 개별 액터로 되돌려야 한다 (`ll_place.py` 한 곳만 고치면 된다).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SlotPoint.generated.h"

class UArrowComponent;
class UCapsuleComponent;

/** 슬롯 종류 — `FName` 이라 새 종류를 코드 수정 없이 늘릴 수 있다 */
namespace BurgerHouseSlot
{
	// ---- 예약하는 것 (한 번에 한 명) ----
	const FName Seat(TEXT("Seat"));
	const FName Kiosk(TEXT("Kiosk"));
	const FName Pickup(TEXT("Pickup"));

	// ---- 예약하지 않는 것 (여럿이 같이 쓴다) ----
	//
	// ⚠ **문은 예약 대상이 아니다.** 들어오는 사람과 나가는 사람이 동시에
	//   쓰므로 `ClaimSlot` 이 아니라 `FindPoint` 로 조회만 한다. 예약을 걸면
	//   문이 한 명에게 잠겨 뒤에 오는 손님이 영영 못 들어온다.
	const FName Entry(TEXT("Entry"));	// 스폰 지점 — 안쪽을 본다
	const FName Exit(TEXT("Exit"));		// 소멸 지점 — 바깥을 본다
}

/**
 *	NPC 가 예약(claim)하고 사용하는 지점.
 *
 *	예약 상태는 **`UStoreSubsystem` 이 소유한다.** 이 액터는 위치·방향·종류만
 *	들고 있고 누가 잡았는지도 편의를 위해 캐시할 뿐이다 — 진실은 Subsystem 에
 *	있다. 나중에 Smart Objects 로 갈아탈 때 이 액터에 컴포넌트만 붙이면 된다.
 */
UCLASS()
class ASlotPoint : public AActor
{
	GENERATED_BODY()

public:
	ASlotPoint();

	/** `Seat` / `Kiosk` / `Pickup` */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	FName Kind = BurgerHouseSlot::Seat;

	/**
	 *	NPC 가 실제로 서거나 앉을 지점.
	 *
	 *	⚠ 좌석은 **의자 원점이 아니라 앉는 지점**이다. 의자 원점에 그대로 앉히면
	 *	  등받이에 파묻힌다. 스폰 스크립트가 정면 축을 따라 앞으로 물려 놓는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	FVector LocalOffset = FVector::ZeroVector;

	/**
	 *	NPC 가 바라볼 방향을 이 액터 회전에서 **yaw 만큼 더 돌린다.**
	 *
	 *	## 왜 필요한가 — 가구 정면과 캐릭터 정면이 다르다
	 *
	 *	좌석 슬롯은 **가구(의자·소파)의 회전을 그대로 물려받는다.** UE 캐릭터의
	 *	정면은 **+X** 인데 가구 정면이 +X 가 아니면 손님이 옆을 보고 앉는다.
	 *
	 *	## ⚠⚠ 값은 **가구마다 다르다** — 「좌석은 전부 90」이 아니다
	 *
	 *	    `Chair_A`     정면 = 로컬 **+Y**  ->  **90**
	 *	    `Sofa_Unit`   정면 = 로컬 **+X**  ->  **0**
	 *
	 *	의자만 표본으로 재고 「좌석 = 90」 으로 단정했다가 소파 8석이 벽을 보고
	 *	앉았다. 실측 — 의자 29석은 `+Y` 가 테이블(`+X` 는 85~95° 어긋남),
	 *	소파 8석은 `+X` 로 56cm 앞이 테이블이고 옆으로 y≈0 이었다 (그 너머
	 *	110cm 에 마주보는 의자 — 부스 배치).
	 *
	 *	→ `ll_slots.py` 의 `USE_YAW_OFFSET` 은 **종류가 아니라 에셋 이름**으로
	 *	  건다. 새 좌석 가구를 넣으면 거기에 항목을 추가할 것.
	 *
	 *	⚠ 키오스크·픽업·문은 스폰 스크립트가 **UE yaw 를 직접 적어** 두므로
	 *	  이 값이 **0** 이어야 한다.
	 *
	 *	⚠ `LocalOffset` 은 이 값의 영향을 **안 받는다.** 오프셋은 가구 기준
	 *	  (+Y 가 앉는 사람 앞쪽)으로 이미 맞춰져 있기 때문이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	float UseYawOffset = 0.f;

	/**
	 *	같은 테이블에 속한 좌석끼리 묶는 번호. **−1 이면 소속 없음.**
	 *
	 *	## 왜 여기 있나 — 의자에는 못 붙인다
	 *
	 *	의자는 HISM **인스턴스**라 컴포넌트도 프로퍼티도 못 붙인다. 그래서
	 *	좌석마다 이 액터를 따로 두는 것이고(위 주석), 「어느 테이블 소속인가」
	 *	같은 정보도 **의자가 아니라 여기에** 붙는다.
	 *
	 *	## ⚠ `Table_A` 는 테이블이 아니라 모듈이다
	 *
	 *	`LL_placement.json` 의 `Table_A` 21개는 **0.403m 짜리 상판 모듈**이고,
	 *	두세 개가 붙어 실제 테이블 하나를 이룬다 (`Table_C1`+`C2` 가 같은 y 에
	 *	0.403m 간격). 모듈 단위로 세면 21개지만 **앉는 단위로는 10개**다.
	 *
	 *	`ll_slots.py` 가 모듈을 붙어 있는 것끼리 묶은 뒤, 각 좌석을 가장 가까운
	 *	묶음에 배정해 그 번호를 여기 넣는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	int32 GroupId = -1;

	/** 어느 인스턴스에서 왔는지 — 디버그·역추적용 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	FName SourceAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Slot")
	int32 SourceIndex = -1;

	/** 사용 지점의 월드 트랜스폼 (오프셋 적용) */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	FTransform GetUseTransform() const;

	/** 잡은 액터. 비어 있으면 자유. **진실은 Subsystem** 이고 이건 캐시다. */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Slot")
	TWeakObjectPtr<AActor> ClaimedBy;

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Slot")
	bool IsFree() const { return !ClaimedBy.IsValid(); }

private:
	UPROPERTY(VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USceneComponent* Root;

#if WITH_EDITORONLY_DATA
	/** 에디터에서 방향을 눈으로 보려고 — 쿠킹되지 않는다 */
	UPROPERTY(VisibleAnywhere, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UArrowComponent* Arrow;
#endif
};
