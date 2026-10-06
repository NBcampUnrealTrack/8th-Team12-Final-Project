# 행인 StateTree 만들기 (Mass 3단계)

행인이 "걷다가 언제 스팟에 들를지", "스팟·부딪힘 반응이 끝나면 무엇을 할지"를 Mass StateTree가 정한다.
실제 동작(경로 이동, 스팟 행동, 반응 애니메이션)은 지금처럼 행인 액터가 한다.
스포너에 StateTree를 지정하지 않으면 예전처럼 액터 코드가 스스로 정한다.

## 1. 에셋 만들기

1. 콘텐츠 브라우저에서 `/Game/NPC/SubNpc` 로 이동
2. 빈 곳 우클릭 → **인공지능(Artificial Intelligence) → StateTree**
3. 스키마(Schema) 선택 창에서 **Mass Behavior** (`MassStateTreeSchema`) 선택
4. 이름: `ST_Passerby`

## 2. 상태 구성

`ST_Passerby` 를 열고, **Root** 아래에 자식 상태 4개를 **이 순서대로** 만든다.
Root는 위에서부터 진입 조건이 맞는 첫 자식을 고른다.

| 순서 | 상태 이름 | 진입 조건 (Enter Conditions) | 태스크 (Tasks) | 전이 (Transitions) |
|---|---|---|---|---|
| 1 | React | **행인: 현재 상태가** → Activity = `Reacting` | **행인: 상태가 끝날 때까지 대기** → Activity = `Reacting` | On State Completed → **Root** |
| 2 | AtSpot | **행인: 현재 상태가** → Activity = `AtSpot` | **행인: 상태가 끝날 때까지 대기** → Activity = `AtSpot` | On State Completed → **Root** |
| 3 | Walk | **행인: 현재 상태가** → Activity = `Walking` | **행인: 걷기** → Check Distance = `150` | On State Completed → **Root** |
| 4 | Idle | (없음) | **행인: 상태가 끝날 때까지 대기** → Activity = `Pooled` | On State Completed → **Root** |

- **Walk**의 `Check Distance`: 이만큼(cm) 걸을 때마다 근처 스팟에 들를지 판정한다. 0이면 스팟에 안 들른다.
  스팟마다의 확률·빈자리·줄 서기는 예전 규칙 그대로다.
- **Idle**은 풀에 들어가 있는 행인용이다. 없으면 트리가 고를 상태가 없어 멈춘다.
- 태스크·조건 목록에서 이름이 "행인:"으로 시작하는 항목을 찾으면 된다.

다 만들었으면 툴바의 **Compile** → **Save**.

## 3. 스포너에 지정

1. 레벨의 `NpcCrowdSpawner` 선택 (테스트 레벨 `Lvl_CrowdLODTest`, 필요하면 `Lvl_ThirdPerson`도)
2. 디테일 → **Crowd | Behavior → Crowd State Tree** = `ST_Passerby`
3. 레벨 저장 (Ctrl+S)

## 4. 확인

- 플레이하면 출력 로그에 `[Crowd] 행인 상태 결정: Mass StateTree (ST_Passerby)` 가 찍힌다.
  `액터 코드 (StateTree 없음)` 이 찍히면 스포너 지정이 안 된 것이다.
- 행인이 예전처럼 스팟에 들르고, 부딪히면 반응한 뒤 다시 걷는지 본다.
- 스팟에 아무도 안 들르면: Walk 상태에 **행인: 걷기** 태스크가 있는지, Check Distance가 0이 아닌지 확인한다.

## 동작 구조 (참고)

```
UMassSubNpcBehaviorSignalProcessor ── 상태가 바뀌었을 때 / 걷는 중 0.25초마다 ──▶ 신호
        │
        ▼
UMassStateTreeProcessor (엔진) ── 신호 받은 행인만 트리 실행
        │
        ├─ 행인: 걷기 ── 150cm마다 RequestSpotRoll() ──▶ 액터 틱(게임 스레드)에서 RollForActivitySpot()
        ├─ 행인: 상태가 끝날 때까지 대기 ── 스팟·반응이 끝나면 Root로
        └─ 행인: 현재 상태가 ── Root가 다음 상태를 고르는 조건
```

- Mass StateTree 태스크는 게임 스레드가 아닌 곳에서 돌 수 있다. 그래서 태스크는 행인 상태를 읽기만 하고,
  스팟 점유처럼 월드를 바꾸는 일은 액터에 요청만 남긴다 (`RequestSpotRoll`, 스레드 안전).
- 신호가 올 때만 실행되므로 행인 200명이어도 평소 비용이 거의 없다.
