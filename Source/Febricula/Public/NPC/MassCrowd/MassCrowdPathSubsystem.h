// Fill out your copyright notice in the Description page of Project Settings.
//
// 스플라인 경로를 "워커 스레드에서 안전하게 읽을 수 있는 평평한 배열"로 구워두는 서브시스템.
//
// [왜 필요한가]
//   Mass 프로세서는 워커 스레드에서 병렬로 돈다. 그런데 액터(ANpcCrowdPath)와
//   USplineComponent는 게임 스레드 소유물이라, 프로세서 안에서 만지면 안 된다.
//   GetLocationAtDistanceAlongSpline 같은 호출은 내부적으로 컴포넌트 상태를 읽고
//   캐시를 갱신할 수 있어서 특히 위험하다.
//
//   그래서 레벨이 준비된 시점에 스플라인을 일정 간격으로 샘플링해 점 배열로 굽고,
//   런타임에는 그 배열만 읽는다. 배열은 굽고 나면 바뀌지 않으므로 몇 개의 스레드가
//   동시에 읽어도 안전하다. 이것이 "개체가 바깥 것을 직접 만지지 않는다"를
//   경로 데이터에 적용한 형태다.
//
// [정확도]
//   샘플 간격(SampleStep)만큼의 오차가 생기지만, 점 사이를 선형 보간하므로
//   사람 걸음 기준으로는 눈에 띄지 않는다. 곡률이 심한 경로는 간격을 줄이면 된다.

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassCrowdPathSubsystem.generated.h"

class ANpcCrowdPath;
class ANpcActivitySpot;

/** 구워둔 경로 하나 */
USTRUCT()
struct FEBRICULA_API FMassCrowdBakedPath
{
	GENERATED_BODY()

	/** 일정 간격으로 뽑은 경로 위 점들 (월드 좌표) */
	TArray<FVector> Points;

	/** 각 점에서의 진행 방향 (정규화) */
	TArray<FVector> Directions;

	/** 각 점에서의 오른쪽 벡터. 차선 오프셋을 적용할 때 쓴다 */
	TArray<FVector> RightVectors;

	/** 점 사이 간격 (cm). 균일 샘플링이라 값 하나면 충분하다 */
	float SampleStep = 100.f;

	/** 전체 길이 (cm) */
	float Length = 0.f;

	/** 좌우로 흩어질 폭의 절반 (cm) */
	float LaneHalfWidth = 90.f;

	/** 이어질 다음 경로들 (구운 배열의 인덱스) */
	TArray<int32> ExitPathIndices;
	TArray<float> ExitWeights;
	TArray<uint8> ExitReversed;

	/** 이 경로에 붙은 활동 스팟들 (구운 스팟 배열의 인덱스) */
	TArray<int32> SpotIndices;

	bool bSpawnEntry = false;
	bool bDespawnAtEnd = false;
};

/** 구워둔 활동 스팟 하나 */
USTRUCT()
struct FEBRICULA_API FMassCrowdBakedSpot
{
	GENERATED_BODY()

	FVector StandLocation = FVector::ZeroVector;
	FVector LookAtLocation = FVector::ZeroVector;

	float EngageChance = 0.3f;
	int32 MaxOccupants = 1;

	/**
	 * 현재 점유 인원.
	 *
	 * 병렬 프로세서에서 여러 엔티티가 동시에 같은 스팟을 노릴 수 있으므로 원자적으로 센다.
	 * 액터 버전은 "HasRoom() 확인 -> TryOccupy()" 두 단계로 나뉘어 있어서
	 * 병렬화하면 둘 다 통과하는 중복 점유가 생긴다. 여기서는 한 번의 원자 연산으로 처리한다.
	 */
	mutable std::atomic<int32> Occupancy{ 0 };

	/** 원본 액터. 게임 스레드에서만 쓴다 (애니메이션 재생 등) */
	TWeakObjectPtr<ANpcActivitySpot> SourceActor;

	FMassCrowdBakedSpot() = default;

