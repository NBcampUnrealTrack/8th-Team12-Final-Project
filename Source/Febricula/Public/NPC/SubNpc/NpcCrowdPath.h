// [skydodo] 2026-09-01 작성 (최종 수정 2026-10-06)
// 요약: 행인이 따라 걷는 스플라인 경로 구간 - 이어 붙여 경로망을 만든다
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
#include "GameplayTagContainer.h"
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
	 * 차선 위를 실제로 1cm 걸을 때 중심선 진행도가 몇 cm 올라야 하는가.
	 *
	 * 행인은 중심선 진행도(PathDistance)를 걸음 속도로 올리고 차선만큼 옆으로 민 자리에 선다.
	 * 직선에서는 1이지만 굽은 곳에서는 안쪽 차선의 실제 이동이 줄고 바깥쪽은 늘어난다.
	 * 직각에 가까운 모서리에서는 안쪽 차선 사람이 거의 제자리에 서 버려 뒷사람이 줄줄이 막혔다.
	 * 이 배율을 곱하면 차선과 상관없이 실제 걸음 속도가 일정해진다.
	 */
	float GetLaneProgressScale(float Distance, float LaneOffset, float Along) const;

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

	/** 이 경로에서 등장할 때 역방향(끝 -> 시작)으로 걷게 할지 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	bool IsSpawnReversed() const { return bSpawnReversed; }

	/** 이지민 수정(2026. 10. 10)
	 * 특정 시간대에만 활성화 되는 경로 구현을 위해 추가
	 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Path")
	bool IsOpen() const;
	
	/**
	 * 중심선·걷는 폭·추월 최대 폭·방향·퇴장구를 디버그 선으로 그린다.
	 * 에디터 뷰포트(배치할 때)와 게임 중 crowd.DrawPaths가 같이 쓴다.
	 *
	 * @param SqueezeOverflow  추월할 때 길 가장자리를 넘어갈 수 있는 거리 (cm)
	 * @param Lifetime         -1이면 이번 프레임만 그린다
	 */
	void DrawLaneDebug(float SqueezeOverflow, float Lifetime = -1.f) const;

	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	/** 에디터 뷰포트에서도 틱해서 길 폭을 그린다 (bDrawLanesInEditor) */
	virtual bool ShouldTickIfViewportsOnly() const override { return bDrawLanesInEditor; }
#endif

protected:
	/** 경로 모양. 에디터에서 점을 찍어 곡선을 만든다 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	TObjectPtr<USplineComponent> Spline;
	
	/** 이지민 수정(2026. 10. 10)
	 * 특정 시간대에만 활성화 되는 경로 구현을 위해 추가
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	FGameplayTagContainer ClosedSchedules;
	
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
	 * 등장한 행인이 이 경로를 역방향으로 걷게 한다.
	 *
	 * 진행 방향은 스플라인 점 순서(0번 -> 마지막)로 정해진다. 경로를 반대로 그렸다면
	 * 점을 다시 찍는 대신 이 체크박스를 켜면 된다. 이미 배치한 경로를 다시 그리는 건
	 * 이어붙인 출구 연결까지 흔들기 때문에, 뒤집는 스위치를 두는 쪽이 안전하다.
	 *
	 * 에디터에서 파란 화살표로 실제 진행 방향을 확인할 수 있다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Path")
	bool bSpawnReversed = false;

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

#if WITH_EDITORONLY_DATA
	/**
	 * 진행 방향을 보여주는 화살표들 (에디터 전용).
	 *
	 * 처음에는 DrawDebugDirectionalArrow로 그렸는데 수명이 지나면 사라져서,
	 * 경로를 배치하는 동안 계속 보이지 않았다. 컴포넌트로 두면 액터가 살아있는 한 남는다.
	 * bIsEditorOnly라 게임에는 아예 만들어지지 않는다.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UArrowComponent>> DirectionArrows;

	/** 방향 화살표를 몇 cm 간격으로 놓을지 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Debug", meta = (ClampMin = "50.0"))
	float ArrowSpacing = 400.f;

	/** 방향 화살표를 표시할지 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Debug")
	bool bShowDirectionArrows = true;

	/**
	 * 레벨 편집 중에도 걷는 폭(초록)과 추월 최대 폭(회색)을 그린다.
	 * 경로를 벽·지형 옆에 놓을 때 폭이 걸리는지 배치하면서 바로 볼 수 있다.
	 * (에디터 뷰포트가 실시간(Realtime) 모드여야 계속 보인다)
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Debug")
	bool bDrawLanesInEditor = true;
#endif

#if WITH_EDITOR
	/** 에디터에서 진행 방향과 출구 연결을 눈으로 확인할 수 있게 한다 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 방향 화살표를 다시 만든다 */
	void RebuildDirectionArrows();
#endif
};
