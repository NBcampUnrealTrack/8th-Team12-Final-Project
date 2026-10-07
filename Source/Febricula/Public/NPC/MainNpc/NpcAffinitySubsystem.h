// Fill out your copyright notice in the Description page of Project Settings.
//
// 메인 NPC 호감도(친밀도) 수치/단계의 "단일 소유자".
// AMainNpcCharacter는 절대 호감도 값을 직접 들고 있지 않고, 이 서브시스템을 통해서만 읽고/쓴다.
// 나중에 SaveGame 로드/세이브 로직은 이 클래스 안에서 처리하도록 확장하면 된다 (지금은 런타임 메모리 스텁).
// 이지민 수정
// 해당 Subsystem이 플레이어 스탯까지 총괄하도록 변경
#pragma once

#include "CoreMinimal.h"
#include "Components/PlayerStat.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NPC/NpcTypes.h"
#include "NpcAffinitySubsystem.generated.h"

/** UNpcRelationshipSubsystem의 상태를 세이브 파일에 담기 위한 스냅샷 */
USTRUCT(BlueprintType)
struct FNpcRelationshipSaveData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Save")
	TMap<FName, float> AffinityByNpcId;
};

UCLASS(BlueprintType)
class FEBRICULA_API UNpcAffinitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** NpcId의 현재 호감도 원시 수치. 등록된 적 없으면 0을 반환 (= 평범/Neutral 시작값) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	float GetAffinityValue(FName NpcId) const;

	/** 원시 수치를 이벤트 트리 조건 판정용 단계(Wary~Intimate)로 변환 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	ENpcRelationshipLevel GetRelationshipLevel(FName NpcId) const;

	/** 이벤트 트리의 "친밀도 상승/하락" 결과를 반영할 때 사용 (예: AddAffinity("NPC_HanSeoYun", -5.f)) */
	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship")
	float AddAffinity(FName NpcId, float Delta);

	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship")
	void SetAffinity(FName NpcId, float NewValue);

	/** 이벤트 트리 전제 조건 "관계가 O 이상" 을 그대로 검사하는 헬퍼 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "NPC|Relationship")
	bool IsRelationshipAtLeast(FName NpcId, ENpcRelationshipLevel MinimumLevel) const;

	// ---- 세이브/로드 ----

	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship|Save")
	FNpcRelationshipSaveData GetSaveData() const;

	UFUNCTION(BlueprintCallable, Category = "NPC|Relationship|Save")
	void LoadSaveData(const FNpcRelationshipSaveData& Data);

protected:
	/** NpcId -> 원시 호감도 수치. 프로젝트가 커지면 SaveGame 슬롯으로 옮겨 담는다 */
	UPROPERTY()
	TMap<FName, float> AffinityByNpcId;
	
	// 단계 컷오프는 우선 디자이너가 즉시 튜닝할 수 있도록 EditDefaultsOnly로 노출.
	// 나중에 데이터테이블/커브 에셋으로 옮겨도 인터페이스(GetRelationshipLevel)는 그대로 유지된다.
	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float WaryThreshold = -20.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float FavorThreshold = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float TrustThreshold = 50.f;

	UPROPERTY(EditDefaultsOnly, Category = "NPC|Relationship|Threshold")
	float IntimateThreshold = 80.f;
};