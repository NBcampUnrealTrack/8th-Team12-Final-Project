// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인이 잠깐 멈춰서 뭔가 하는 지점.
//
// 예)
//   키오스크  : 주문한다 / 메뉴를 고민한다 / 뒤를 돌아본다
//   진열장    : 상품을 본다 / 거울처럼 매무새를 다듬는다 / 옆사람과 얘기한다
//   벤치      : 앉는다 / 폰을 본다
//
// 한 스팟에 여러 행동을 넣고 가중치로 굴리면, 같은 자리에서 매번 다른 그림이 나온다.
// 행동에 필요한 건 결국 두 가지뿐이다: **무슨 애니메이션**을 재생하고 **어디를 보는가**.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/SubNpc/SubNpcTypes.h"
#include "NpcActivitySpot.generated.h"

class UArrowComponent;
class UBillboardComponent;
class ASubNpcCharacter;

/** 스팟을 점유 중인 행인 하나와, 그가 하고 있는 행동 */
USTRUCT()
struct FNpcSpotOccupant
{
	GENERATED_BODY()

	/** 약한 참조로 들고 있어야 스팟이 NPC를 붙잡아 GC를 막는 일이 없다 */
	UPROPERTY()
	TWeakObjectPtr<ASubNpcCharacter> Npc;

	/** Activities 배열의 인덱스. 독점 행동 판정에 쓴다 */
	UPROPERTY()
	int32 ActivityIndex = INDEX_NONE;
};

UCLASS()
class FEBRICULA_API ANpcActivitySpot : public AActor
{
	GENERATED_BODY()

public:
	ANpcActivitySpot();

	/**
	 * 지금 이 스팟을 쓸 수 있는가 (정원이 남았는가).
	 * 키오스크 한 대에 다섯 명이 겹쳐 서는 걸 막는다.
	 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	bool HasRoom() const { return CurrentOccupants < MaxOccupants; }

	/** 이 스팟에 들를 확률 (0~1). 경로를 지나갈 때마다 판정한다 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	float GetEngageChance() const { return EngageChance; }

	/** 동시 정원 (Mass 경로 캐시를 구울 때 읽는다) */
	int32 GetMaxOccupants() const { return MaxOccupants; }

	/**
	 * 재생할 행동을 고른다. 고정 확률(Chance) 행동을 먼저 굴리고,
	 * 안 뽑히면 나머지를 가중 랜덤으로 고른다. 독점 행동 중 누가 하고 있는 건 빠진다.
	 * 고를 수 있는 행동이 없으면 false.
	 *
	 * OutIndex는 TryOccupy에 그대로 넘긴다.
	 */
	bool PickActivity(FNpcActivityOption& OutOption, int32& OutIndex) const;

	/** 이 행동을 지금 고를 수 있는가 (독점 행동이면 아무도 안 하고 있어야 한다) */
	bool IsActivityAvailable(int32 ActivityIndex) const;

	/** NPC가 실제로 서야 할 위치 (스팟 앞) */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	FVector GetStandLocation() const;

	/** 선택한 행동에서 바라볼 월드 좌표 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	FVector GetLookAtLocation(const FNpcActivityOption& Option) const;

	/**
	 * 점유 등록/해제. NPC가 반드시 짝을 맞춰 호출해야 한다.
	 * ActivityIndex는 PickActivity가 준 값. 그 사이 다른 행인이 같은 독점 행동을 잡았으면 실패한다.
	 */
	bool TryOccupy(ASubNpcCharacter* Npc, int32 ActivityIndex);
	void ReleaseOccupancy(ASubNpcCharacter* Npc);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 이 스팟에서 일어날 수 있는 행동들 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot")
	TArray<FNpcActivityOption> Activities;

	/** 지나가는 행인이 이 스팟에 들를 확률 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EngageChance = 0.25f;

	/** 동시에 이 스팟을 쓸 수 있는 인원 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot", meta = (ClampMin = "1"))
	int32 MaxOccupants = 1;

	/**
	 * 서는 위치를 매번 조금씩 흩뜨릴 반경 (cm).
	 * 0이면 항상 정확히 같은 자리에 서서 부자연스럽다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot", meta = (ClampMin = "0.0"))
	float StandJitterRadius = 25.f;

	/**
	 * 같은 NPC가 이 스팟을 다시 쓰기까지의 최소 간격 (초).
	 * 루프 경로에서 같은 행인이 계속 같은 자리에 멈추는 걸 막는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot", meta = (ClampMin = "0.0"))
	float ReuseCooldown = 30.f;

	/** 에디터에서 위치/방향을 보기 위한 표식 (게임에서는 안 보인다) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Spot")
	TObjectPtr<USceneComponent> SpotRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Crowd|Spot")
	TObjectPtr<UArrowComponent> FacingArrow;

	/** 현재 이 스팟을 쓰고 있는 NPC 수 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Crowd|Spot")
	int32 CurrentOccupants = 0;

	/** 지금 점유 중인 NPC들과 각자 하고 있는 행동 */
	UPROPERTY(Transient)
	TArray<FNpcSpotOccupant> Occupants;

	/** NPC별 마지막 사용 시각 (재사용 쿨다운 판정용) */
	TMap<TWeakObjectPtr<ASubNpcCharacter>, float> LastUseTime;
};
