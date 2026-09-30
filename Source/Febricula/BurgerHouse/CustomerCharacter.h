// 버거리아 — 손님 NPC
//
// LL_PROJECT.md §9.5 손님 루프.
//
// ## 이 클래스가 푸는 문제 하나 — 애님이 없어도 루프가 돌아야 한다
//
// §9.4 가 애니메이션 6종을 **유일한 진짜 블로커**라고 적어 뒀고 §10.1 도 3순위로
// 올려 뒀다. 그런데 그 6종을 기다리면 Phase 6(StateTree 그래프)·Phase 7 이 전부
// 뒤로 밀린다 — §9.4 자신이 "**Phase 6 까지 미루지 말 것 · 다른 페이즈와
// 병행**" 이라고 경고한 그 상황이다.
//
// 그래서 **동작을 「몽타주」가 아니라 「이름 붙은 행동 + 걸리는 시간」으로**
// 모델링한다. `PlayAction("Sit_Down")` 은 몽타주가 꽂혀 있으면 그것을 재생하고
// 그 길이를 돌려주고, **비어 있으면 그냥 기본 시간을 돌려준다.** 호출부(StateTree
// 태스크)는 어느 쪽인지 모른다 — 초를 받아 세기만 한다.
//
// → 애님 0종으로 **오늘 전체 루프를 돌려 볼 수 있고**, 나중에 6종이 조달되면
//   `Actions` 맵에 꽂는 것으로 끝난다. StateTree 그래프는 손대지 않는다.
//
// ⚠ 그 대신 **눈으로 볼 것은 미끄러지는 T 포즈**다. 「동작이 없다」와 「태스크가
//   안 돌았다」를 헷갈리지 말 것 — 후자는 로그에 남는다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CustomerCharacter.generated.h"

class UAnimMontage;
class UStaticMeshComponent;

/** 한 행동의 변형 하나 */
USTRUCT(BlueprintType)
struct FCustomerActionVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	TObjectPtr<UAnimMontage> Montage;

	/**
	 *	뽑힐 가중치. 합이 1일 필요는 없다 — 쓸 때 정규화한다.
	 *
	 *	기본 동작(가만히 서 있기)을 1.0, 가끔 나오는 몸짓을 0.2 식으로 주면
	 *	**대개 평범하고 가끔 눈에 띄는** 그림이 된다. 전부 1.0 이면 균등이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/**
	 *	이 몽타주가 도는 **동안** 손에 폰을 보인다.
	 *
	 *	⚠ 「행동이 끝나면 숨긴다」가 아니라 **몽타주가 도는 동안만**이다. 대기
	 *	  상태는 몽타주가 끝나도 `Order.Ready` 이벤트를 기다리며 남아 있어서,
	 *	  행동 종료로 판단하면 폰이 빈손에 떠 있게 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	bool bShowPhone = false;
};

/**
 *	한 행동 이름에 딸린 변형들.
 *
 *	⚠ **변형이 하나뿐이어도 된다.** 그때는 늘 그것만 나온다 — 호출부는 개수를
 *	  구별하지 않는다.
 */
USTRUCT(BlueprintType)
struct FCustomerAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	TArray<FCustomerActionVariant> Variants;
};

