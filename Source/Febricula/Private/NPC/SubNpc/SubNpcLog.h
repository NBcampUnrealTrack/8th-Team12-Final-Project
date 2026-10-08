// [skydodo] 2026-10-08 작성
// 요약: 행인 로그 카테고리(LogSubNpc)와 crowd.Log 스위치 매크로
// Fill out your copyright notice in the Description page of Project Settings.
//
// 행인(군중) 로그 — 카테고리 LogSubNpc, 스위치는 콘솔 변수 crowd.Log.
//
// 군중 로그는 전부 LogSubNpc로 찍는다. 출력 로그 창의 Filters > Categories에서 LogSubNpc만 골라 볼 수 있고,
// 콘솔 `log LogSubNpc off`로 통째로 끌 수도 있다.
//
// 행인 수십 명이 각자 찍으면 출력 로그가 금방 가득 찬다. 그래서 자동으로 찍히는 군중 로그는
// 전부 SUBNPC_LOG로 쓰고, crowd.Log 1일 때만 남긴다 (기본은 꺼짐).
//   SUBNPC_LOG(Warning, TEXT("[Crowd] ..."), ...);   // UE_LOG(LogSubNpc, ...)와 같은 인자
//
// 콘솔 명령의 결과(crowd.Bench, crowd.BakeFarCrowd 등)처럼 직접 실행했을 때만 나오는 로그는
// UE_LOG(LogSubNpc, ...)로 바로 쓴다.

#pragma once

#include "CoreMinimal.h"

/** 군중(SubNpc) 로그 카테고리. SubNpcDirectorSubsystem.cpp에 정의돼 있다 */
DECLARE_LOG_CATEGORY_EXTERN(LogSubNpc, Log, All);

/** crowd.Log 값. SubNpcDirectorSubsystem.cpp에 정의돼 있다 */
extern int32 GSubNpcCrowdLog;

#define SUBNPC_LOG(Verbosity, Format, ...) \
	do \
	{ \
		if (GSubNpcCrowdLog != 0) \
		{ \
			UE_LOG(LogSubNpc, Verbosity, Format, ##__VA_ARGS__); \
		} \
	} while (0)
