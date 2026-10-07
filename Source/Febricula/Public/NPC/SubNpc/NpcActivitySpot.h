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

	/** 몇 번째 칸에 서 있는가. 정원이 여러 명이어도 서로 다른 칸에 서서 겹치지 않는다 */
	UPROPERTY()
	int32 SlotIndex = 0;
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

	/**
	 * NPC가 실제로 서야 할 위치. 칸(Slot)마다 SlotSpacing만큼 옆으로 떨어져 있다.
	 * 예전에는 반경 안의 아무 점이나 뽑아서 정원이 여러 명이면 서로 겹쳐 섰다.
	 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	FVector GetStandLocation(int32 SlotIndex = 0) const;

	// ---- 줄 서기 -------------------------------------------------------------

	/**
	 * 지금 바로 들어갈 수 있는가. 자리가 있어도 먼저 와서 기다리는 사람이 있으면
	 * 그 사람이 먼저다 (줄의 맨 앞만 들어갈 수 있다).
	 */
	bool CanEnterNow(const ASubNpcCharacter* Npc) const;

	/** 줄 끝에 선다. 줄이 꽉 찼거나 재사용 쿨다운 중이면 false */
	bool TryJoinQueue(ASubNpcCharacter* Npc);

	/** 줄에서 빠진다 (포기·반응·회수). 줄에 없으면 아무 일도 없다 */
	void LeaveQueue(ASubNpcCharacter* Npc);

	/** 줄에서 몇 번째인가 (0 = 맨 앞). 줄에 없으면 INDEX_NONE */
	int32 GetQueueIndex(const ASubNpcCharacter* Npc) const;

	/** 지금 줄 선 인원 (새로 서는 사람은 이 번호 자리에 선다) */
	int32 GetQueueLength() const;

	/** 줄의 Index번째 사람이 기다릴 위치 */
	FVector GetQueueLocation(int32 Index) const;

	/** 줄에서 이 시간(초)보다 오래 기다리면 포기하고 지나간다 */
	float GetMaxQueueWait() const { return MaxQueueWait; }

	/** 일행 중 한 명이 들를 수 있는 스팟인가 (나머지가 기다릴 자리가 있다) */
	bool AllowsGroupVisit() const { return bAllowGroupVisit; }

	/** 일행이 모여 기다릴 자리의 중심 (월드) */
	FVector GetGroupWaitCenter() const;

	/** Count명이 둘러설 때 Index번째 사람의 자리 (월드). 중심을 보고 서면 서로 마주 본다 */
	FVector GetGroupWaitLocation(int32 Index, int32 Count) const;

	/** 일행이 이 시간(초)보다 오래 기다리면 들른 사람만 일행에서 빠지고 나머지는 떠난다 */
	float GetMaxGroupVisitWait() const { return MaxGroupVisitWait; }

	/** SlotIndex번 칸의 중심 (흔들기 전). 서는 자리와 디버그 표시가 같이 쓴다 */
	FVector GetSlotCenter(int32 SlotIndex) const;

	/** 칸·줄 자리·바라볼 방향을 디버그 선으로 그린다. Lifetime -1이면 이번 프레임만 */
	void DrawSpotDebug(float Lifetime = -1.f) const;

	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	/** 에디터 뷰포트에서도 틱해서 칸·줄 자리를 그린다 (bDrawSlotsInEditor) */
	virtual bool ShouldTickIfViewportsOnly() const override { return bDrawSlotsInEditor; }
