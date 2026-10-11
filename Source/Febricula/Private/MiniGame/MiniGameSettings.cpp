// [skydodo] 2026-10-10 작성
// 요약: 미니게임 설정 기본값
// Fill out your copyright notice in the Description page of Project Settings.

#include "MiniGame/MiniGameSettings.h"

#include "MiniGame/MiniGameWidget.h"
#include "Sound/SoundBase.h"

UMiniGameSettings::UMiniGameSettings()
{
	// 햄버거집 알바. ini에 값이 있으면 그쪽이 이긴다.
	// 시작 위치는 카운터 뒤 주방 통로 - 손님 내비메시(y -1140까지) 밖이라 손님 동선과 겹치지 않는다.
	FMiniGameLevelEntry Burger;
	Burger.WidgetClass = TSoftClassPtr<UMiniGameWidget>(FSoftObjectPath(TEXT("/Script/Febricula.BurgerMiniGameWidget")));
	Burger.bMovePlayer = true;
	Burger.PlayerLocation = FVector(300.f, -1310.f, 120.f);
	Burger.PlayerYaw = 90.f; // 홀 쪽(+Y)을 본다
	// 카운터 바로 뒤 눈높이에서 홀과 입구를 내다보는 구도 (플레이어는 카메라 뒤라 안 보인다)
	Burger.bUseFixedCamera = true;
	Burger.CameraLocation = FVector(300.f, -1230.f, 180.f);
	Burger.CameraRotation = FRotator(-8.f, 90.f, 0.f);
	Burger.CameraFOV = 90.f;
	Burger.BGM = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/MiniGame/Burger/Audio/S_Burger_BGM.S_Burger_BGM")));
	Burger.BGMVolume = 0.6f;
	Burger.FallbackReturnLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/MiniGame/Maps/L_MiniGameTest.L_MiniGameTest")));
	Burger.MoneyReward = 40000;
	Burger.RewardStat = TEXT("HandWork");
	Burger.StatThresholds = { 3, 5, 7, 9 };
	Levels.Add(TEXT("L_BurgerHouse"), Burger);

	// 꽃집 알바 (팀원 맵 FlowerHouse - 맵 수정 없음).
	// 가로 화면 패널이 화면을 다 덮으므로 플레이어 이동 · 고정 카메라는 쓰지 않는다.
	FMiniGameLevelEntry Flower;
	Flower.WidgetClass = TSoftClassPtr<UMiniGameWidget>(FSoftObjectPath(TEXT("/Script/Febricula.FlowerMiniGameWidget")));
	Flower.bMovePlayer = false;
	Flower.bUseFixedCamera = false;
	Flower.FallbackReturnLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/MiniGame/Maps/L_MiniGameTest.L_MiniGameTest")));
	Flower.MoneyReward = 30000;
	Flower.RewardStat = TEXT("Health");
	// 완료 주문 수 0~1건 +1, 2~3건 +2, 4~5건 +3, 6~7건 +4, 8건 이상 +5
	Flower.StatThresholds = { 2, 4, 6, 8 };
	Levels.Add(TEXT("FlowerHouse"), Flower);
}
