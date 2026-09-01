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
	 */
	ASubNpcCharacter* SpawnOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed);

	/**
	 * 여러 명을 한 무리로 등장시킨다. 첫 번째가 리더, 나머지는 뒤를 따른다.
	 * 레퍼런스 영상의 "커플/일행"이 이것이다.
	 */
	TArray<ASubNpcCharacter*> SpawnGroupOnPath(ANpcCrowdPath* Path, float StartDistance, bool bReversed, int32 GroupSize);

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