#endif

	/** 선택한 행동에서 바라볼 월드 좌표 */
	UFUNCTION(BlueprintPure, Category = "Crowd|Spot")
	FVector GetLookAtLocation(const FNpcActivityOption& Option) const;

	/**
	 * 점유 등록/해제. NPC가 반드시 짝을 맞춰 호출해야 한다.
	 * ActivityIndex는 PickActivity가 준 값. 그 사이 다른 행인이 같은 독점 행동을 잡았으면 실패한다.
	 */
	bool TryOccupy(ASubNpcCharacter* Npc, int32 ActivityIndex, int32& OutSlotIndex);
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
	 * 정원이 여러 명일 때 칸 사이 간격 (cm). 스팟의 오른쪽 방향으로 나란히 선다.
	 * 캡슐 지름(약 68cm)보다 커야 어깨가 겹치지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot", meta = (ClampMin = "70.0"))
	float SlotSpacing = 90.f;

	/** 자리가 없을 때 줄을 설 수 있는 최대 인원. 0이면 줄을 서지 않고 그냥 지나간다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Queue", meta = (ClampMin = "0"))
	int32 MaxQueueLength = 3;

	/** 줄 선 사람 사이 간격 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Queue", meta = (ClampMin = "70.0"))
	float QueueSpacing = 80.f;

	/**
	 * 줄이 늘어설 방향 (스팟 로컬). 기본은 스팟의 뒤쪽 - 이벤트하는 사람 뒤로 한 줄로 서서 앞을 본다.
	 * 스팟은 길을 바라보게 놓으므로 줄은 길에서 멀어지는 쪽으로 뻗는다.
	 * [변경] 예전 기본값은 오른쪽(0,1,0)이라, 정원이 1이면 이벤트하는 사람 옆에 가로로 서서 옆을 쳐다보는 모양이었다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Queue")
	FVector QueueDirection = FVector(-1.f, 0.f, 0.f);

	/** 줄에서 이 시간(초)보다 오래 기다리면 포기하고 지나간다. 영원히 서 있는 사람을 막는다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Queue", meta = (ClampMin = "1.0"))
	float MaxQueueWait = 20.f;

	/**
	 * 일행(2~5명 무리) 중 한 명이 이 스팟에 들를 수 있다. 나머지는 일행 대기 자리(초록 원)에 비켜서서
	 * 이야기하며 기다렸다가 같이 떠난다. 길가에 4명이 설 공간이 있는 스팟에만 켠다 - 좁은 곳에서 켜면 병목이 된다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Group")
	bool bAllowGroupVisit = false;

	/**
	 * 일행이 모여 기다릴 자리의 중심 (스팟 로컬). 뷰포트에 끌어서 옮기는 표시가 나온다 - 줄(주황 원)·길에서
	 * 떨어진 빈 곳에 놓는다. 기다리는 사람들은 이 점을 둘러싸고 서로 마주 보고 선다 (초록 원).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Group", meta = (MakeEditWidget = true))
	FVector GroupWaitCenter = FVector(-250.f, -250.f, 0.f);

	/** 둘러설 원의 최소 반지름 (cm). 인원이 많으면 어깨가 닿지 않게 자동으로 넓어진다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Group", meta = (ClampMin = "40.0"))
	float GroupWaitRadius = 55.f;

	/** 일행이 기다리는 최대 시간 (초). 넘으면 들른 사람만 일행에서 빠지고 나머지는 떠난다 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Crowd|Spot|Group", meta = (ClampMin = "1.0"))
	float MaxGroupVisitWait = 30.f;

#if WITH_EDITORONLY_DATA
	/**
	 * 레벨 편집 중에도 칸(노랑 원)·줄 자리(주황 원과 번호)·바라볼 방향(하늘 화살표)을 그린다.
	 * 줄이 길 쪽으로 뻗거나 칸이 벽에 박히는지 배치하면서 바로 볼 수 있다.
	 * (에디터 뷰포트가 실시간(Realtime) 모드여야 계속 보인다)
	 */
	UPROPERTY(EditAnywhere, Category = "Crowd|Spot|Debug")
	bool bDrawSlotsInEditor = true;
#endif

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

	/** 자리가 나길 기다리는 사람들. 먼저 온 순서대로 들어간다 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ASubNpcCharacter>> WaitQueue;

	/** 쿨다운 중인가 (같은 행인이 같은 스팟에 계속 멈추지 않게) */
	bool IsOnReuseCooldown(const ASubNpcCharacter* Npc) const;

	/** 죽은 참조를 줄에서 뺀다 */
	void CleanQueue();

	/** NPC별 마지막 사용 시각 (재사용 쿨다운 판정용) */
	TMap<TWeakObjectPtr<ASubNpcCharacter>, float> LastUseTime;
};
