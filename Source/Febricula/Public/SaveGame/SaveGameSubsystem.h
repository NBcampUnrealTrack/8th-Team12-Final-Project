// Fill out your copyright notice in the Description page of Project Settings.
//
// 저장/불러오기의 단일 진입점.
//
// [왜 UGameInstanceSubsystem인가]
//   저장 대상 중 절반(시간/진행도/호감도/행동력)은 레벨이 바뀌어도 유지되는
//   GameInstance급 값이고, 절반(플레이어 위치, 대화 백로그)은 레벨(World)이 있어야만
//   의미가 있는 값이다. 이 둘을 한 번에 처리하려면 레벨 전환을 아우르는 객체가 필요하므로
//   GameInstance 수명을 갖는 이 서브시스템이 적임이다.
//
// [로드 흐름 - 왜 두 단계로 나뉘는가]
//   저장된 레벨이 지금 레벨과 다르면, LoadFromSlot()이 OpenLevel을 호출한 시점에는
//   새 레벨이 아직 존재하지 않는다. 그래서:
//     1) GameInstance급 값(시간/진행도/호감도/행동력)은 LoadFromSlot()에서 즉시 반영
//     2) 레벨 종속 값(플레이어 위치, 대화 백로그)은 PendingXxx에 담아두고 대기
//     3) 새 레벨이 완전히 뜬 뒤(AFebriculaGameMode::BeginPlay) ApplyPendingLoadIfAny()를
//        호출해서 마저 반영한다.
//   저장된 레벨과 지금 레벨이 같으면 레벨 이동이 없으므로 그 자리에서 바로 2)까지 적용한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dialogue/DialogueSubsystem.h"
#include "SaveGameSubsystem.generated.h"

class UFebriculaSaveGame;

UCLASS()
class FEBRICULA_API USaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 슬롯(1~3 등)에 지금 상태를 저장한다. 성공 여부 반환 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool SaveToSlot(int32 SlotIndex);

	/**
	 * 슬롯을 불러온다.
	 * 저장된 레벨이 지금과 다르면 레벨 이동을 트리거하고, 레벨이 뜬 뒤 ApplyPendingLoadIfAny()가
	 * 마저 처리해야 완전히 끝난다. 같은 레벨이면 이 호출 안에서 전부 끝난다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	bool LoadFromSlot(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category = "Save")
	bool DoesSlotExist(int32 SlotIndex) const;

	/**
	 * 레벨 로드가 끝난 뒤(AFebriculaGameMode::BeginPlay 등) 호출해서, 월드가 있어야만
	 * 적용 가능한 값(플레이어 위치, 대화 백로그)을 마저 반영한다.
	 * 대기 중인 로드가 없으면 아무 일도 하지 않으니 매 BeginPlay마다 불러도 안전하다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Save")
	void ApplyPendingLoadIfAny();

	UFUNCTION(BlueprintPure, Category = "Save")
	bool HasPendingLoad() const { return bHasPendingLoad; }

protected:
	static FString GetSlotName(int32 SlotIndex);

	/** GameInstance급(레벨 무관) 서브시스템 값을 즉시 반영한다 */
	void ApplyGameInstanceData(const UFebriculaSaveGame* Save);

	// ---- 레벨이 뜬 뒤에야 적용 가능한, 대기 중인 로드 데이터 ----
	bool bHasPendingLoad = false;
	FVector PendingPlayerLocation = FVector::ZeroVector;
	FRotator PendingPlayerRotation = FRotator::ZeroRotator;
	TArray<FDialogueLogEntry> PendingDialogueLog;
};
