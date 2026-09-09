// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인이 따라 걷는 경로 한 구간.
//
// 레벨에 이 액터를 여러 개 깔고 서로 이어 붙이면 레퍼런스 영상의 "바닥에 그려진 선 망"이 된다.
// 갈림길은 Exits를 2개 이상 두는 것으로 표현하고, NPC는 도착할 때마다 가중치로 하나를 고른다.
//
// [왜 NavMesh를 안 쓰는가]
//   행인은 수십~수백 마리다. NavMesh 경로 질의와 회피(RVO)는 마리당 비용이 커서
//   그 숫자를 감당하지 못한다. 어차피 행인은 "정해진 길을 걷기만" 하면 되므로
//   스플라인 위 거리값 하나만 굴리면 충분하다. 이게 이 시스템 성능의 8할이다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "NpcCrowdPath.generated.h"

class USplineComponent;
class ANpcActivitySpot;

UCLASS()
class FEBRICULA_API ANpcCrowdPath : public AActor
{
	GENERATED_BODY()

public:
	ANpcCrowdPath();

	USplineComponent* GetSpline() const { return Spline; }

	/** 스플라인 전체 길이 (cm) */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	float GetPathLength() const;

	/**
	 * 경로 위 Distance 지점의 월드 위치.
	 * LaneOffset은 진행 방향 기준 좌우 오프셋 - 여러 명이 한 줄로 겹쳐 걷지 않게 한다.
	 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	FVector GetLocationAtDistance(float Distance, float LaneOffset = 0.f) const;

	/** 경로 위 Distance 지점의 진행 방향 (정규화) */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	FVector GetDirectionAtDistance(float Distance) const;

	/**
	 * 월드 좌표에서 가장 가까운 이 경로 위의 지점을 찾는다.
	 * 배치 위치 기준으로 스폰할 때, 어느 스플라인의 몇 cm 지점인지 역산하는 용도다.
	 *
	 * @param OutDistanceAlongPath  그 지점의 경로 진행 거리 (cm)
	 * @return                      해당 지점까지의 거리 제곱 (경로 비교용)
	 */
	float FindClosestPointOnPath(const FVector& WorldLocation, float& OutDistanceAlongPath) const;

	/**
	 * 이 경로가 끝났을 때 이어갈 다음 경로를 가중 랜덤으로 고른다.
	 * 출구가 없으면 nullptr (그 자리에서 회수되거나 되돌아간다).
	 *
	 * @param bOutReversed  고른 경로를 역방향으로 타야 하는지
	 */
	ANpcCrowdPath* PickNextPath(bool& bOutReversed) const;

	/** 이 경로 구간에 붙어 있는 활동 스팟들 (키오스크, 진열장 등) */
	const TArray<TObjectPtr<ANpcActivitySpot>>& GetActivitySpots() const { return ActivitySpots; }

	/** 스포너가 이 경로를 시작점으로 쓸 수 있는가 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	bool IsSpawnEntry() const { return bSpawnEntry; }

	/** 이 경로에서 동시에 걸을 수 있는 좌우 차선 폭 (cm). 개체마다 이 범위에서 랜덤 오프셋 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	float GetLaneHalfWidth() const { return LaneHalfWidth; }

	/** 이 경로 끝은 퇴장구인가. 켜면 보이든 말든 끝에 닿는 즉시 풀로 회수한다 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	bool IsDespawnAtEnd() const { return bDespawnAtEnd; }

	/** 출구 목록 (Mass 경로 캐시를 구울 때 읽는다) */
	const TArray<FNpcPathExit>& GetExits() const { return Exits; }

protected:
	/** 경로 모양. 에디터에서 점을 찍어 곡선을 만든다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	TObjectPtr<USplineComponent> Spline;

	/**
	 * 이 경로가 끝난 뒤 갈 수 있는 길들. 2개 이상이면 갈림길이 된다.
	 * 비워두면 막다른 길 - NPC는 여기서 풀로 회수된다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Crowd|Path")
	TArray<FNpcPathExit> Exits;

	/**
	 * 이 구간에 붙은 활동 스팟.
	 * NPC가 이 경로를 걷기 시작할 때 "여기서 뭔가 할까"를 스팟마다 확률 판정한다.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Crowd|Path")
	TArray<TObjectPtr<ANpcActivitySpot>> ActivitySpots;

	/** 좌우로 흩어질 폭의 절반 (cm). 인도 폭에 맞춘다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path", meta = (ClampMin = "0.0"))
	float LaneHalfWidth = 90.f;

	/**
	 * 스포너가 이 경로에서 새 행인을 등장시킬 수 있는가.
	 * 맵 가장자리(플레이어 시야 밖에서 걸어 들어오는 지점)에만 켜는 게 자연스럽다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	bool bSpawnEntry = false;

	/**
	 * 이 경로의 끝을 "퇴장구"로 취급한다.
	 *
	 * 기본(끔)은 막다른 길에서 보이는 거리면 되돌아가게 한다. 눈앞에서 사람이 뻥 사라지는 걸
	 * 막기 위해서다. 하지만 맵 밖으로 나가는 길, 건물 출입문, 골목 끝처럼 "여기로 나가면
	 * 사라지는 게 맞는" 구간에서는 되돌아가는 쪽이 오히려 어색하다.
	 *
	 * 켜면 시야 안이든 밖이든 끝에 닿는 즉시 풀로 회수한다.
	 * 문 안쪽으로 몇 미터 더 들어가도록 스플라인을 그려두면 사라지는 순간이 가려진다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	bool bDespawnAtEnd = false;

#if WITH_EDITOR
	/** 에디터에서 출구 연결을 선으로 그려서 망 구조를 눈으로 확인할 수 있게 한다 */
	virtual void OnConstruction(const FTransform& Transform) override;
#endif
};
