#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "CustomerSpawner.generated.h"

/**
 * 스플라인을 따라 손님을 낳고 **개체 수를 유지한다.** 서버 전용(HasAuthority 가드).
 *
 * 낳은 손님에게 **일행 번호와 시작 역할을 통보한다** — 그래야 일행이 같은
 * 테이블에 앉고, 레벨이 열릴 때부터 앉아 있는 손님이 생긴다
 * (`ACustomerCharacter::OnAssignedSpawnRole` · `OnSpawnedInGroup`).
 *
 * ────────────────────────────────────────────────────────────────
 * ## 맵마다 다시 맞춰야 하는 것
 *
 * 이 액터는 여러 맵에 그대로 떨어뜨려 쓰는 물건이라 **기본값을 특정 맵에
 * 맞춰 두지 않았다** — 한 맵에서만 맞는 기본값은 나머지에서 조용히 틀린다.
 *
 *   ① `NavMeshBoundsVolume`  → 스플라인을 덮는가
 *   ② 규모 값                 → `SampleSpacing` · `LateralWidth`
 *   ③ 개체 수                 → `FixedDesiredAlive` 또는 `NPCsPer100m`
 *
 * ⚠ **②를 빠뜨리는 것이 가장 흔하다.** 기본값은 200m 야외 기준이라 실내(수 m)에
 *   그대로 쓰면 손님이 방을 가로질러 튀어 나간다. 버거 가게 홀(6×11m)은
 *   `SampleSpacing 40` · `LateralWidth 60` · `HardMaxAlive 12`.
 *
 * ## 두 가지 쓰임새
 *
 * **① 길게 흩뿌리기.** 인도·복도를 따라 스플라인을 깔고 `FixedDesiredAlive = 0`
 * 으로 두어 길이 공식을 살린다.
 *
 * **② 한 지점에서 계속 내보내기.** 문처럼 **한 곳**에서 등장시키되 개체 수만
 * 유지하고 싶을 때. 짧은 스플라인 하나를 놓는다. 버거 가게가 이 경우다.
 *
 * ⚠ 그때 `DesiredAlive = (Length / 10000) × NPCsPer100m` 공식이 무너진다 —
 *   길이 200cm 면 `Length/10000 = 0.02` 이라 8명을 얻으려고 `NPCsPer100m` 을
 *   400 으로 올려야 한다. 그래서 **`FixedDesiredAlive`** 가 있다 — 0 보다 크면
 *   길이 공식을 통째로 건너뛴다.
 *
 * ⚠⚠ **`NavMeshBoundsVolume` 이 스플라인을 덮어야 한다.** 안 덮으면 스폰이
 *   **0** 이고 「스포너가 고장났나」로 보인다. 내비는 커맨드릿에서 안 구워진다 —
 *   에디터에서 `Build > Build Paths`.
 */
/**
 *	프리필 역할 하나와 그 비율.
 *
 *	이름의 **뜻은 스포너가 모른다** — 받는 쪽(`ACustomerCharacter::OnAssignedSpawnRole`)
 *	이 정한다. `Eating` · `Waiting` · `Ordering` 셋을 쓴다.
 *
 *	⚠ 여기 적은 이름이 `ST_Customer` 의 조건 `Start Role Is` 와 **철자까지 같아야**
 *	  한다. 틀리면 조건이 전부 거짓이 되어 기본 갈래로 떨어진다 — 조용히 틀린다.
 */
USTRUCT(BlueprintType)
struct FSpawnRoleWeight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName Role;

	/** 합이 1일 필요는 없다 — 쓸 때 정규화한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0"))
	float Weight = 1.f;
};

/**
 *	프리필 **한 명**을 어디에 어떤 역할로 놓을지 — 뽑기 없이 그대로 쓴다.
 *
 *	## 왜 가중치 대신 목록인가
 *
 *	`PrefillRoles` 로 뽑으면 **자리 수를 고려하지 못한다.** 키오스크가 2대인데
 *	`Ordering` 이 3번 뽑히면 한 명은 역할을 잃고, `Eating` 이 0번 뽑히면
 *	**앉은 손님이 아무도 없다** (실제로 그렇게 나왔다 — 60% 짜리가 4번 연속
 *	안 나올 확률이 2.6% 인데 그게 걸렸다).
 *
 *	그림을 정하고 싶으면 뽑지 말고 **적어야 한다.**
 */