	// std::atomic은 복사가 안 되므로 배열에 담으려면 직접 정의해줘야 한다.
	FMassCrowdBakedSpot(const FMassCrowdBakedSpot& Other)
		: StandLocation(Other.StandLocation)
		, LookAtLocation(Other.LookAtLocation)
		, EngageChance(Other.EngageChance)
		, MaxOccupants(Other.MaxOccupants)
		, Occupancy(Other.Occupancy.load())
		, SourceActor(Other.SourceActor)
	{
	}

	FMassCrowdBakedSpot& operator=(const FMassCrowdBakedSpot& Other)
	{
		StandLocation = Other.StandLocation;
		LookAtLocation = Other.LookAtLocation;
		EngageChance = Other.EngageChance;
		MaxOccupants = Other.MaxOccupants;
		Occupancy.store(Other.Occupancy.load());
		SourceActor = Other.SourceActor;
		return *this;
	}
};

UCLASS()
class FEBRICULA_API UMassCrowdPathSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * 레벨의 ANpcCrowdPath / ANpcActivitySpot을 훑어 배열로 굽는다.
	 * 경로를 런타임에 추가했다면 다시 호출해야 한다 (게임 스레드에서만).
	 */
	UFUNCTION(BlueprintCallable, Category = "Crowd|Mass")
	void RebuildPathCache();

	int32 GetNumPaths() const { return BakedPaths.Num(); }
	bool IsValidPath(int32 PathIndex) const { return BakedPaths.IsValidIndex(PathIndex); }

	const FMassCrowdBakedPath& GetPath(int32 PathIndex) const { return BakedPaths[PathIndex]; }

	/** 등장구로 쓸 수 있는 경로 인덱스들 */
	const TArray<int32>& GetSpawnEntryPaths() const { return SpawnEntryPaths; }

	// ---- 워커 스레드에서 호출해도 안전한 조회 --------------------------------

	/**
	 * 경로 위 위치. 점 배열을 선형 보간한다.
	 * 액터도 컴포넌트도 만지지 않으므로 병렬 프로세서에서 불러도 된다.
	 */
	FVector GetLocationAt(int32 PathIndex, float Distance, float LaneOffset) const;

	/** 경로 위 진행 방향 */
	FVector GetDirectionAt(int32 PathIndex, float Distance) const;

	float GetPathLength(int32 PathIndex) const;
	float GetLaneHalfWidth(int32 PathIndex) const;
	bool IsDespawnAtEnd(int32 PathIndex) const;

	/**
	 * 다음 경로를 가중 랜덤으로 고른다.
	 *
	 * FRandomStream을 인자로 받는 이유: 병렬 프로세서에서 전역 난수(FMath::Rand)를
	 * 쓰면 스레드끼리 같은 상태를 건드려 레이스가 난다. 엔티티마다 자기 시드를 갖게 한다.
	 */
	bool PickNextPath(int32 PathIndex, FRandomStream& Stream, int32& OutNextPath, bool& bOutReversed) const;

	// ---- 스팟 ----------------------------------------------------------------

	bool IsValidSpot(int32 SpotIndex) const { return BakedSpots.IsValidIndex(SpotIndex); }
	const FMassCrowdBakedSpot& GetSpot(int32 SpotIndex) const { return BakedSpots[SpotIndex]; }

	/**
	 * 스팟 자리를 하나 차지한다. 성공하면 true.
	 * 원자적 비교-교환이라 여러 스레드가 동시에 불러도 정원을 넘지 않는다.
	 */
	bool TryOccupySpot(int32 SpotIndex) const;
	void ReleaseSpot(int32 SpotIndex) const;

protected:
	/** 스플라인 하나를 점 배열로 굽는다 */
	void BakeOne(const ANpcCrowdPath& Path, FMassCrowdBakedPath& Out) const;

	/** 샘플 간격 (cm). 작을수록 정확하지만 메모리를 더 쓴다 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Mass", meta = (ClampMin = "10.0"))
	float SampleStep = 100.f;

	TArray<FMassCrowdBakedPath> BakedPaths;
	TArray<FMassCrowdBakedSpot> BakedSpots;
	TArray<int32> SpawnEntryPaths;

	/** 원본 액터 -> 구운 인덱스. 출구 연결을 인덱스로 바꿀 때 쓴다 */
	TMap<TWeakObjectPtr<const ANpcCrowdPath>, int32> PathLookup;
};
