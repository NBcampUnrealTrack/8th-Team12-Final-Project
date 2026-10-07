# 군중(행인) 성능 측정 — Mass 도입 전후

- 대상: 행인 (`ASubNpcCharacter` + `USubNpcDirectorSubsystem`), 브랜치 `feature/npc_upgrade`
- 1차 측정 (Mass 전): 2026-10-06, `Lvl_ThirdPerson`, PIE
- 2차 측정 (Mass 1·2단계 후): 2026-10-07, `Lvl_CrowdLODTest`, Standalone

## 1. 측정 방법

```
crowd.Bench <인원...> [secs=20] [warmup=5] [label=이름]
```

- 단계마다 인원을 즉시 맞추고 안정화 후 측정. 측정 동안 `t.MaxFPS 0`, `r.VSync 0`, `r.VSyncEditor 0`을 걸고 끝나면 되돌린다.
- 결과: 로그 `[CrowdBench]`, `Saved/Profiling/CrowdBench/Summary_*.csv`, 단계별 CSV 캡처 `Saved/Profiling/CSV/CrowdBench_*_N###.csv`
- 함께 찍히는 검사 로그: `[CrowdOverlap]`(몸 겹침), `[CrowdHeight]`(발이 바닥에서 뜸/묻힘), `[CrowdSpeed]`(걸음 속도 1.3배 초과)
- 군중 구간 표식 (Insights `Crowd_*`, CSV `Crowd/GameThread/*`): PathFollow, PathApply, MassPathFollow, GroundSnap, WorldBlock, Avoidance, BodySeparation, GroupLogic, LodSlice

관련 콘솔 변수

| 변수 | 내용 |
|---|---|
| `crowd.Mass 0/1` | 이동 계산을 Mass 프로세서에서 병렬로 (1단계) |
| `crowd.FarInstances 0/1` | 멀리 있는 행인을 인스턴스드 메시로 그림 (2단계) |
| `crowd.FarInstanceDistance` | 인스턴스로 바꾸는 거리 (cm, 기본 4000) |
| `crowd.Count N` / `-1` | 인원을 바로 N명으로 (측정 없이 볼 때) |

**주의**: 지금까지 모든 측정이 60 FPS(16.67ms)에 묶였다 (vsync를 꺼도 렌더 스레드가 16.6ms에서 대기). FPS가 아니라 스레드별 시간으로 판단한다.

## 2. 1차 측정 — Mass 전 (PIE, Lvl_ThirdPerson)

PIE라 게임 스레드에 에디터 UI(약 3.3~4.0ms)가 섞여 있다. CSV 중앙값.

| 인원 | 게임 스레드 | (UI 제외) | GPU | 애니메이션 워커 | 군중 로직 |
|---:|---:|---:|---:|---:|---:|
| 0 | 8.50 | 5.23 | 11.15 | 0.15 | 0.00 |
| 30 | 9.95 | 6.29 | 12.36 | 1.79 | 0.30 |
| 60 | 11.00 | 7.25 | 12.88 | 2.94 | 0.50 |
| 100 | 12.49 | 8.55 | 13.42 | 4.30 | 0.85 |

1명당 비용: 애니메이션 워커 약 42µs, 게임 스레드 약 33µs(그중 군중 로직 8.5µs), GPU 약 23µs.

결론: **군중 로직은 싸고(100명 0.85ms), 비싼 건 애니메이션과 렌더링이다.** Mass로 로직만 옮기면 이득이 작고, 멀리 있는 사람을 다른 방식으로 그려야 효과가 크다.

## 3. Mass 1단계 — 이동 계산 병렬화

`UMassSubNpcPathFollowProcessor`가 걷는 행인의 경로 이동 계산(`ComputePathStep`)을 워커 스레드에서 병렬로 하고, 적용(`ApplyPathStep`)만 게임 스레드에서 한다.

| (Lvl_ThirdPerson, Standalone, 약 65명) | Mass 끔 | Mass 켬 |
|---|---:|---:|
| 이동이 게임 스레드를 잡는 시간 | 0.55ms | 0.38ms (-31%) |
| 이동 계산 CPU 총합 (모든 스레드) | 0.55ms | 1.03ms |

게임 스레드 0.17ms 절약. 예상대로 작다 (1명당 계산이 몇 µs라 나누는 비용이 상대적으로 크다).

## 4. Mass 2단계 — 원경 표현 (인스턴스드 메시 + 텍스처로 구운 애니메이션)

`UMassSubNpcRepresentationProcessor`가 카메라에서 `crowd.FarInstanceDistance`보다 먼 행인의 스켈레탈 메시를 숨기고(애니메이션 계산 정지), AnimToTexture로 걷기·대기를 구운 인스턴스드 메시 한 장(`ASubNpcFarCrowdInstancer`)으로 대신 그린다. 걷는 중이면 걷기(걸음 속도에 맞춘 재생 속도), 서 있으면 대기.

### 테스트 레벨 `Lvl_CrowdLODTest` (Standalone, 기본 거리 40m)

기존 맵 옆에 122m × 14m 바닥과 119m 순환 경로 2개를 둔 레벨. 플레이어 시작점에서 0~120m에 행인이 퍼진다.

| CSV 중앙값 (ms) | 90명 끔 | 90명 켬 | 200명 끔 | 200명 켬 |
|---|---:|---:|---:|---:|
| 애니메이션 워커 | 3.74 | 2.25 (-40%) | 6.21 | **3.35 (-46%)** |
| 애니메이션 (게임 스레드) | 0.89 | 0.52 | 1.64 | **0.85** |
| EndOfFrameUpdates | 0.37 | 0.27 | 0.75 | 0.52 |
| 게임 스레드 전체 | 6.24 | 5.44 (-13%) | 8.01 | **6.91 (-14%)** |
| GPU | 5.90 | 6.08 | 6.28 | **7.76 (+1.5ms)** |