USTRUCT(BlueprintType)
struct FSpawnPlacement
{
	GENERATED_BODY()

	/** 놓을 자리. **발밑 기준** — 캡슐 반높이는 받는 쪽이 더한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FTransform Transform = FTransform::Identity;

	/** `Eating` · `Waiting` · `Ordering`. 비우면 평범한 손님으로 시작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName Role;

	/**
	 *	**같은 번호끼리 한 일행**이 된다. `-1` 이면 혼자.
	 *
	 *	같은 테이블 의자 넷에 넷을 앉히려면 넷 다 같은 번호를 준다 — 그러면
	 *	`GroupSize` 가 4로 통보되어 좌석 배정이 일행으로 처리된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	int32 PartyIndex = -1;
};

UCLASS()
class FEBRICULA_API ACustomerSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACustomerSpawner();

	/** 스플라인을 런타임에 바꿨을 때 호출. 그 외에는 부를 필요 없다. */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	void RebuildDistribution();

	// ⚠ 아래 설정은 전부 **public 이어야 한다.** 이 액터는 맵마다 파이썬으로
	//   깔고 값을 채우는 물건이라, protected 로 두면 스크립트에서 만질 수
	//   있는지가 리플렉션 구현에 달리게 된다. 설정이 곧 이 액터의 인터페이스다.

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawn")
	TObjectPtr<USplineComponent> Spline;