// /**
//  *	매장 손님.
//  *
//  *	⚠ **주문 번호를 여기 두는 것은 캐시일 뿐이다.** 진실은 `UStoreSubsystem` 이
//  *	  들고 있다 (§9.1 — "주문 번호는 매장의 상태지 손님의 상태가 아니다").
//  *	  여기 값은 "내가 어느 번호를 기다리는가" 를 태스크가 빨리 보려는 것뿐이고,
//  *	  발급·수령 판정은 전부 서브시스템이 한다.
//  */
UCLASS()
class ACustomerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACustomerCharacter();

	// ---------------------------------------------------------------- 일행
	/**
	 *	같이 온 일행의 번호. **0 이면 혼자.**
	 *
	 *	한 번에 여럿을 내보내면 그 전부에게 같은 번호를 준다. 그 번호로
	 *	**같은 테이블**에 앉는다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Party")
	int32 PartyId = 0;

	/** 일행 인원. 혼자면 1. 테이블을 고를 때 「몇 자리 필요한가」로 쓰인다. */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Party")
	int32 PartySize = 1;

	/**
	 *	**스폰한 쪽이 스폰 직후 불러 준다.** 일행으로 등록하고 시작 역할을 적용한다.
	 *
	 *	⚠ 원래는 스포너가 인터페이스(`ISpawnGroupMember`)로 부르던 것이다. 이식하며
	 *	  스포너를 안 가져왔으므로 **부르는 쪽이 없다** — 손님을 낳는 코드에서
	 *	  직접 불러야 일행·프리필이 동작한다. 안 부르면 전원이 「문으로 막 들어온
	 *	  혼자 온 손님」이 된다 (조용히 그렇게 된다).
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	void OnSpawnedInGroup(int32 GroupId, int32 GroupSize, int32 IndexInGroup);

	// ------------------------------------------------------------ 시작 역할
	/**
	 *	레벨이 시작될 때 **이미 하고 있던 일.** `None` 이면 문으로 들어온 손님이다.
	 *
	 *	`Eating` · `Waiting` · `Ordering` — 낳는 쪽이 `OnAssignedSpawnRole` 로 정해 준다.
	 *
	 *	## 어떻게 쓰이나
	 *
	 *	`ST_Customer` 의 **Root 자식들이 이 값으로 갈린다** — 조건
	 *	`Start Role Is` 를 각 갈래에 걸고, 마지막 갈래는 조건 없이 둔다. StateTree
	 *	는 Root 에 들어갈 때 자식을 **위에서부터** 훑어 조건이 맞는 첫 갈래를
	 *	고르므로, 이것만으로 「중간부터 시작」이 된다.
	 *
	 *	⚠ **한 무리는 한 역할이다.** 일행 둘이 하나는 먹고 하나는 줄 서 있으면
	 *	  일행이 아니다. 역할은 스포너가 무리 단위로 뽑는다.
	 *
	 *	⚠ 값을 트리에서 **소비해야 한다** (`Consume Start Role`). 안 그러면
	 *	  「주문을 잃어 처음으로 되돌아가는」 전이가 걸렸을 때 프리필 갈래로 다시
	 *	  들어가 무한히 먹는다. 그 태스크가 이 값을 `None` 으로 되돌린다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Spawn")
	FName StartRole;

	/**
	 *	시작 역할을 정해 준다. **프리필 손님에게만 부른다.**
	 *
	 *	⚠ **`OnSpawnedInGroup` 보다 먼저 불러야 한다.** 좌석 배정이 `PartyId` 를
	 *	  보기 때문이다 — 순서가 뒤집히면 일행이 같은 테이블에 안 앉는다.
	 *
	 *	⚠ 인자 이름이 `Role` 이 아니라 `InRole` 인 이유 — **`AActor::Role` 이
	 *	  이미 있다** (`TEnumAsByte<ENetRole>`). 그대로 쓰면 C4458 로 빌드가 깨진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	void OnAssignedSpawnRole(FName InRole);

	/** 역할을 읽고 지운다. 트리의 `Consume Start Role` 이 부른다. */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	FName ConsumeStartRole();

	/**
	 *	시작 역할대로 **지금 당장** 자리를 잡고 자세를 만든다.
	 *
	 *	스폰 직후(`OnSpawnedInGroup` 끝)에 불린다 — **StateTree 가 첫 틱을 돌기
	 *	전이다.** 그래서 PIE 를 켠 첫 프레임부터 손님이 이미 앉아 있다.
	 *
	 *	## 왜 트리에 맡기지 않는가
	 *
	 *	트리도 `Claim Slot(Teleport On Enter)` 로 같은 일을 할 수 있지만 그것은
	 *	**첫 틱**에 일어나고, 무엇보다 **12개 상태가 정확히 배선돼 있어야** 한다.
	 *	배선이 하나라도 틀리면 전원이 기본 갈래로 떨어져 문 앞에서 걸어 나온다 —
	 *	조용히 틀리는 실패다. 자리 잡기는 C++ 이 **먼저** 해 두는 편이 확실하다.
	 *
	 *	## ⚠ 그래도 트리의 프리필 갈래는 여전히 필요하다
	 *
	 *	여기서 앉혀 놔도 트리가 `Enter → Kiosk` 로 가면 **일어나 걸어가려 한다**
	 *	(`SitAt` 이 `MOVE_None` 을 걸어 두므로 실제로는 못 걷고 자리에서 버둥댄다).
	 *	트리가 역할에 맞는 갈래로 시작해야 그림이 완성된다 (`ST_Customer` 의
	 *	`P_Eating` · `P_Waiting` · `P_Ordering` 갈래).
	 *
	 *	⚠ `UStoreSubsystem::ClaimSlot` 이 **멱등**이라 트리의 `Claim Slot` 이
	 *	  같은 종류를 또 잡아도 **여기서 잡은 그 자리**가 돌아온다. 그래서 둘이
	 *	  싸우지 않고, `Sit At Seat` 의 `ExitState` 가 일으켜 세우는 경로도 살아 있다.
	 *
	 *	@return 자리를 잡고 자세까지 만들었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	bool ApplyStartRole();

	/**
	 *	그 자리로 **순간이동**한다. 프리필 손님을 「이미 거기 있던 것」으로
	 *	만들기 위한 것이고, 그 외에는 쓰지 않는다.
	 *
	 *	⚠ 발밑 좌표를 준다 — 캡슐 반높이는 여기서 더한다 (`SitAt` 과 같은 규약).
	 *	⚠ 내비메시에 투영한다. 안 하면 프리필 손님이 벽 안에서 시작해 **영영 못
	 *	  움직인다** — 증상은 「가만히 서 있다」라 애님 문제로 오진하기 쉽다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	void TeleportToPoint(const FVector& Location, const FRotator& Rotation);

	// ---------------------------------------------------------------- 주문
	/** 내가 기다리는 번호. 0 이면 없음. **판정 근거로 쓰지 말 것** — 위 주석 참조. */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Customer")
	int32 OrderNumber = 0;

	// ---------------------------------------------------------------- 행동
	/**
	 *	행동 이름 → **변형들**. **비워 둬도 된다** (위 주석).
	 *
	 *	§9.4 의 6종 — `Interact_Reach` · `Sit_Down` · `Sit_Idle` · `Sit_Eat` ·
	 *	`Stand_Up` · `Idle_Look`.
	 *
	 *	## 왜 하나가 아니라 배열인가
	 *
	 *	같은 애님만 반복하면 손님 여덟이 **같은 몸짓을 같이** 한다. 특히
	 *	`Idle_Look` 은 대기열에서 가장 오래 보이는 동작이라 티가 크다.
	 *
	 *	`Play Action` 이 **루프·반복마다 새로 뽑으므로**(`bLoop` 나
	 *	`RandomExtraRepeats`) 한 번 서 있는 동안에도 몸짓이 바뀐다.
	 *
	 *	⚠ **`Sit_Down`·`Stand_Up` 은 변형을 늘리지 말 것.** 그 둘은 끝/시작
	 *	  포즈가 `Sit_Idle`·`Sit_Eat` 과 맞아야 하는데, 변형이 늘면 맞춰야 할
	 *	  조합이 곱으로 늘어난다. 하나면 충분하다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action")
	TMap<FName, FCustomerAction> Actions;

	/**
	 *	같은 변형이 **연속으로** 나오지 않게 한다.
	 *
	 *	⚠ 이게 없으면 변형을 셋 넣어도 확률적으로 같은 게 두세 번 이어져
	 *	  "바리에이션이 안 먹는다" 로 보인다. 변형이 하나뿐이면 자동으로 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action")
	bool bAvoidRepeatVariant = true;

	/**
	 *	행동을 시작하고 **걸리는 시간(초)** 을 돌려준다.
	 *
	 *	몽타주가 있으면 재생 길이 / 없으면 `FallbackSeconds`. 호출부는 구별하지
	 *	않는다 — 경로가 하나여야 디버깅 경로도 하나다 (§9.1 의 "BP 태스크로 가지
	 *	말 것" 과 같은 이유).
	 *
	 *	@param ForceVariant `-1` 이면 평소대로 가중 무작위로 뽑는다. 그 외에는
	 *	                    **그 번호의 변형**을 쓴다 (장식 손님의 고정 몸짓용).
	 *	@param PlayRate     재생 속도. 2 면 두 배 빠르다.
	 *
	 *	⚠ 돌려주는 시간은 **속도를 나눈 실제 재생 시간**이다. `Montage_Play` 의
	 *	  반환값은 원본 길이라 그대로 쓰면 호출부가 두 배를 기다린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Action")
	float PlayAction(FName ActionName, float FallbackSeconds = 2.f, int32 ForceVariant = -1,
		float PlayRate = 1.f);

	/** 재생 중인 행동을 끊는다 (상태 이탈 시) */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Action")
	void StopAction();

	// ------------------------------------------------------- 장식 손님(프리필)
	/**
	 *	**StateTree 를 안 태우는 손님.** 스포너가 프리필로 낳을 때 켜 준다.
	 *
	 *	장식 손님은 AI 컨트롤러를 안 받으므로 트리가 시작조차 하지 않는다 —
	 *	`Move To` · `Leave` · `Despawn` 이 구조적으로 불가능하다. 대신 아래
	 *	`StartActionLoop` 이 역할에 맞는 몸짓을 계속 돌린다.
	 *
	 *	⚠ 장식 손님도 **슬롯은 예약한다.** 안 그러면 걸어 들어온 손님이 같은
	 *	  의자에 겹쳐 앉는다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BurgerHouse|Spawn")
	bool bDecoration = false;

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Spawn")
	void SetDecoration(bool bIn) { bDecoration = bIn; }

	/**
	 *	역할별로 장식 손님이 반복할 행동 이름.
	 *
	 *	⚠ 여기 이름이 `Actions` 맵의 키와 **철자까지 같아야** 한다. 틀리면
	 *	  `PlayAction` 이 몽타주를 못 찾아 시간만 세고, 손님이 **가만히 서 있는
	 *	  것처럼** 보인다 — 조용히 실패한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action")
	TMap<FName, FName> DecorationActions = {
		{ TEXT("Eating"),   TEXT("Sit_Idle")  },
		{ TEXT("Waiting"),  TEXT("Idle_Look") },
		{ TEXT("Ordering"), TEXT("Idle_Look") },
	};

	/**
	 *	**첫 재생을 늦추는** 무작위 시간(초). `0 ~ 이 값` 에서 뽑는다.
	 *
	 *	⚠ 이것은 루프 **사이의 쉬는 시간이 아니다.** 한 번만 쓰인다.
	 *
	 *	## 왜 필요한가 — 군무 방지
	 *
	 *	장식 손님들은 같은 프레임에 태어난다. 안 흩으면 8초마다 **전원이 동시에
	 *	같은 동작**을 해서 군무처럼 보인다. 첫 재생을 한 번 어긋내 두면 그 위상
	 *	차이가 계속 유지된다.
	 *
	 *	⚠ 이 시간 동안 손님은 AnimBP 의 앉은 Idle 자세로 있는다. 너무 크게 주면
	 *	  가게가 열리고 한참 아무도 안 먹는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action",
		meta = (Units = "s", ClampMin = "0.0"))
	float DecorationGap = 3.0f;

	/**
	 *	루프를 이을 때 **앞 몽타주와 겹치는 시간(초).**
	 *
	 *	## ⚠⚠ 이게 없으면 손님이 매 루프마다 버거를 내려놨다 다시 든다
	 *
	 *	몽타주를 끝까지 재생하면 `Auto Blend Out` 이 **기본 포즈(앉은 Idle)로
	 *	되돌린다.** 그 다음 재생이 거기서 다시 블렌드인하므로 —
	 *
	 *	    먹는 중 → (BlendOut) 앉은 Idle → (BlendIn) 먹는 중
	 *
	 *	끝나기 전에 겹쳐 걸면 기본 포즈를 안 거치고 **끝 자세에서 시작 자세로
	 *	직접** 넘어간다.
	 *
	 *	⚠ 증상이 「애님 첫·끝 프레임이 안 맞는다」와 똑같아 보인다. 그래서 애님을
	 *	  자르려 들기 쉬운데, 먹는 애님은 **순환 동작이 아니라** 시작 자세로
	 *	  돌아오는 지점이 아예 없다 — 잘라도 안 고쳐진다.
	 *
	 *	⚠ 몽타주의 `Blend In` 과 **비슷하게** 둘 것. 크로스페이드 길이는 들어오는
	 *	  몽타주의 Blend In 이 정하므로, 이 값만 키우면 겹치는 동안 앞 몽타주가
	 *	  그냥 계속 재생될 뿐이다. 현재 몽타주는 전부 0.35 다.
	 *
	 *	⚠ 0 이면 예전 동작(기본 포즈를 거침)으로 돌아간다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action",
		meta = (Units = "s", ClampMin = "0.0"))
	float DecorationLoopCrossfade = 0.35f;

	/**
	 *	장식 손님이 **한 가지 몸짓만** 계속 반복하게 한다.
	 *
	 *	## 왜 기본이 켬인가
	 *
	 *	끄면 반복할 때마다 변형을 새로 뽑아 손님 하나가 「버거 → 폰 → 감자튀김」
	 *	으로 계속 바뀐다. 앉아서 밥 먹는 사람은 그러지 않는다. 다양성은 **한 사람
	 *	안에서**가 아니라 **손님들 사이에서** 나와야 한다.
	 *
	 *	⚠ 켜면 이 손님에게 `bAvoidRepeatVariant` 는 의미가 없다 (늘 같은 것을
	 *	  고르는 것이 목적이므로).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action")
	bool bDecorationSingleAction = true;

	/**
	 *	장식 손님의 몸짓을 고르는 **씨앗.**
	 *
	 *	## 왜 「NPC 마다 랜덤」이 아니라 **자리 기반**인가
	 *
	 *	몸짓은 **좌석 번호**(`ASlotPoint::SourceIndex`)와 이 값에서 결정적으로
	 *	뽑는다. 그래서 —
	 *
	 *	  · 같은 의자는 **늘 같은 몸짓**이다. PIE 를 다시 켜도 그림이 같아
	 *	    스크린샷·리뷰가 재현된다.
	 *	  · 해시로 흩어 두므로 **옆자리끼리 안 겹친다.** 좌석 번호를 그대로
	 *	    나머지 연산하면 「버거·감자·버거·감자」가 줄줄이 나온다.
	 *	  · 그림이 마음에 안 들면 **이 값 하나만** 바꿔 전체를 다시 섞는다.
	 *
	 *	⚠ NPC 마다 `InitialSeed` 를 주는 방법도 있다. 그런데 프리필 손님은 PIE
	 *	  마다 새로 태어나므로 씨앗을 **스포너의 배치 목록에 저장**해야 재현되고,
	 *	  자리를 옮기면 씨앗도 같이 옮겨야 한다. 자리에서 뽑으면 저장할 것이 없고
	 *	  레벨만 보면 결과를 알 수 있다.
	 *
	 *	⚠ 가중치(`FCustomerActionVariant::Weight`)는 **그대로 존중된다** —
	 *	  뽑는 방식은 같고 난수 자리에 결정적인 값이 들어갈 뿐이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Action")
	int32 DecorationSeed = 0;

	/**
	 *	`ActionName` 을 **끝없이 반복한다.**
	 *
	 *	@param FixedVariant `INDEX_NONE` 이면 반복할 때마다 새로 뽑는다.
	 *	                    그 외에는 **그 변형 하나만** 계속 돌린다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Action")
	void StartActionLoop(FName ActionName, int32 FixedVariant = -1);

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Action")
	void StopActionLoop();

	// ---------------------------------------------------------------- 착석
	/**
	 *	좌석 지점에 **스냅**한다.
	 *
	 *	§9.5 "착석 정렬 — 1차는 스냅, 그림이 잡히면 Motion Warping". MoveTo 는
	 *	좌석 근처까지만 데려오므로 몇 cm·몇 도를 여기서 맞춘다.
	 *
	 *	⚠ 콜리전은 **켠 채로 둔다.** `MOVE_None` 이면 무브먼트가 캡슐을 안 밀기
	 *	  때문에 의자를 뚫지 않고, 켜 두면 다른 손님이 앉은 사람을 통과하지 않는다.
	 *
	 *	## ⚠ 높이는 **애님이 정한다**
	 *
	 *	앉는 애님은 골반을 좌면 높이로 올려 놓는다. 그래서 **캡슐은 바닥에 둬야**
	 *	하고, 좌석 지점(`ASlotPoint::LocalOffset`)의 Z 도 **0** 이어야 한다.
	 *
	 *	좌면 높이 45를 `LocalOffset.Z` 에 넣으면 캡슐이 45cm 올라가고, 그 위에서
	 *	애님이 골반을 또 45 올려 **90cm 에 앉은 그림**이 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Seat")
	void SitAt(const FTransform& SeatTransform);

	/**
	 *	착석 높이 미세 조정(cm). 애님을 보고 눈으로 맞추는 값.
	 *
	 *	애님의 골반 높이와 실제 좌면 높이가 조금 다를 때 쓴다. 0 이면 애님이
	 *	정한 그대로.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Seat")
	float SitZAdjust = 0.f;

	/**
	 *	좌석으로 **미끄러져 들어가는** 시간(초). 0 이면 예전처럼 즉시 스냅한다.
	 *
	 *	## ⚠⚠ 왜 필요한가 — 「자리로 순간이동한다」의 정체
	 *
	 *	`SitAt` 은 좌석 트랜스폼으로 **스냅**한다(`TeleportPhysics`). 걸어서
	 *	도착한 지점과 좌석 사이가 아무리 가까워도 그 거리는 **한 프레임에**
	 *	사라진다 — 그게 순간이동으로 보인다.
	 *
	 *	`MoveTo` 의 도착 판정을 조여 그 거리를 39cm → 5cm 로 줄였지만(§9-9),
	 *	0 으로는 못 만든다. 남은 거리를 **시간에 걸쳐** 없애는 것이 이 값이다.
	 *
	 *	⚠ 미끄러지는 동안 이동 모드는 이미 `MOVE_None` 이라 캡슐이 의자를 밀지
	 *	  않는다. 그래서 위치를 직접 써도 안전하다.
	 *
	 *	⚠ 너무 길면 앉는 애님이 먼저 끝나 **공중에서 앉은 자세로 미끄러진다.**
	 *	  0.3~0.4 근처가 눈에 안 띈다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Seat", meta = (ClampMin = "0.0"))
	float SeatBlendTime = 0.5f;

	/**
	 *	한 틱에 이만큼(cm) 넘게 움직이면 **경고를 찍는다.** 0 이면 안 본다.
	 *
	 *	## ⚠⚠ 이것이 있어야 「순간이동한다」를 고칠 수 있다
	 *
	 *	위치를 직접 쓰는 곳이 여럿이고(`TeleportToPoint` · `SitAt` · `StandUp` ·
	 *	좌석 보간) 어느 것이 범인인지 화면만 봐서는 못 가른다. 실제로 두 번
	 *	엉뚱한 곳을 고쳤다.
	 *
	 *	⚠ 틱 간격이 1/30 이라 350cm/s 로 **걸어도** 한 틱에 12cm 는 움직인다.
	 *	  기본값 40 은 그 세 배 — 걷기로는 안 찍히고 스냅만 찍히는 값이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Debug", meta = (ClampMin = "0.0"))
	float TeleportWarnDistance = 40.f;

	/**
	 *	착석 순간 좌석까지 이만큼(cm) 넘게 남아 있으면 **경고를 찍는다.**
	 *
	 *	⚠ 0 이 될 수 없다. 좌석 지점은 의자 위(내비 구멍)라 손님은 거기 못 선다 —
	 *	  내비로 투영한 자리까지만 걸어오고, 실측으로 **56~117cm** 가 남는다. 남는
	 *	  거리는 `SeatBlendTime` 이 미끄러져 메운다.
	 *
	 *	⚠ 그래서 기본값은 **투영 거리보다 넉넉히** 잡는다. 이 값을 넘으면 그때는
	 *	  진짜로 이동이 안 일어난 것이다 (상태가 건너뛰어졌거나 경로가 막혔거나).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Debug", meta = (ClampMin = "0.0"))
	float SeatWarnDistance = 150.f;

	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Seat")
	void StandUp();

	UFUNCTION(BlueprintPure, Category = "BurgerHouse|Seat")
	bool IsSitting() const { return bSitting; }

	// ---------------------------------------------------------------- 트레이
	/** 손에 든다 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Tray")
	void HoldTray();

	/**
	 *	테이블에 내려놓는다.
	 *
	 *	§9.5 "트레이를 테이블에 Attach ★손에 든 채 안 먹는다".
	 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Tray")
	void PlaceTrayAt(const FTransform& Where);

	/** 반납 — 감춘다 */
	UFUNCTION(BlueprintCallable, Category = "BurgerHouse|Tray")
	void DropTray();

	UFUNCTION(BlueprintPure, Category = "BurgerHouse|Tray")
	bool HasTray() const { return bHasTray; }

	/**
	 *	트레이를 붙일 지점. **소켓 이름 또는 본 이름.**
	 *
	 *	⚠ 소켓을 안 만들어도 된다 — `USkinnedMeshComponent::GetSocketTransform` 이
	 *	  소켓을 못 찾으면 **`GetBoneIndex(InSocketName)` 으로 폴백**한다
	 *	  (엔진 소스 확인). 그래서 본 이름을 그대로 줘도 붙는다.
	 *
	 *	⚠ 본에 직접 붙이면 **손목 원점·손목 회전** 기준이라 트레이가 어긋나 보인다.
	 *	  아래 `TrayHandOffset` 으로 맞춘다. 정밀하게 하려면 스켈레톤에 소켓을
	 *	  만들고 그 이름을 넣는 편이 낫다 (에디터 작업).
	 *
	 *	⚠ 둘 다 못 찾으면 **컴포넌트 원점(발밑)에 붙는다** — 안 죽지만 티가 난다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Tray")
	FName TraySocket = TEXT("mixamorig:RightHand");

	/** 손에 들 때의 보정 (붙인 지점 기준 로컬). 본에 직접 붙일 때 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Tray")
	FTransform TrayHandOffset = FTransform::Identity;

	/**
	 *	좌석 지점 기준 트레이 놓는 자리 (앞·위로).
	 *
	 *	## ⚠⚠ Z 는 **테이블 상판 높이**다 — 좌면 높이가 아니다
	 *
	 *	좌석 지점의 Z 는 **바닥(0)** 이다 (`SitAt` 주석). 그래서 이 Z 가 곧 상판
	 *	높이가 된다.
	 *
	 *	실측: `Table_A` 는 높이 70cm(원점 z 35 · 반높이 35) 라 상판이 **z = 70**.
	 *	예전 기본값 30 은 상판보다 **40cm 아래** — 트레이가 테이블 **안에** 있었다.
	 *
	 *	⚠ 트레이 메시는 원점이 바닥이라(`Props/Tray` 는 z 0~2.2) 상판 높이를 그대로
	 *	  주면 상판 **위에** 얹힌다. 반높이를 더하지 말 것.
	 *
	 *	⚠ X 45 는 앉은 사람 **앞**으로 45cm 다. 방향은 좌석의 사용 회전을 따르므로
	 *	  의자마다 알아서 맞는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Tray")
	FVector TrayTableOffset = FVector(45.f, 0.f, 70.f);

	/**
	 *	놓을 자리 이 거리(cm) 안에 **트레이 액터**가 이미 있으면 내 것은 안 놓는다.
	 *
	 *	마주보고 앉은 손님 둘이 각자 놓으면 테이블 한가운데서 겹치기 때문이다
	 *	(실측 6.5cm). 그 자리에 놓인 것이 2인용 세트다 (`ACounterTray::bTwoPersonSet`).
	 *
	 *	⚠⚠ 옆 사람의 트레이까지 잡으면 **그 사람 트레이가 통째로 사라진다.** 실측 —
	 *
	 *	```
	 *	짝을 이룬 둘        공용 트레이까지 3 · 12cm
	 *	혼자 앉은 사람      38cm      <- 여기를 물면 안 된다
	 *	```
	 *
	 *	⚠ 40 으로 뒀다가 실제로 혼자 앉은 손님이 트레이를 잃었다 (벽쪽 좌석을
	 *	  30cm 옮기면서 공용 트레이가 그쪽으로 다가갔기 때문). **25** 면 양쪽에
	 *	  13cm 여유가 남는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Tray", meta = (ClampMin = "0.0"))
	float SharedTrayRadius = 25.f;

	/** 트레이 메쉬 — 액터를 새로 스폰하지 않는다 (스폰·파괴 경로를 안 늘리려고) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Tray;

	/**
	 *	지금 폰을 요구하는 몽타주. 이것이 **재생 중일 때만** 폰이 보인다.
	 *
	 *	⚠ 널이면 숨긴다. 몽타주가 끝나면 `Tick` 이 알아서 비운다.
	 */
	TObjectPtr<UAnimMontage> PhoneMontage;

	/** 도약 검출용 — 지난 틱의 위치 (`TeleportWarnDistance`) */
	FVector LastTickLocation = FVector::ZeroVector;
	bool bHasLastTickLocation = false;

	/**
	 *	마지막으로 앉은 좌석의 트랜스폼.
	 *
	 *	⚠ `PlaceTrayAt` 이 빈 트랜스폼을 받았을 때 대신 쓴다 — 트리의
	 *	  `TableLocation` 바인딩이 비어 있으면 트레이가 **월드 원점**으로 날아간다.
	 */
	FTransform SitSeatTransform;

	/**
	 *	앉기가 끝나면 트레이를 테이블에 놓아야 하는가.
	 *
	 *	⚠⚠ `S_Eat` 의 `Tray[PlaceOnTable]` 은 상태에 **들어가는 순간** 돈다. 그런데
	 *	  `Sit At Seat` 는 이제 좌석까지 **걸어간 뒤** 앉는다 — 그대로 놓으면 손님이
	 *	  아직 걸어오는 중에 트레이가 테이블에 떨어지고, 손님은 빈손으로 걸어온다.
	 *	  그래서 **앉을 때까지 미룬다.**
	 */
	bool bPendingTrayOnTable = false;

	/** 좌석 미끄러짐 — 남은 시간이 0 보다 크면 `Tick` 이 보간한다 (`SeatBlendTime`) */
	FTransform SeatBlendFrom;
	FTransform SeatBlendTo;
	float SeatBlendLeft = 0.f;

	/** 미끄러짐이 끝난 뒤 `MOVE_Walking` 으로 되돌릴 것인가 (`StandUp` 경로) */
	bool bSeatBlendRestoreWalking = false;

	/** 좌석/기립 미끄러짐을 시작한다. `SeatBlendTime` 이 0 이면 그 자리에서 스냅. */
	void BeginSeatBlend(const FTransform& To, bool bRestoreWalkingWhenDone);

	// ──────────────── 트레이에 얹히는 것들
	//
	// ## ⚠ 전부 `Tray` 의 **자식**이다
	//
	// 그래서 `HoldTray` · `PlaceTrayAt` · `DropTray` 가 트레이를 옮기면 **같이
	// 따라간다.** 손에 붙이는 코드도, 테이블에 놓는 코드도 손댈 필요가 없다.
	//
	// ⚠ 숨김도 `Tray` 에서 내려온다 (`SetHiddenInGame(true, /*bPropagate=*/true)`).
	//   자식마다 따로 숨기면 한 군데를 빠뜨렸을 때 **음식만 공중에 뜬 채로** 보인다.
	//
	// ⚠ 콜리전은 **전부 끈다.** 켜 두면 손에 든 음식이 손님 캡슐을 밀거나
	//   내비를 막는다 — 증상이 「손님이 이상하게 걷는다」로 보여 원인 찾기가 고약하다.

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BurgerBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Fries;

	/** 영수증 — 평평한 흰 종이. 엔진 기본 `Plane` 을 얇게 눕혀 쓴다. */
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
	 *	귀에 대는 휴대폰 — `Talking_On_A_Cell_Phone` 이 도는 동안만 보인다.
	 *
	 *	⚠ 실측: 그 애님에서 귀로 가는 것은 **오른손**이다 (`hand_r` 가 머리까지
	 *	  26.5cm, 왼손은 40.5cm). 그래서 `PhoneSocket` 기본값이 `hand_r` 다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Phone;

	/** 폰을 붙일 본. ⚠ 못 찾으면 컴포넌트 원점(발밑)으로 폴백한다 (§9-20). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Prop")
	FName PhoneSocket = TEXT("hand_r");

	/** 손 기준 보정. 눈으로 맞추는 값. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Prop")
	FTransform PhoneHandOffset = FTransform::Identity;


	// ──────────────── 로코모션을 몽타주로 (`Import/PLAN_Montage_Locomotion.md`)

	/**
	 *	걷기·서기·앉기를 **몽타주로** 돌린다 (`ABP_Customer` 의 `LocoSlot`).
	 *
	 *	## 끄면 어떻게 되나 — **상태 기계로 되돌아간다**
	 *
	 *	`LocoSlot` 에 아무것도 안 걸리면 슬롯은 Source 를 그대로 통과시킨다. 그
	 *	Source 가 상태 기계다. 즉 이 스위치 하나로 예전 그림으로 돌아간다 —
	 *	그래서 상태 기계를 **안 지우고 남겨 뒀다.**
	 *
	 *	⚠ 끄면 틱도 같이 꺼진다 (`BeginPlay`).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	bool bMontageLocomotion = true;

	/**
	 *	상태별 행동 이름. `Actions` 맵의 키와 **철자까지 같아야** 한다.
	 *
	 *	⚠ 틀리면 `PlayAction` 이 몽타주를 못 찾아 시간만 세고 슬롯이 빈 채로
	 *	  남는다 → 상태 기계 포즈가 그대로 나와서 **「바꿨는데 아무것도 안
	 *	  변한다」** 로 보인다. 조용히 실패하는 쪽이다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	FName LocoIdleAction = TEXT("Loco_Idle");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	FName LocoWalkAction = TEXT("Loco_Walk");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	FName LocoSitAction = TEXT("Loco_Sit");

	/**
	 *	**트레이를 든 동안** 쓸 걷기/서기. 비우면 평소 것을 쓴다.
	 *
	 *	## ⚠ 양손으로 들려면 애님이 그렇게 생겨야 한다
	 *
	 *	트레이를 몸통에 붙이는 것만으로는 **손이 따라가지 않는다.** 손 위치는
	 *	애님이 정한다 — Mixamo 세트의 `Carrying` 이 두 손을 앞으로 모은 동작이라
	 *	그것을 쓴다.
	 *
	 *	⚠ 이 이름들이 `Actions` 맵에 **없으면** `PlayAction` 이 몽타주를 못 찾아
	 *	  시간만 세고, 슬롯이 비어 상태 기계 포즈가 나온다 — 「트레이는 들었는데
	 *	  팔이 그대로」로 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	FName LocoCarryWalkAction = TEXT("Loco_CarryWalk");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion")
	FName LocoCarryIdleAction = TEXT("Loco_CarryIdle");

	/**
	 *	걷기로 치는 속도(cm/s). **상태 기계의 전이 규칙과 같은 값**이라야 한다.
	 *
	 *	⚠ 실측: `ABP_Customer` 의 `Idle→Walk` 는 `Speed > 5`, 되돌아오는 쪽은
	 *	  `Speed < 5` 다. 문서가 한동안 **10** 이라고 적어 놨었는데 틀린 값이었다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion", meta = (ClampMin = "0.0"))
	float LocoWalkSpeed = 5.f;

	/**
	 *	임계값 주변의 **떨림 방지** 폭(cm/s).
	 *
	 *	## ⚠⚠ 이게 없으면 Idle↔Walk 가 초당 수십 번 뒤집힌다
	 *
	 *	상태 기계는 전이 블렌드가 그 떨림을 덮어 줬다. 몽타주는 바뀔 때마다 재생을
	 *	새로 걸기 때문에 **블렌드인이 계속 처음부터 다시 시작해 애님이 얼어붙는다.**
	 *	그래서 걷기 시작은 `LocoWalkSpeed + 이 값`, 멈춤은 `LocoWalkSpeed - 이 값`.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BurgerHouse|Locomotion", meta = (ClampMin = "0.0"))
	float LocoSpeedHysteresis = 1.f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 *	속도·착석 상태를 보고 `LocoSlot` 에 걸 몽타주를 바꾼다. 틱에서 부른다.
	 *
	 *	⚠ **바뀔 때만** 재생한다. 매 틱 걸면 블렌드인이 계속 처음부터 다시
	 *	  시작해서 애님이 얼어붙는다.
	 */
	void UpdateLocomotionMontage();

	/**
	 *	`Actions` 의 몽타주를 **끝없이** 재생한다.
	 *
	 *	## 루프를 거는 법 — 에셋이 아니라 런타임이다
	 *
	 *	몽타주는 기본이 1회 재생이다. 세그먼트의 `LoopingCount` 는 무한 스위치가
	 *	**아니다** (0 으로 두면 길이가 0 이 된다). 대신 재생 뒤에
	 *	`Montage_SetNextSection(섹션, 같은 섹션)` 으로 자기 자신을 가리키게 하면
	 *	끊김 없이 영원히 돈다.
	 *
	 *	⚠ 장식 루프(`PlayLoopedAction`)처럼 타이머로 다시 걸면 안 된다 — 걷기가
	 *	  1.067초라 분당 83회를 다시 걸어야 하고, 타이밍이 흔들리면 보폭이 튄다.
	 *
	 *	⚠ 섹션 이름은 **박아 넣지 않는다.** 몽타주에서 읽는다.
	 */
	bool PlayLoopingMontageAction(FName ActionName);

	/** `LocoSlot` 에 지금 걸려 있는 행동. 비어 있으면 아직 아무것도 안 걸었다. */
	FName CurrentLocoAction;

	/** 행동마다 **직전에 뽑은** 변형. `bAvoidRepeatVariant` 용. */
	TMap<FName, int32> LastVariant;

	/** 반복 재생 중인 행동. 비어 있으면 반복 안 함. */
	FName LoopingAction;

	/** 반복할 때 고정할 변형. `INDEX_NONE` 이면 매번 새로 뽑는다. */
	int32 LoopVariant = INDEX_NONE;

	FTimerHandle LoopTimer;

	/** 다음 몸짓을 재생하고 그 길이 + 쉬는 시간만큼 타이머를 다시 건다. */
	void PlayLoopedAction();

	/**
	 *	장식 손님이면 `StartRole` 에 맞는 행동을 반복 재생한다.
	 *
	 *	@param StableKey 몸짓을 결정적으로 고를 때 쓰는 값 — **좌석 번호**를
	 *	                 넣는다. `INDEX_NONE` 이면 무작위로 뽑는다.
	 */
	void StartDecorationLoop(int32 StableKey = INDEX_NONE);

	/** 가중 무작위로 변형 하나를 고른다. 없으면 INDEX_NONE. */
	int32 PickVariant(FName ActionName, const FCustomerAction& Action);

	/**
	 *	`StableKey` 에서 **결정적으로** 변형을 고른다 (`DecorationSeed` 반영).
	 *
	 *	가중치는 그대로 쓰고 난수 자리에만 해시를 넣는다 — 같은 자리는 늘 같은
	 *	결과, 옆자리는 다른 결과.
	 */
	int32 PickStableVariant(FName ActionName, int32 StableKey) const;

	bool bSitting = false;
	bool bHasTray = false;

	/** 앉기 전 위치 — 일어설 때 되돌린다 */
	FTransform PreSitTransform;
};
