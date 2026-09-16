// Fill out your copyright notice in the Description page of Project Settings.
//
// 거리에 사람이 끊기지 않게 채워 넣는 스포너.
//
// 레벨에 하나만 두면 된다. 하는 일:
//   1) 게임 시작 시 풀을 미리 채운다 (메타휴먼을 플레이 중에 만들지 않기 위해)
//   2) 주기적으로 "지금 목표 인원보다 적으면" 입구 경로에서 새 행인을 내보낸다
//   3) 시간대에 따라 목표 인원을 바꾼다 (UTimeSubsystem 연동 - 밤에는 한산하게)
//
// 스폰 자체는 USubNpcDirectorSubsystem이 하고, 여기는 "언제 몇 명" 만 결정한다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NpcCrowdSpawner.generated.h"

class ANpcCrowdPath;
class ASubNpcCharacter;
class UDataTable;

/** 시간대별 인구 밀도 배율 */
USTRUCT(BlueprintType)
struct FCrowdDensityByHour
{
	GENERATED_BODY()

	/** 시간대 GameplayTag 이름 (예: Schedule.MorningClass). 비우면 기본값 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
	FName ScheduleTagName;

	/** 목표 인원에 곱할 배율. 0이면 그 시간대엔 아무도 안 다닌다 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd", meta = (ClampMin = "0.0"))
	float DensityMultiplier = 1.f;
};

UCLASS()
class FEBRICULA_API ANpcCrowdSpawner : public AActor
{
	GENERATED_BODY()

public:
	ANpcCrowdSpawner();

	/** 지금 목표 인원 (시간대 배율까지 반영한 값) */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spawner")
	int32 GetCurrentTargetCount() const;

	/** 즉시 목표 인원까지 채운다 (테스트용). 플레이어 시야를 신경쓰지 않는다 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Spawner")
	void FillImmediately();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 주기적으로 인원을 확인하고 부족하면 채운다 */
	void TickSpawn();

	/** 입구로 쓸 수 있는 경로 중 하나를 고른다 */
	ANpcCrowdPath* PickEntryPath() const;

	/**
	 * 초기 충원을 끝냈는가.
	 *
	 * 시작 직후 한 번만 경로 전체에 흩어 놓고, 그 뒤의 보충은 입구에서만 등장시킨다.
	 * 목표 인원을 다 채운 시점에 켜진다.
	 */
	UPROPERTY(Transient)
	bool bInitialFillDone = false;

	/** 플레이어 폰 위치 (없으면 false) */
	bool GetViewerLocation(FVector& OutLocation) const;

	/** 스폰할 인원(단독/그룹)을 굴린다 */
	int32 RollGroupSize() const;

	/** 스폰할 클래스 (블루프린트로 만든 서브 NPC) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner")
	TSubclassOf<ASubNpcCharacter> NpcClass;

	/**
	 * 미리 만들어둘 풀 크기. 목표 인원보다 넉넉해야 회수/재등장이 끊기지 않는다.
	 * 이 값이 곧 메모리 상한이다 - 여기서 정한 만큼만 메타휴먼이 존재한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner", meta = (ClampMin = "0"))
	int32 PoolSize = 60;

	/** 평상시 유지할 활성 인원 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner", meta = (ClampMin = "0"))
	int32 BaseTargetCount = 30;

	/** 인원 점검 주기 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner", meta = (ClampMin = "0.1"))
	float SpawnInterval = 1.f;

	/** 한 번에 최대 몇 명까지 등장시킬지. 크면 한꺼번에 우르르 나타나 티가 난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner", meta = (ClampMin = "1"))
	int32 MaxSpawnPerTick = 2;

	/**
	 * 플레이어와 이 거리(cm) 안에서는 등장시키지 않는다.
	 *
	 * 입구 구간이 화면 안에 있으면 보충이 일어날 때마다 눈앞에서 사람이 튀어나온다.
	 * 후보 지점을 몇 번 다시 굴려보고 끝내 못 찾으면 그 주기는 건너뛴다.
	 * 0이면 이 검사를 끈다 (예전 동작).
	 *
	 * 입구가 벽 뒤나 골목처럼 원래 안 보이는 곳이면 낮춰도 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner", meta = (ClampMin = "0.0"))
	float MinSpawnDistanceFromViewer = 2500.f;

	/** 아키타입 테이블 (FSubNpcArchetypeRow) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spawner")
	TObjectPtr<UDataTable> ArchetypeTable;

	/**
	 * 입구로 쓸 경로들. 비워두면 레벨 전체에서 bSpawnEntry가 켜진 경로를 자동으로 찾는다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Crowd|Spawner")
	TArray<TObjectPtr<ANpcCrowdPath>> EntryPaths;

	// ---- 그룹 ----

	/** 일행 없이 혼자 다닐 확률 (0~1). 나머지는 2~MaxGroupSize명 무리 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Group", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoloChance = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Group", meta = (ClampMin = "2"))
	int32 MaxGroupSize = 3;

	// ---- 시간대 연동 ----

	/**
	 * 시간대별 인구 배율. 비워두면 항상 BaseTargetCount를 유지한다.
	 * UTimeSubsystem이 없으면 이 설정은 무시된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Time")
	TArray<FCrowdDensityByHour> DensityBySchedule;

	FTimerHandle SpawnTimerHandle;
};