- 200명에서 애니메이션 계산이 거의 절반, 게임 스레드가 1.1ms 줄었다.
- **GPU는 1.5ms 늘었다.** 원경 메시(엔진 샘플 UE4 마네킹)에 LOD가 없고, 뼈대 애니메이션을 버텍스 셰이더에서 계산하기 때문으로 본다. 멀리 있는 스켈레탈 행인은 강제 LOD로 폴리곤이 적었다.
- 같은 측정에서 `[CrowdHeight]` 0건, `[CrowdSpeed]` 0건.

## 5-1. Mass 3단계: StateTree (행인 상태 결정)

테스트 레벨, 120명, 40초. 끔은 실행 인자 `-NoCrowdStateTree`(액터 코드가 결정), 켬은 `ST_Passerby`.

| 항목 | 끔 (액터 코드) | 켬 (Mass StateTree) |
|---|---|---|
| 트리가 시작된 행인 | 0 | 120 / 120 |
| 스팟·줄에 있는 인원 (평균 / 최대) | 0.20 / 3 | 0.16 / 3 |
| 군중 합계 (게임 스레드, ms/프레임) | 0.786 | 0.784 |
| 프레임 평균 / p99 (ms) | 16.67 / 18.08 | 16.67 / 17.91 |
| `[CrowdHeight]` / `[CrowdSpeed]` | 0 / 0 | 0 / 0 |

- 스팟에 들르는 정도가 비슷하다. 동작은 그대로 유지하면서 상태 결정만 StateTree로 옮겨졌다. 비용 차이는 측정 오차 안이다.
- 겹친 프레임은 28.7% / 10.5%로 차이가 컸다. 다만 StateTree는 이동·회피를 건드리지 않는다. 실행마다 나오는 편차로 본다.
- **고친 버그**: `FMassEntityManager::CreateEntity(프래그먼트 목록, 공유 값)`는 아키타입 구성에 공유 프래그먼트를 넣지 않는다. 그래서 엔진의 StateTree 활성화 프로세서 쿼리에 아무것도 걸리지 않았고, 이 프로세서는 실행 목록에서 잘려 나갔다. 결과적으로 트리가 한 번도 시작되지 않았다. 지금은 아키타입을 만들고 → `CreateEntity(아키타입, 공유 값)` → `SetEntityFragmentValues`로 만든다.
- 확인할 때는 트리가 시작된 행인 수(`FMassStateTreeActivatedTag`)와 걷기 태스크 실행 횟수를 임시 로그로 찍어 봤다 (지금은 뺐다).
## 5. 이번 작업에서 함께 고친 움직임 문제

| 증상 | 원인 | 결과 |
|---|---|---|
| 머리 위로 걷기 | 바닥 찾는 트레이스가 아래 행인 몸에 맞음 | 지형만 바닥으로 봄 |
| 하늘 걷기 / 공중에서 사라짐 | 가까운 사람만 바닥에 붙임 | 전원·등장 순간에도 붙임 |
| 언덕에서 허공 걷기 | 길로 돌아가는 사람이 앞쪽 목표점(오르막 위) 높이를 따라감 | 발밑 바닥에 붙임 → `[CrowdHeight]` 0건 |
| 이음매에서 1.4~1.6배 빨라짐 | 갈아탈 때 어긋남을 걸음과 무관하게 메움 + 일행 따라잡기 1.65배 | 실제 이동 1.25배 상한, 따라잡기 1.2배 → `[CrowdSpeed]` 0건 |
| 경로 C 끝에서 툭 사라짐 | 퇴장구가 보이는 곳 | 0.8초 디더 페이드 (`M_Passerby`, 행인 전용 머티리얼) |
| 순환 경로 반환점 정체 | 서로를 출구로 갖는 경로에서 앞뒤 판정이 엇갈림 | 경로 끝·시작 6m 안에서만 앞뒤 판정 |

## 6. 남은 일

1. **원경 GPU 비용**: 원경 메시에 LOD를 붙이거나 우리 Manny로 다시 구워 폴리곤을 줄인다. 거리별 인스턴스 컬링도 검토.
2. **겹침**: 200명에서 측정 프레임의 약 45%에 1~4쌍. 대부분 스팟 근처(스팟·줄로 가는 사람 ↔ 길로 돌아오는 사람·서 있는 사람). 원경 켬/끔과 무관.
3. **원경 메시 모양**: 엔진 샘플(UE4 마네킹)이라 Manny와 조금 다르다.
4. ~~StateTree~~: 완료 (5-1). 메인 맵 Lvl_ThirdPerson 스포너에도 ST_Passerby 지정 필요.
5. **60 FPS 상한**: 렌더 스레드가 16.6ms에서 대기한다. 상한을 풀어야 실제 여유를 잴 수 있다.

## 7. 원본

- 1차: `Saved/Profiling/CSV/CrowdBench_Base_1006_2336*~2338*`
- 2차: `Saved/Profiling/CSV/CrowdBench_LodTestFarOn_1007_043100_*`, `CrowdBench_LodTestFarOff_1007_043216_*`
- 3단계: `Saved/Logs/TreeOff_074932.log`, `TreeOn_074932.log`, `Saved/Profiling/CrowdBench/Summary_TreeO*_1007_07*.csv`
- `Saved/`는 git에 올라가지 않는다. 비교에 쓸 원본은 따로 보관한다.
