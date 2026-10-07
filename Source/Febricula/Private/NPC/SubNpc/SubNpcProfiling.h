// Fill out your copyright notice in the Description page of Project Settings.
//
// 군중 성능 측정용 구간 표식.
//
// SUBNPC_PROFILE_SCOPE(구간) 하나로 세 군데에 동시에 기록한다.
//   1) Unreal Insights (-trace=cpu)    : "Crowd_구간" 이름으로 타임라인에 보인다
//   2) CSV 프로파일러 (csvprofile)      : Crowd/구간 열로 프레임마다 합계가 남는다
//   3) crowd.Bench 요약                 : 측정 중일 때만 누적해서 인원별 평균 ms를 낸다
//
// Mass로 옮기기 전후를 같은 이름으로 비교하려고 구간 이름을 고정해 둔다.
// 클래스 레이아웃을 건드리지 않도록 private 헤더에 전역으로 둔다 (Live Coding 안전).

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(Crowd);

namespace SubNpcProfiling
{
	enum class ESection : uint8
	{
		PathFollow,       // 행인 1명의 경로 이동 계산 (아래 두 개 포함). Mass면 워커 스레드들의 합계(CPU 시간)
		PathApply,        // 계산 결과 적용 (위치 설정·경로 갈아타기·스팟 판정, 게임 스레드)
		MassPathFollow,   // Mass 프로세서 전체가 게임 스레드를 붙잡은 시간 (병렬 계산 + 적용)
		GroundSnap,       // 지면 스냅 라인 트레이스
		WorldBlock,       // 벽 캡슐 스윕
		Avoidance,        // 속도 회피 (0.1초마다)
		BodySeparation,   // 몸 겹침 금지 (매 프레임)
		GroupLogic,       // 일행 대화·대열·길 밖 양보 (0.1초마다)
		LodSlice,         // LOD 재계산
		Count
	};

	const TCHAR* GetSectionName(ESection Section);

	/** crowd.Bench가 측정 구간에 있을 때만 true. 평소에는 누적 비용이 없다 */
	extern bool bRecording;

	/** 측정 시작 후 구간별 누적 사이클 */
	extern int64 SectionCycles[static_cast<int32>(ESection::Count)];

	struct FScope
	{
		explicit FScope(ESection InSection)
			: Section(InSection)
			, StartCycles(bRecording ? FPlatformTime::Cycles64() : 0)
		{
		}

		~FScope()
		{
			if (StartCycles != 0)
			{
				// 워커 스레드에서도 불리므로(Mass 병렬 계산) 원자적으로 더한다.
				FPlatformAtomics::InterlockedAdd(&SectionCycles[static_cast<int32>(Section)], static_cast<int64>(FPlatformTime::Cycles64() - StartCycles));
			}
		}

		ESection Section;
		uint64 StartCycles;
	};
}

#define SUBNPC_PROFILE_SCOPE(SectionName) \
	TRACE_CPUPROFILER_EVENT_SCOPE(Crowd_##SectionName); \
	CSV_SCOPED_TIMING_STAT(Crowd, SectionName); \
	SubNpcProfiling::FScope UE_JOIN(SubNpcProfileScope_, __LINE__)(SubNpcProfiling::ESection::SectionName)
