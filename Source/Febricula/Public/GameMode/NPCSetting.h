// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NPC/BaseNpcCharacter.h"
#include "NPCSetting.generated.h"

class UDataTable;

/**
 * 
 */
UCLASS(Config="Game")
class FEBRICULA_API UNPCSetting : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "NPC Character Map")
	TMap<FName, TSoftClassPtr<ABaseNpcCharacter>> NPCCharacterMap;

	/**
	 * 행인(서브 NPC) 기본 아키타입 테이블 (FSubNpcArchetypeRow).
	 *
	 * 스포너에 ArchetypeTable을 따로 지정하지 않은 레벨은 이 테이블을 쓴다.
	 * 행인 대사(IdleBarks / BumpedBarks), 반응 애니메이션 키, 속도·체격 범위를
	 * 여기 한 곳에서 관리한다. 레벨마다 다르게 하고 싶을 때만 스포너에서 덮어쓴다.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Sub NPC",
		meta = (RequiredAssetDataTags = "RowStructure=/Script/Febricula.SubNpcArchetypeRow"))
	TSoftObjectPtr<UDataTable> DefaultSubNpcArchetypeTable;
};