	/** 낳을 손님. **`ACustomerCharacter` 파생이어야** 일행·역할 통보가 먹는다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<APawn> NPCClass;

	/**
	 * 0 보다 크면 길이 기반 공식을 무시하고 이 값을 쓴다.
	 *
	 * **한 지점에서 내보낼 때 쓰는 값이다** (위 주석 ②). 짧은 스플라인에서는
	 * 길이 공식이 0 을 내므로 이걸 안 쓰면 스폰이 아예 안 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	int32 FixedDesiredAlive = 0;

	/**
	 * 스플라인 이산화 간격(cm).
	 *
	 * ⚠ **맵 규모에 맞출 것.** 실내(수 m)는 20~50, 야외(수십~수백 m)는 200 이상.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "10.0"))
	float SampleSpacing = 200.f;

	/** 스플라인 좌우 산포 폭(cm). 0이면 일렬로 서서 부자연스럽다. ⚠ 통로 폭의 절반을 넘기지 말 것. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0"))
	float LateralWidth = 300.f;

	/** 스플라인 100m당 목표 손님 수. `FixedDesiredAlive` 가 0일 때만 쓰인다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0.0"))
	float NPCsPer100m = 3.f;

	/** 계산 결과와 무관한 상한. ⚠ 좁은 실내라면 크게 낮출 것. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (ClampMin = "0"))
	int32 HardMaxAlive = 60;

	/**
	 *	스폰 시도 주기(초).
	 *
	 *	⚠ 실내 매장 기준 기본값이 **40초**다. 야외 인파에 쓰려면 훨씬 짧게.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "s", ClampMin = "0.05"))
	float SpawnInterval = 40.f;

	/**
	 *	주기를 매번 `±SpawnIntervalJitter` 만큼 흔든다 (초).
	 *
	 *	⚠ **고정 주기는 사람이 아니라 메트로놈으로 보인다.** 정확히 4초마다
	 *	  한 명씩 들어오는 가게는 없다. 값이 0 이면 안 흔든다.
	 *
	 *	⚠ 주기보다 크게 주지 말 것 — 최소 0.05초로 잘린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn", meta = (Units = "s", ClampMin = "0.0"))
	float SpawnIntervalJitter = 5.f;

	/**
	 *	한 번에 몇 명을 내보낼지 — **인원별 가중치**.
	 *
	 *	    [0] = 1명일 확률   [1] = 2명   [2] = 3명   ...
	 *
	 *	기본값 `{0.8, 0.2}` 은 **1명 80% · 2명 20%** 다.
	 *	원소를 더 넣으면 3명 이상도 나온다.
	 *
	 *	⚠ **합이 1일 필요는 없다.** 쓸 때 정규화하므로 `{6, 3, 1}` 도 같은 뜻이다.
	 *	⚠ 전부 0 이거나 배열이 비면 **1명**으로 친다.
	 *
	 *	## 왜 최소·최대가 아니라 가중치인가
	 *
	 *	`Min 1 / Max 3` 은 1·2·3이 **똑같이 1/3씩** 나온다. 실제 식당은 혼자 오는
	 *	손님이 압도적으로 많다. 균등 분포는 "가끔 몰린다" 가 아니라 "늘 몰린다" 로
	 *	보인다.
	 *
	 *	⚠ 크게 잡지 말 것 — 같은 지점 같은 프레임에 여럿이 생기면 공유 자원
	 *	  (키오스크 2대·픽업 2곳) 경합이 한꺼번에 터진다.
	 *
	 *	⚠ `DesiredAlive` 를 넘지 않게 알아서 잘린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TArray<float> SpawnBurstWeights = { 0.8f, 0.2f };

	/** 캡슐 half-height 보정. 스폰된 NPC가 바닥에 묻히면 올린다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float SpawnZOffset = 90.f;

	// ────────────────────────────────────────────────── 프리필 (시작부터 있던 것들)

	/**
	 * true면 BeginPlay에서 미리 채운다.
	 *
	 * ## 왜 필요한가 — **빈 가게에서 시작하지 않으려고**
	 *
	 * 이걸 끄면 레벨이 텅 빈 채로 열리고, `SpawnInterval` 이 40초라 **첫 손님을
	 * 보기까지 40초가 걸린다.** 그동안 화면에는 아무 일도 안 일어난다.
	 *
	 * 프리필로 태어나는 것들은 문이 아니라 **가게 안 제자리에서 시작한다** —
	 * `PrefillRoles` 가 「누가 무엇을 하는 중인가」를 정하고, 받는 쪽이 그 역할에
	 * 맞는 상태로 트리를 시작한다.
	 *
	 * ⚠ **전원이 같은 프레임에 생긴다.** 공유 자원(좌석·키오스크) 경합이 한꺼번에
	 *   터지므로, 처음 켤 때는 `PrefillCount` 를 2~3 으로 낮춰 두고 로그를 볼 것.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Prefill")
	bool bPrefillOnBeginPlay = false;

	/**
	 *	프리필로 채울 **인원 수**. `0` 이면 `DesiredAlive` 만큼.
	 *
	 *	⚠ `DesiredAlive` 를 넘지 않게 잘린다 — 프리필로 정원을 채워 버리면
	 *	  문으로 들어오는 손님이 **영영 안 생긴다.** 정원의 절반쯤이 적당하다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Prefill", meta = (ClampMin = "0"))
	int32 PrefillCount = 0;

	/**
	 *	프리필 손님에게 **AI 를 붙일 것인가.**
	 *
	 *	기본은 **끔** — 프리필은 가게를 채우는 **장식**이다. AI 컨트롤러를 안
	 *	붙이면 StateTree 가 시작조차 안 하므로 `Move To` · `Leave` · `Despawn` 이
	 *	구조적으로 불가능하고, 손님이 자리에 **영원히** 남는다. 대신
	 *	`ACustomerCharacter::DecorationActions` 의 몸짓을 계속 반복한다.
	 *
	 *	⚠ 켜면 프리필도 트리를 타서 먹고 나가는 한 바퀴를 돈다. 그때는
	 *	  `ST_Customer` 의 프리필 갈래(`P_Eating` 등)가 제대로 배선돼 있어야 한다.
	 *
	 *	⚠ 장식 손님도 **슬롯은 예약한다.** 그래서 걸어 들어온 손님이 그 의자에
	 *	  겹쳐 앉지 않는다. 대신 그만큼 자리가 영구히 준다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Prefill")
	bool bPrefillRunsAI = false;

	/**
	 *	프리필된 것들에게 나눠 줄 **역할과 비율**.
	 *
	 *	    Eating   3.0   ← 앉아서 먹는 중
	 *	    Waiting  1.0   ← 카운터 줄에 서 있는 중
	 *	    Ordering 1.0   ← 키오스크 앞
	 *
	 *	⚠ **역할은 무리 단위로 뽑는다.** 같이 온 일행 둘이 하나는 먹고 하나는
	 *	  줄을 서 있으면 일행이 아니다. 한 무리는 한 역할이다.
	 *
	 *	⚠ **비우면 프리필이 전부 역할 `None`** 이 되고, 그건 「방금 문으로 들어온
	 *	  손님」과 같다 — 문 앞에 여럿이 겹쳐 생긴다. 프리필을 켰으면 여기를 채울 것.
	 *
	 *	⚠ 여기 적은 이름이 트리의 조건(`Start Role Is`)과 **철자까지 같아야** 한다.
	 *	  틀리면 조건이 전부 거짓이 되어 기본 갈래로 떨어진다 — 조용히 틀린다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Prefill")
	TArray<FSpawnRoleWeight> PrefillRoles;

	/**
	 *	**정해진 자리 목록.** 비어 있지 않으면 위의 `PrefillCount` ·
	 *	`PrefillRoles` · 스플라인 샘플링 · 버스트를 **전부 무시하고** 이 목록
	 *	그대로 놓는다 — 한 항목에 한 명.
	 *
	 *	## 이것을 쓰는 이유
	 *
	 *	가중치 뽑기는 **자리 수를 모른다.** 키오스크 2대에 `Ordering` 3명이
	 *	뽑히거나 `Eating` 이 0명 뽑히는 일이 실제로 일어났다. 「테이블 셋에 앉은
	 *	손님 넷, 줄에 한 명」 같은 **정해진 그림**을 원하면 뽑지 말고 적어야 한다.
	 *
	 *	⚠ **좌표를 손으로 넣지 말 것.** 레벨의 `ASlotPoint` 를 읽어 채운다
	 *	  (`Import/Scripts/ll_spawner.py`). 손으로 넣으면 의자를 옮겼을 때
	 *	  손님이 공중에 앉는다.
	 *
	 *	⚠ `DesiredAlive` 를 넘는 항목은 **잘린다** — 넘겨 채우면 문으로 들어오는
	 *	  손님이 영영 안 생긴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Prefill")
	TArray<FSpawnPlacement> PrefillPlacements;

protected:
	virtual void BeginPlay() override;

private:
	void BuildDistribution();
	void TrySpawn();
	void Prefill();

	/** `PrefillPlacements` 그대로 놓는다. 뽑기 없음. */
	void PrefillFromPlacements();

