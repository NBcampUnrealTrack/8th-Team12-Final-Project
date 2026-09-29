// 버거리아 — 카운터 위에 놓이는 트레이 (픽업 대기 중인 주문)
//
// ## 왜 손님의 트레이와 따로 있나
//
// `ACustomerCharacter::Tray` 는 **손님에게 붙어 있는 컴포넌트**다. 손에 들기
// 전에는 숨어 있다가 `HoldTray()` 로 나타난다 — 액터를 새로 스폰·파괴하지
// 않으려고 그렇게 설계돼 있다.
//
// 그래서 「카운터 위에 주문이 나와 있다」를 표현할 것이 없었다. 이 액터가 그
// 자리를 채운다.
//
// ## ⚠ 이것은 **장식**이다 — 예약하지 않는다
//
// 픽업 슬롯의 예약은 `UStoreSubsystem` 이 한다 (§9.1 「소유자를 분리하는 것이
// 설계의 전부다」). 이 액터는 **보이기만** 하고, 손님이 주문을 수령하면
// (`OnOrderClaimed`) 숨는다.
//
// ⚠ 예약 체계를 하나 더 만들지 말 것. 「누가 이 트레이를 가져갔나」를 여기서
//   들고 있으면 서브시스템과 두 벌이 되고, 손님이 사라졌을 때 유령이 남는다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CounterTray.generated.h"

class UStaticMeshComponent;
class UStoreSubsystem;

/**
 *	픽업대에 놓인 트레이 한 벌 — 트레이 + 버거박스 + 감자 + 영수증.
 *
 *	준비된 주문이 **하나라도 있으면** 보이고, 없으면 숨는다. 몇 건이 준비됐든
 *	한 벌만 놓는다 — 개수를 맞추려면 액터를 여러 개 놓으면 된다.
 */
UCLASS()
class FEBRICULA_API ACounterTray : public AActor
{
	GENERATED_BODY()

public:
	ACounterTray();

	/** 준비된 주문 유무를 다시 읽어 보이기/숨기기를 정한다. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Counter")
	void Refresh();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Tray;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BurgerBox;

	/**
	 *	**2인용**일 때만 보이는 두 번째 버거박스 (`bTwoPersonSet`).
	 *
	 *	⚠ 마주보고 앉은 손님 둘이 각자 트레이를 놓으면 테이블 한가운데서 **겹친다**
	 *	  (실측: 6.5cm 떨어져 있었다). 그때는 트레이 **하나**에 버거박스 둘을 담는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BurgerBox2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Fries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Receipt;

	/**
	 *	감자튀김 세트 (`FRIES_ASSET.md`) — 봉투감자 · 콜라 · 흩어진 감자 · 케찹.
	 *
	 *	⚠⚠ 이 넷은 원래 **셸 메시에 합쳐져** 들어온다 (`ll_import.py` 가
	 *	  `ALL_MESHES_INTO_ONE`). 트레이에 얹으려고 `ll_fries_set_import.py` 가
	 *	  합치지 않고 한 번 더 구워 `Props/Food/` 에 따로 뽑아 뒀다.
	 *
	 *	⚠ 그래서 **피벗이 월드 원점**이고 지오메트리만 테이블 위 좌표에 있다
	 *	  (블렌더에서 배치를 버텍스에 구웠기 때문). 상대 위치가 −290 같은 값으로
	 *	  보이는 것이 정상이다 — 배치 스크립트가 `bounds.origin` 을 빼서 계산한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Coke;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Ketchup;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> FriesLoose;


	/**
	 *	이 번호 이상 준비돼 있어야 보인다.
	 *
	 *	⚠ `1` 이면 「준비된 주문이 하나라도 있으면 보인다」. 트레이를 여러 개
	 *	  놓고 `1` · `2` · `3` 을 주면 **줄줄이 쌓이는** 그림이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Counter", meta = (ClampMin = "1"))
	int32 ShowWhenReadyCount = 1;

	/**
	 *	**2인용 세트**인가. 켜면 버거박스가 둘이 된다.
	 *
	 *	⚠ 트레이 크기는 그대로다 — 담는 것만 바뀐다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Counter")
	bool bTwoPersonSet = false;

	/**
	 *	매장의 주문 상태에 **연동**할 것인가.
	 *
	 *	⚠⚠ 픽업대 트레이는 켠다(주문이 나와야 보인다). **테이블 위 트레이는 끈다**
	 *	  — 이미 먹고 있는 손님의 것이라 주문 상태와 아무 상관이 없다. 켜 두면
	 *	  주문이 없을 때 테이블 트레이가 통째로 사라진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Counter")
	bool bBindToStore = true;

private:
	UStoreSubsystem* GetStore() const;

	UFUNCTION()
	void HandleOrderChanged(int32 Number);
};
