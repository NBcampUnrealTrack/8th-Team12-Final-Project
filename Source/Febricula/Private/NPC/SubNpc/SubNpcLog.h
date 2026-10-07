// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인(군중) 로그 스위치 — 콘솔 변수 crowd.Log.
//
// 행인 수십 명이 각자 찍으면 출력 로그가 금방 가득 찬다. 그래서 자동으로 찍히는 군중 로그는
// 전부 SUBNPC_LOG로 쓰고, crowd.Log 1일 때만 남긴다 (기본은 꺼짐).
//   SUBNPC_LOG(Warning, TEXT("[Crowd] ..."), ...);   // UE_LOG(LogTemp, ...)와 같은 인자
//
// 콘솔 명령의 결과(crowd.Bench, crowd.BakeFarCrowd 등)처럼 직접 실행했을 때만 나오는 로그는
// 그대로 UE_LOG를 쓴다.

#pragma once

#include "CoreMinimal.h"

/** crowd.Log 값. SubNpcDirectorSubsystem.cpp에 정의돼 있다 */
extern int32 GSubNpcCrowdLog;

#define SUBNPC_LOG(Verbosity, Format, ...) \
	do \
	{ \
		if (GSubNpcCrowdLog != 0) \
		{ \
			UE_LOG(LogTemp, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while (0)
