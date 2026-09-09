// Fill out your copyright notice in the Description page of Project Settings.
//
// 군중 전체를 한 곳에서 관리하는 감독.
//
// 이 클래스가 존재하는 이유는 단 하나, **비용을 한 군데로 모으기 위해서**다.
//   - NPC 100마리가 각자 "플레이어까지 거리"를 재면 매 프레임 100번 계산
//   - 여기서 한 번에 나눠 계산하면서 **분할 갱신(N분의 1씩)** 까지 하면 프레임당 몇 번으로 끝난다
//
// 담당:
//   1) 오브젝트 풀 - 메타휴먼을 런타임에 Spawn/Destroy하지 않는다
//   2) LOD 밴드 배정 - 거리 + 최근 렌더링 여부
//   3) 전역 정원 - 화면에 몇 명까지 살릴지 하드 캡
//   4) 회수 - 너무 멀어진 개체는 풀로 돌려보낸다

#pragma once

#include "CoreMinimal.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "SubNpcDirectorSubsystem.generated.h"

class ANpcCrowdPath;
class ASubNpcCharacter;
class UDataTable;

UCLASS()
class FEBRICULA_API USubNpcDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/**
	 * 풀을 미리 채운다. 스포너가 게임 시작 시 한 번 호출한다.
	 *
	 * 여기서 한꺼번에 만들어두는 이유: 메타휴먼은 스폰 비용이 커서 플레이 중에 만들면
	 * 그 프레임이 눈에 띄게 튄다. 로딩 중에 다 만들어놓고 재활용하는 게 유일한 답이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	void PrewarmPool(TSubclassOf<ASubNpcCharacter> NpcClass, int32 Count);

	/** 아키타입 테이블 등록 (FSubNpcArchetypeRow) */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	void SetArchetypeTable(UDataTable* InTable);

	/**
	 * 풀에서 하나 꺼내 경로 위에 세운다. 정원이 찼거나 풀이 비면 nullptr.
	 * 반환값이 null인 것은 정상 동작이다 (스포너가 다음 기회에 다시 시도한다).
	 *
	 * 밖에서 보면 평범한 스폰이다. 내부에서 새 액터를 만드는 대신 풀에서 꺼낼 뿐이라,
	 * 스포너를 만드는 쪽은 이 함수만 부르면 되고 풀의 존재를 몰라도 된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	ASubNpcCharacter* SpawnOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed = false);

	/**
	 * 여러 명을 한 무리로 등장시킨다. 첫 번째가 리더, 나머지는 뒤를 따른다.
	 * 레퍼런스 영상의 "커플/일행"이 이것이다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	TArray<ASubNpcCharacter*> SpawnGroupOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed, int32 GroupSize);

	/**
	 * 월드 좌표를 주면 가장 가까운 경로를 찾아 그 지점에 등장시킨다.
	 *
	 * "여기에 한 명 세워줘" 식으로 배치 위치 기준으로 스폰하는 쪽을 위한 입구다.
	 * 스포너를 만드는 사람이 어느 스플라인의 몇 cm 지점인지 계산할 필요가 없다.
	 *
	 * @param SearchRadius  이 반경 안에 경로가 없으면 nullptr (0 이하면 거리 제한 없음)
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	ASubNpcCharacter* SpawnAtNearestPath(const FVector& WorldLocation, float SearchRadius = 2000.f);

	/** 풀로 회수 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	void ReturnToPool(ASubNpcCharacter* Npc);

	/** 레벨에 직접 배치된 개체도 LOD 관리를 받도록 등록한다 */
	void RegisterNpc(ASubNpcCharacter* Npc);
	void UnregisterNpc(ASubNpcCharacter* Npc);

	UFUNCTION(BlueprintPure, Category = "Crowd|Director")
	int32 GetActiveCount() const { return ActiveNpcs.Num(); }

	UFUNCTION(BlueprintPure, Category = "Crowd|Director")
	int32 GetPooledCount() const { return PooledNpcs.Num(); }

	/** 동시에 살아있을 수 있는 최대 인원. 런타임에 낮추면 초과분이 순차 회수된다 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Director")
	void SetMaxActiveNpcs(int32 NewMax) { MaxActiveNpcs = FMath::Max(0, NewMax); }

	UFUNCTION(BlueprintPure, Category = "Crowd|Director")
	int32 GetMaxActiveNpcs() const { return MaxActiveNpcs; }

protected:
	/** 등록된 NPC를 N분할해서 프레임당 일부만 LOD 재계산한다 */
	void UpdateLodSlice();

	/**
	 * 가까운 개체끼리 좌우로 밀어낸다 (소프트 회피).
	 *
	 * 진로를 다시 계산하지 않고 차선 오프셋만 조정한다. 같은 경로에서 진행 거리가
	 * 비슷한 개체끼리만 비교하므로 전방위 이웃 탐색이 필요 없다.
	 * Near/Mid 대역만 대상으로 한다 - 멀리서 겹치는 건 어차피 화면에 안 보인다.
	 */
	void UpdateAvoidance();

	/** 경로 끝에 닿아 회수를 예약한 개체들을 안전한 시점에 처리한다 */
	void ProcessPendingRecycles();

	/** 아키타입 테이블에서 가중 랜덤으로 하나 고른다 */
	bool PickArchetype(FSubNpcArchetypeRow& OutRow) const;

	/** 플레이어 폰 위치 (없으면 false) */
	bool GetViewerLocation(FVector& OutLocation) const;

	// ---- 설정 (프로젝트 세팅으로 빼도 되지만, 우선 기본값으로 시작) ----

	/** 동시 활성 인원 상한. 메타휴먼 기준으로는 이 값이 곧 메모리·GPU 예산이다 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Director", meta = (ClampMin = "0"))
	int32 MaxActiveNpcs = 40;

	/** LOD 경계 거리 (cm) */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD")
	float NearDistance = 1500.f;

	UPROPERTY(EditAnywhere, Category = "Crowd|LOD")
	float MidDistance = 4000.f;

	UPROPERTY(EditAnywhere, Category = "Crowd|LOD")
	float FarDistance = 9000.f;

	/** 이 거리를 넘으면 풀로 회수한다 (다시 필요하면 스포너가 새로 등장시킨다) */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD")
	float RecycleDistance = 14000.f;

	/** LOD 갱신 주기 (초). 사람 걸음 속도 기준 0.2초면 충분하다 */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD", meta = (ClampMin = "0.01"))
	float LodUpdateInterval = 0.2f;

	/** 한 번의 갱신에서 검사할 최대 인원. 인원이 많아져도 프레임 비용이 일정하게 유지된다 */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD", meta = (ClampMin = "1"))
	int32 LodSliceSize = 12;

	/**
	 * 전체를 몇 초 안에 한 바퀴 다 돌 것인가 (초).
	 *
	 * [개선] 슬라이스 크기를 고정값으로 두면, 인원이 늘수록 한 개체가 다시 검사받기까지
	 * 걸리는 시간이 같이 늘어난다. 그러면 플레이어가 빠르게 달릴 때 최저 LOD인 채로
	 * 코앞에 서 있는 상황이 생긴다. 인원에 비례해 슬라이스를 키워서 순회 주기를 고정한다.
	 * 0 이하면 이 보정을 끄고 LodSliceSize를 그대로 쓴다.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD", meta = (ClampMin = "0.0"))
	float LodFullSweepTime = 1.0f;

	// ---- 소프트 회피 ---------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Crowd|Avoidance")
	bool bEnableAvoidance = true;

	/** 이 거리보다 가까우면 서로 밀어낸다 (cm) */
	UPROPERTY(EditAnywhere, Category = "Crowd|Avoidance", meta = (ClampMin = "1.0"))
	float AvoidDistance = 110.f;

	/** 밀어내는 세기. 겹친 정도에 곱해진다 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Avoidance", meta = (ClampMin = "0.0"))
	float AvoidStrength = 0.6f;

	/** 회피 계산 주기 (초) */
	UPROPERTY(EditAnywhere, Category = "Crowd|Avoidance", meta = (ClampMin = "0.01"))
	float AvoidUpdateInterval = 0.1f;

	/**
	 * 회피 계산에 참여할 최대 인원.
	 * 이 안에서는 서로 다 비교하므로(O(n^2)) 상한을 두어 최악의 경우를 막는다.
	 * 화면에 가까이 보이는 개체만 대상이라 실제로는 훨씬 적다.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Avoidance", meta = (ClampMin = "2"))
	int32 MaxAvoidancePairs = 24;

	/**
	 * 등장 간격 (cm).
	 *
	 * [개선] 같은 지점에 연달아 등장하면 서로 겹친 채 한 덩어리로 걸어간다.
	 * 예전에 두 개체가 서로를 막아 얼어붙었던 것도 같은 원인이었다.
	 * 이 거리 안에 이미 누가 있으면 조금 밀어서 등장시킨다.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Director", meta = (ClampMin = "0.0"))
	float SpawnSpacing = 160.f;

	/** 회피 계산 타이머 */
	float AvoidTimer = 0.f;

	/** crowd.Stats 출력 타이머 */
	float StatsTimer = 0.f;

	/**
	 * 거리로는 Near/Mid인데 실제로 화면에 안 보이는 경우(벽 뒤, 등 뒤) 한 단계 낮춘다.
	 * 3인칭 게임에서는 등 뒤에 있는 군중이 상당수라 절약 폭이 크다.
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|LOD")
	bool bDemoteWhenNotRendered = true;

	// ---- 런타임 ----

	/** 활성 상태로 월드를 돌아다니는 개체들 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASubNpcCharacter>> ActiveNpcs;

	/** 숨겨진 채 대기 중인 개체들 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASubNpcCharacter>> PooledNpcs;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ArchetypeTable;

	/** 분할 갱신 커서 */
	int32 LodCursor = 0;

	float LodTimer = 0.f;
};