	/**
	 *	한 명을 **지정한 자리에** 놓는다. 스플라인·내비 샘플링을 안 탄다.
	 *
	 *	⚠ **컨트롤러는 안 붙인다** — `StartControllers` 가 나중에 붙인다.
	 */
	APawn* SpawnAt(const FTransform& Where);

	/**
	 *	무리 전원에게 컨트롤러를 붙여 **AI 를 시작한다.**
	 *
	 *	⚠⚠ **반드시 역할·일행 통보 뒤에 부른다.** `SpawnDefaultController()` 가
	 *	  `OnPossess → StartLogic → Context.Start()` 로 **초기 상태 선택을 그
	 *	  자리에서 동기로 끝내기** 때문이다 (`StateTreeComponent.cpp:160`).
	 *	  통보보다 먼저 붙이면 트리가 **역할이 비어 있는 상태로** 갈래를 고르고,
	 *	  프리필 손님 전원이 기본 갈래로 떨어진다.
	 */
	void StartControllers(const TArray<APawn*>& Pawns);
	void RescheduleTimer();
	int32 PickBurstCount() const;
	FName PickPrefillRole() const;

	/**
	 *	한 무리를 통째로 내보낸다. 실제로 태어난 수를 돌려준다.
	 *
	 *	⚠ 인자가 `Role` 이 아니라 `InRole` 인 이유 — **`AActor::Role` 이 이미
	 *	  있다** (`TEnumAsByte<ENetRole>`). 가리면 C4458 로 빌드가 깨진다.
	 */
	/**
	 *	@param bAsDecoration true 면 AI 를 안 붙이고 장식으로 남긴다 (프리필용).
	 */
	int32 SpawnGroup(int32 Count, FName InRole, bool bAsDecoration = false);

	bool SampleLocation(FVector& OutLocation) const;
	APawn* SpawnOne();

	UFUNCTION()
	void HandleNPCDestroyed(AActor* DestroyedActor);

	/** 각 샘플의 스플라인 거리. */
	TArray<float> SampleDistances;

	/** 샘플별 누적합. 스플라인 위 균등 분포를 위한 이진 탐색 대상. */
	TArray<float> CDF;

	int32 DesiredAlive = 0;

	/** 다음 무리에 줄 번호. 1부터. */
	int32 NextGroupId = 1;

	FTimerHandle SpawnTimer;
	TArray<TWeakObjectPtr<APawn>> Alive;
};
