// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SettingsSaveGame.generated.h"

/** 게임 설정을 담는 세이브 그릇. 슬롯 이름 "Settings", 게임 세이브 슬롯과 별개. */
UCLASS()
class FEBRICULA_API USettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() float BGMVolume  = 0.8f;
	UPROPERTY() float SFXVolume  = 0.8f;
	UPROPERTY() bool  bMuted     = false;
	UPROPERTY() float TextSpeed  = 0.5f;
	UPROPERTY() float AutoSpeed  = 0.5f;
};
