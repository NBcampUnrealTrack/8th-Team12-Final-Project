# BurgerHouse — 손님 NPC 시스템

버거 가게 손님이 **들어와서 → 주문하고 → 기다리다 → 받아서 → 앉아 먹고 → 나가는**
루프. 레벨 하나(`L_BurgerHouse`)와 그것을 돌리는 C++ · StateTree 로 되어 있다.

---

## 1. 무엇으로 되어 있나

```
Source/Febricula/BurgerHouse/
  CustomerCharacter       손님 폰 — 행동 재생 · 착석 · 트레이
  CustomerAIController    StateTree 를 들고 있다
  CustomerSpawner         손님을 낳고 개체 수를 유지한다
  StoreSubsystem          주문 · 대기열 · 슬롯 예약의 **소유자**
  SlotPoint               좌석 · 키오스크 · 픽업 · 문 (레벨에 43개)
  BurgerHouseStateTreeTasks   태스크 11 + 조건 2
  BurgerHouseTags         이벤트 태그 4

Content/BurgerHouse/
  Maps/L_BurgerHouse      레벨
  AI/BP_Customer · BP_CustomerAI · ST_Customer
  Anims/Mixamo/ABP_Customer + 애님 21 (Eating 4 포함)
  Anims/Montages/ 몽타주 11
  Anims/Eat_Animation_Pack/ 원본 + 리타겟 설정
  Props · Materials · Shell · Textures
```

### 설계의 요점 — 주문은 매장의 상태다

**주문 번호를 손님이 들고 있지 않는다.** NPC 가 번호를 들면 표시기가 NPC 를 뒤져야
하고, NPC 가 사라지면 주문이 유령이 된다. 그래서 주문·대기열·슬롯 예약을
`UStoreSubsystem` 이 전부 소유하고, 손님과 표시기는 서로를 모른 채 그것만 본다.

### 손님 루프

```
Enter → Kiosk → Wait → Pickup → Seat → Leave
                 ↑
        Order.Ready 이벤트로 전이한다 — 조건 폴링이 아니라
        StoreSubsystem 이 담당 손님 한 명에게만 민다
```

`ST_Customer` 는 상태 32 · 태스크 39 · 전이 29 로 완성돼 있다.

⚠ 「한 칸 당기기」도 이벤트다(`Queue.Advanced`). `Move To` 의 `Destination` 바인딩은
상태에 **들어갈 때 한 번** 복사되므로, 값만 바꾸면 아무도 안 움직인다.

---

## 2. 쓰는 법

### 레벨에 필요한 것

| | |
|---|---|
| `SlotPoint` 액터 | 좌석 · 키오스크 · 픽업 · `Entry` · `Exit`. `ll_slots.py` 가 깐다 |
| `CustomerSpawner` 액터 하나 | 손님을 낳는다 |
| `NavMeshBoundsVolume` | **스포너 스플라인과 홀 전체를 덮어야 한다** |

⚠⚠ **내비를 안 구우면 스폰이 0 이다.** 그런데 증상은 「스포너가 고장났다」로
보인다. 에디터에서 `Build > Build Paths`. 커맨드릿에서는 안 구워진다.

### ⚠ 좌석이 바라보는 방향 — 가구마다 다르다

UE 캐릭터 정면은 **+X** 인데 좌석 슬롯은 **가구 회전을 그대로 물려받는다.**
가구 정면이 +X 가 아니면 손님이 옆을 보고 앉는다. 그래서 `SlotPoint` 에
`UseYawOffset` 이 있고 `GetUseTransform()` 이 yaw 에 더한다.

| 에셋 | 가구 정면 | `UseYawOffset` |
|---|---|---|
| `Chair_A` (29석) | 로컬 **+Y** | **90** |
| `Sofa_Unit` (8석) | 로컬 **+X** | **0** |
| 키오스크 · 픽업 · 문 | — | **0** (스크립트가 UE yaw 를 직접 적는다) |

⚠⚠ **의자만 보고 「좌석은 전부 90」 이라고 단정하지 말 것.** 표본 12개가 우연히
전부 `Chair_A` 였던 탓에 한 번 틀렸다. 그래서 `ll_slots.py` 의
`USE_YAW_OFFSET` 은 **종류가 아니라 에셋 이름**으로 건다 — 새 좌석 가구를
넣으면 항목을 추가할 것.

⚠⚠ **화면이 90도 틀어져 보이면 좌석이 아니라 `CharacterMesh0` 을 먼저 볼 것**
(§6). 메시 컴포넌트 yaw 가 0 이면 손님 전원이 90도 옆을 보는데, 그 상태에서
좌석 값을 맞추면 오히려 나빠진다.

검산은 「테이블 중심과의 각도」가 아니라 **정면으로 뻗은 선이 테이블 모듈을
지나는지**로 한다. 긴 테이블에 붙은 소파는 정면이 상판 모서리를 향하고 중심은
옆으로 치우치는 게 정상이라, 각도로 재면 멀쩡한 좌석이 전부 틀렸다고 나온다.
현재 37석 중 34석이 정면 25cm 안에 테이블이 있고, 나머지 3석(`Seat_30`·`34`·
`35`)은 테이블 모듈 **사이 틈**에 앉아 중심이 33cm 옆이다 — 모듈 폭이 40cm 라
상판 모서리는 13cm 앞이고, 다른 각도는 전부 더 나쁘다. 회전 문제가 아니다.

### 스포너 설정 (현재 `L_BurgerHouse` 값)

```
NPCClass            BP_Customer
FixedDesiredAlive   8      ← 동시에 살아 있을 손님 수
HardMaxAlive        12
FirstSpawnDelay     60s    ← ±30s. **첫 워크인까지** 기다린다 (30~90초)
SpawnInterval       240s   ← ±30s 로 흔들린다 (210~270초)
SpawnBurstWeights   {1.0}  ← 한 번에 **항상 1명** (예전 1명 80% · 2명 20%)
SampleSpacing       40     ← 홀이 6×11m 라 촘촘하게
LateralWidth        60
PrefillOnBeginPlay  true
PrefillPlacements   5명    ← 레벨이 열릴 때부터 앉아 있는 손님
```

⚠ `FixedDesiredAlive` 를 프리필 인원으로 다 채우면 **문으로 들어오는 손님이 영영
안 생긴다.** 정원의 절반쯤을 프리필로 두는 것이 적당하다.

#### ⚠⚠ `BeginPlay` 가 첫 워크인을 **즉시** 뽑고 있었다

```cpp
// 예전
if (bPrefillOnBeginPlay) { Prefill(); }
TrySpawn();                 // ← 바로. 문이 열리는 순간 이미 걸어 들어오고 있다
```

프리필은 「원래 있던 사람」이라 즉시가 맞지만 워크인은 아니다. `FirstSpawnDelay`
(+`FirstSpawnDelayJitter`)를 넣어 첫 회만 늦춘다. **0 이면 예전 동작**이다.

⚠ `SpawnInterval` 과 같게 두면 「첫 손님도 남들과 같은 리듬」이 된다. 개발 중에는
  짧게 두는 편이 확인이 빠르다.

#### 주기를 바꿀 때 — **지터도 같이** 바꾼다

헤더가 적어 놓은 대로 **고정 주기는 사람이 아니라 메트로놈으로 보인다.** 원래
비율은 40초에 ±5 = **±12.5%** 였다. 주기만 240 으로 올리고 지터를 5 로 두면
±2% 라 사실상 고정 주기가 된다. 그래서 ±30 으로 같이 올렸다.

현재 값의 뜻 —

```
도착 1회당 인원 기대값 1.2명   (SpawnBurstWeights {0.8, 0.2} = 1명 80% · 2명 20%)
240초마다 1회                  -> 이론상 시간당 15회 · 18명
```

⚠ 실제로는 **정원에 걸려 그만큼 안 온다.** 정원 8명 중 프리필 5명이 영구
  장식이라 워크인 자리는 **3개**뿐이다. 빈 자리 3개를 채우는 데 평균 10분이
  걸리고, 손님이 나간 자리도 **최대 4분 뒤에야** 다시 찬다.

⚠ 손님 수를 늘리고 싶은 것이라면 주기가 아니라 `FixedDesiredAlive` 를 볼 것.
  주기는 「얼마나 자주 문이 열리나」이고, 정원은 「몇 명까지 있나」다.

### 장식 손님의 몸짓 — 한 사람은 **하나만** 돌린다

앉은 장식 손님은 `Sit_Idle` 의 몸짓 **6종 중 하나**를 골라 그것만 계속
반복한다 (`bDecorationSingleAction`, 기본 켬). 구성은 §7-1 참고.

⚠ 예전에는 반복할 때마다 새로 뽑아서 한 사람이 「버거 → 폰 → 감자튀김」으로
계속 바뀌었다. 앉아서 밥 먹는 사람은 그러지 않는다. **다양성은 한 사람 안이
아니라 손님들 사이에서** 나와야 한다.

#### ⚠⚠ 루프 이음매 — **애님을 자르려 들지 말 것**

증상은 「몽타주 첫·끝 프레임이 안 맞아 툭 끊긴다」로 보인다. 그런데 실측하면
원인이 다르다.

**① 자를 지점이 없다.** 애님 전체를 훑어 첫 프레임과 가장 비슷한 시점을 찾아도
지금 끝과 거의 차이가 없다 — 먹는 애님은 **순환 동작이 아니라 한 번짜리
연기**라 시작 자세로 돌아오는 순간이 아예 없다.

| 애님 | 지금 끝 차이 | 가장 좋은 지점 |
|---|---|---|
| `Eating_a_Burger` | 186.6 | 6.07s 에서 **180.2** |
| `Eating_Fries` | 124.4 | 7.36s 에서 **122.4** |
| `Eating_a_Drumstick` | 60.6 | 4.89s 에서 48.1 |
| `Eating_Frantically` | 42.7 | 없음 (끝이 최선) |

**② 더 큰 원인은 따로 있다.** 몽타주를 끝까지 재생하면 `Auto Blend Out` 이
**기본 포즈(앉은 Idle)로 되돌린다.** 그 다음 재생이 거기서 블렌드인하므로 —

```
먹는 중 → (BlendOut 0.35) 앉은 Idle → (BlendIn 0.35) 먹는 중
```

즉 손님이 8초마다 **버거를 내려놨다 다시 든다.** 프레임 차이보다 이게 훨씬
눈에 띈다.

→ **끝나기 전에 겹쳐 건다** (`DecorationLoopCrossfade`, 기본 0.35). 기본 포즈를
안 거치고 끝 자세 → 시작 자세로 직접 블렌드된다. 팔뚝이 최대 68도 차이인데
0.35초면 초당 195도 — 사람 팔 속도라 튀는 게 아니라 동작으로 보인다.

⚠ 크로스페이드 길이는 **들어오는 몽타주의 `Blend In`** 이 정한다. 둘을 같이
맞춰야 한다 (`ll_montages.py` 의 `BLEND`, 현재 0.35).

⚠ 「몽타주를 길게 만들기」도 답이 아니다. 튀는 빈도만 줄고 원인은 그대로다.

⚠ `DecorationGap` 은 이제 **루프 사이 쉬는 시간이 아니라 첫 재생 지연**이다
(기본 3초). 장식 손님이 전부 같은 프레임에 태어나므로, 안 흩으면 8초마다
**전원이 동시에 같은 동작**을 해서 군무처럼 보인다.

#### 어느 몸짓을 받을지 — **자리에서 결정적으로** 뽑는다

```
해시(좌석 번호 · DecorationSeed) → 가중 선택 → 그 변형만 반복
```

| | |
|---|---|
| 같은 의자 | **늘 같은 몸짓** — PIE 를 다시 켜도 그림이 같다 |
| 옆자리 | 해시로 흩어져 **안 겹친다** |
| 다시 섞고 싶으면 | `DecorationSeed` **하나만** 바꾼다 |

⚠ 좌석 번호를 그대로 `% 4` 하면 안 된다 — 번호가 연속이라 「버거·감자·버거·
감자」가 줄줄이 나온다. 그래서 해시를 한 번 통과시킨다.

⚠ 가중치(`Weight`)는 그대로 존중된다. 난수 자리에 결정적인 값이 들어갈 뿐이다.

##### 왜 NPC 마다 `InitialSeed` 를 두지 않았나

프리필 손님은 PIE 마다 새로 태어난다. NPC 씨앗으로 하면 재현하려고 **씨앗을
스포너의 배치 목록에 저장**해야 하고, 자리를 옮길 때 씨앗도 같이 옮겨야 한다.
자리에서 뽑으면 저장할 것이 없고, 레벨만 봐도 결과를 알 수 있다.

⚠ 줄에 선 장식 손님은 슬롯이 없어서 **줄 순번**을 자리 대신 쓴다.

### 프리필 손님은 **장식이다** — 트리를 안 탄다

```
CustomerSpawner > Prefill > bPrefillRunsAI = false   ← 기본값
```

프리필로 태어난 손님에게는 **AI 컨트롤러를 안 붙인다.** 컨트롤러가 없으면
StateTree 가 시작조차 안 하므로 `Move To` · `Leave` · `Despawn` 이 **구조적으로
불가능하다.** 자리에 영원히 남는다.

대신 `ACustomerCharacter` 가 역할에 맞는 몸짓을 끝없이 돌린다:

```
DecorationActions      Eating -> Sit_Idle · Waiting -> Idle_Look · Ordering -> Idle_Look
DecorationGap  2.5s    몸짓 사이에 0~2.5초를 무작위로 쉰다
```

⚠ 몸짓은 `PlayAction` 이 **끝날 때마다 변형을 새로 뽑는다** — 같은 행동 이름을
반복해도 몸짓이 계속 바뀐다. 쉬는 시간까지 흔들어서 여럿이 앉아 있어도 동시에
안 바뀐다.

⚠ **장식 손님도 슬롯은 예약한다.** 그래서 걸어 들어온 손님이 그 의자에 겹쳐
앉지 않는다. 대신 그만큼 자리가 영구히 준다 — `FixedDesiredAlive` 를 프리필
인원보다 넉넉히 둘 것.

⚠ 장식 `Waiting` 손님은 **주문을 넣지 않는다.** 넣으면 아무도 안 찾아가서
12초 뒤 `Ready` 가 되어 픽업대에 영원히 남는다.

⚠ 장식 `Waiting` 손님은 **줄 자리로 옮겨 주는 코드가 없다** (그건 트리가 하던
일이다). `PrefillPlacements` 에 줄 좌표를 직접 적어야 한다.

`bPrefillRunsAI` 를 켜면 프리필도 트리를 타서 먹고 나가는 한 바퀴를 돈다.

### 다른 곳에서 손님을 낳고 싶다면

스포너를 안 쓰고 직접 스폰할 때는 **이 순서로 불러야** 일행·프리필이 동작한다
(둘 다 `BlueprintCallable`):

```cpp
Customer->OnAssignedSpawnRole(TEXT("Eating"));   // 먼저. 프리필일 때만
Customer->OnSpawnedInGroup(GroupId, GroupSize, IndexInGroup);
```

⚠ **순서가 뒤집히면 일행이 같은 테이블에 안 앉는다** — 좌석 배정이 `PartyId` 를
보기 때문이다. 안 부르면 조용히 전원이 「혼자 온 손님」이 된다.

⚠ **AI 컨트롤러는 통보가 끝난 뒤에 붙인다.** `SpawnDefaultController()` 가
초기 상태 선택을 그 자리에서 동기로 끝내기 때문에, 먼저 붙이면 트리가 **역할이
비어 있는 상태로** 갈래를 고르고 프리필 손님 전원이 기본 갈래로 떨어진다.

---

## 3. PIE 콘솔 명령

| 명령 | 하는 일 |
|---|---|
| **`BurgerHouseReady 0`** | **조리중인 주문 전부 즉시 준비** — 안 쓰면 매번 12초 기다린다 |
| `BurgerHouseDump` | 주문 · 대기열 · 슬롯 현황 |
| `BurgerHouseSlots` | 슬롯 다시 훑고 종류별 빈 개수 |
| `BurgerHouseOrder` | 손님 없이 주문 1건 발행 |
| `BurgerHouseClaim Seat` | 좌석 하나 예약 (경합 확인용) |
| `BurgerHouseReleaseAll` | 전부 회수 |

---

## 4. ⚠ 프로젝트 공용 설정을 건드렸다

`Config/DefaultEngine.ini` 의 **내비게이션 에이전트**를 손님 캡슐에 맞췄다.

```
AgentRadius 34 · AgentHeight 176      (ACharacter 기본 캡슐: 반지름 34 · 반높이 88)
```

⚠⚠ **다른 NPC 의 내비에도 영향을 준다.** 다른 캐릭터 캡슐과 맞는지 확인할 것.

이 값이 없으면 엔진이 하드코딩된 35/144 로 내비를 굽고, 176 짜리 손님이 144 용
내비 위를 걷는다. 그리고 **에이전트 크기를 실제로 정하는 건 `SupportedAgents`**
이지 `RecastNavMesh` 블록이 아니다 — 되읽어 확인했다.

`Febricula.uproject` 에 `PythonScriptPlugin` · `EditorScriptingUtilities` 를 켰다
(아래 스크립트용).

---

## 5. 스크립트

에디터를 **끄고** 커맨드릿으로:

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "C:\Project\Unreal\Febricula.uproject" -run=pythonscript `
  -script="C:/Project/Unreal/Import/Scripts/<이름>.py" -unattended -nopause -nosplash
```

에디터가 **켜져 있으면** `Window > Output Log` → 드롭다운을 `Python` 으로:

```python
exec(open(r'C:/Project/Unreal/Import/Scripts/<이름>.py').read())
```

| 스크립트 | 무엇 |
|---|---|
| `ll_slots.py` | 좌석·키오스크·픽업·문 슬롯을 배치한다 (`LL_placement.json` 을 읽는다) |
| `ll_seat_map.py` | 어느 좌석이 어느 테이블인지 — 일행이 같이 앉는지 확인 |
| `ll_nav.py` | NavMesh 바운즈를 깐다 |
| `ll_nav_check.py` | 내비가 실제로 홀을 덮는지 **숫자로** 확인 |
| `ll_st_check.py` | `ST_Customer` · `BP_Customer` · `BP_CustomerAI` 배선 점검 |
| `ll_st_dump.py` | 트리를 통째로 덤프 — 상태 32 · 태스크 39 · 전이 29 가 기준 |
| `ll_montages.py` | 몽타주를 만들고 `BP_Customer > Actions` 에 꽂는다 |
| `ll_eat_retarget.py` | Eat 팩 애님을 손님 스켈레톤으로 리타겟한다 |
| `ll_walk_retarget.py` | 마네퀸 `A_WalkFwd_Loop` 를 손님 스켈레톤으로 리타겟 (§9-1) |
| `ll_sit_retarget.py` | 마네퀸 `SitIdle_Male` 을 손님 스켈레톤으로 리타겟 (§7-1) |
| `ll_sit_verify.py` | 프리필 idle 교체 검산 — **새 프로세스**로 돌릴 것 |
| `ll_sitidle_probe.py` | 교체 전 실측 — 후보 3벌·Actions·프리필 역할 |
| `ll_sit_candidates.py` | 앉은 몸짓 후보를 전부 재서 골반 높이·이음매를 찍는다 |
| `ll_sit_loop_check.py` | 장식 루프가 몇 초마다 도는지 (몽타주가 아니라 C++ 타이머다) |
| `ll_bp_save_probe.py` | 블루프린트가 안 써질 때 저장 경로를 하나씩 시험 (§6) |
| `ll_prefill_ai_probe.py` | 프리필에 AI 가 붙는지 — `Auto Possess AI` 진단 (§6) |
| `ll_spawn_interval.py` | 워크인 스폰 주기·지터를 바꾼다 (§2) |
| `ll_spawn_interval_verify.py` | 위의 검산 — **새 프로세스**로 돌릴 것 |
| `ll_order_sound.py` | 주문 알림음 태스크를 `K_Order` 에 얹는다 (§9-3) |
| `ll_order_sound_level.py` | 레벨의 소리 액터에 태그 + Auto Activate 끄기 |
| `ll_order_sound_verify.py` | 위 둘의 검산 — **새 프로세스** |
| `ll_walkin_tuning.py` | 워크인 이동속도 · 키오스크 회전 (§9-4) |
| `ll_walkin_tuning_verify.py` | 위의 검산 — **새 프로세스** |
| `ll_fix_loco_rootmotion.py` | 손님 애님의 루트모션 플래그를 전부 끈다 (§9-2) |
| `ll_rootmotion_claim_probe.py` | 루트모션·`ClaimedBy` 진단 (안 고침) |
| `ll_remove_door_wait.py` | 문 앞 대기 제거 (§9-4) |
| `ll_three_issues_verify.py` | 문 앞 대기·이동·회전 검산 — **새 프로세스** |
| `ll_walkin_pacing.py` | 첫 워크인 지연 · 1명 고정 · 도착 정밀도 · 알림음 볼륨 |
| `ll_walkin_pacing_verify.py` | 위의 검산 — **새 프로세스** |
| `ll_food_import.py` | 버거박스·감자·차임을 Downloads 에서 들여온다 (§9-6) |
| `ll_tray_setup.py` | 트레이 구성 · 양손 들기 · 주문 알림음 배선 |
| `ll_tray_verify.py` | 위의 검산 — **새 프로세스** |
| `ll_counter_tray.py` | 카운터 트레이 BP 생성 + 픽업대 배치 |
| `ll_fix_event_tags.py` | 트리의 이벤트 태그 접두사를 고친다 (§9-7) |
| `ll_fixes_round2.py` | 알림음 볼륨 · 도착 판정 · 음식 텍스처 · 영수증 (§9-9) |
| `ll_food_materials.py` | `M_BurgerBox`·`M_Fries` 를 직접 만들어 텍스처를 물린다 (§9-8) |
| `ll_tray_layout.py` | 트레이 위 배치를 **바운즈로 역산** + 카운터 트레이 회전 (§9-8) |
| `ll_fixes_round3.py` | 방향·음식 크기·알림음·`Telling_A_Secret` 제거 (§9-10~13) |
| `ll_fixes_round3_verify.py` | 위의 검산 — **새 프로세스** |
| `ll_fixes_round4.py` | 영수증 세로 3배 · 알림음 0.02 · 트레이 위치 확인 (§9-14~16) |
| `ll_anim_map.py` | chime 통일 + **어떤 상황에 어떤 애님이 도는지** 네 층을 한 번에 (§9-18) |
| `ll_manny_retarget.py` | Mixamo -> 마네퀸 리타게터 + 애님 47개 일괄 변환 (§9-19) |
| `ll_manny_verify.py` | **원본 대비 보존율**로 리타겟 품질 검산 — 새 프로세스 |
| `ll_manny_swap.py` | `BP_Customer` 의 메시·ABP·소켓·Actions 교체 |
| `ll_manny_abp_fix.py` | ABP 그래프의 Sequence Player 를 새 애님으로 |
| `ll_manny_swap_verify.py` | 위 전부의 검산 — **새 프로세스** |
| `ll_tray_hands.py` | `Carrying` 의 양손 중점을 실측해 `TrayHandOffset` 역산 (§9-21) |
| `ll_fixes_round5.py` | 영수증 비율 · 키오스크 몸짓 2배속 3회 (§9-22) |
| `ll_table_tray.py` | 2인용 테이블 트레이 + `L_Gone` 디스폰 태스크 (§9-23·24) |
| `ll_arms_in.py` | 리타겟 포즈로 팔 오므리기 + 시퀀스 재굽기 (§9-25) |
| `ll_prefill_nudge.py` · `ll_prefill_nudge_seat.py` | 벽쪽 프리필의 **스폰 지점과 좌석**을 같이 옮긴다 (§9-26) |
| `ll_fries_set_import.py` | 감자튀김 세트 4개를 셸에서 **떼어내** 개별 메시로 (§9-27) |
| `ll_fries_set_layout.py` | 그 넷 + 버거박스 + 영수증을 트레이에 배치 (§9-27) |
| `ll_burger_held_import.py` | 손에 드는 버거 메시·텍스처·머티리얼 (§9-32) |
| `ll_eat_duration.py` | 착석 체류 시간을 1~3분으로 (§9-33) |
| `ll_phone_import.py` | 쇼케이스 FBX 에서 폰 **한 대**를 골라낸다 (§9-34) |
| `ll_phone_hand.py` | `Talking_On_A_Cell_Phone` 의 귀 위치로 `PhoneHandOffset` 역산 |
| `ll_player_speed_probe.py` | 플레이어 폰 속도 · 레벨별 게임모드 · 입력 에셋 (안 고침) |
| `ll_sync_player_speed.py` | 손님 속도를 플레이어 걷기에서 **읽어** 맞춘다 |
| `ll_burgerhouse_speed_350.py` | 버거집 폰·NPC 를 350 으로 고정 (§9-4) |
| `ll_burgerhouse_speed_verify.py` · `ll_sync_sprint_verify.py` | 검산 — **새 프로세스** |
| `ll_idle_survey.py` | 프로젝트 전체에서 **실제로 쓰이는** idle 을 훑는다 |
| `ll_walk_apply.py` | Walk 상태 애님 교체 + `MaxWalkSpeed` 를 애님에 맞춤 |
| `ll_walk_verify.py` | 위 둘의 검산 — **새 프로세스**로 돌릴 것 |
| `ll_walk_pose_check.py` | 리타겟 자세(A포즈/T포즈 정렬)를 기존 애님과 견준다 |
| `ll_walk_probe.py` · `ll_walk_stride.py` | 교체 전 실측 (아무것도 안 고친다) |
| `ll_walk_ops.py` | 리타게터의 op 설정을 통째로 덤프한다 — 어느 손잡이가 있는지 문서로는 안 나온다 |

---

## 6. ⚠ 조용히 실패하는 곳 — 여기만 조심하면 된다

| 빠뜨리면 | 증상 | 오독하기 쉬운 것 |
|---|---|---|
| NavMesh 굽기 | 스폰 **0** · `Move To` 실패 | 「스포너가 고장났나」 |
| `Auto Possess AI` | StateTree 가 **시작조차 안 함** | 「애님이 없어서 안 움직이나」 |
| 통보보다 컨트롤러를 먼저 붙임 | 프리필 전원이 기본 갈래로 | 「트리 자식 순서를 잘못 놨나」 |
| `Auto Possess AI` 를 **켜 둠** | 위와 같은 증상 — C++ 만 봐서는 안 보인다 | 아래 참고 |
| `PrefillRoles` 철자 오타 | 프리필이 **전부** 기본 갈래로 | 위와 증상이 같다 |
| 루프 상태에 「앉는 동작」을 씀 | **섰다 앉았다 반복** | 「전이가 덜덜 떤다」 (§7) |
| 트리의 액션 이름 오타 | 그 상태만 **4초 폴백** | 「몽타주가 없나」 (§8-1) |
| 레벨 액터 프로퍼티만 고치고 저장 | **아무 일도 안 일어남** | 아래 참고 |
| 블루프린트를 **컴파일한 뒤** 저장 | **파일이 안 써짐** (되읽기는 통과) | 아래 참고 |
| 메시 컴포넌트 yaw 가 0 | 손님이 **전부 90도 옆을 봄** | 「좌석 각도가 틀렸나」 (아래) |
| 리타게터에 op 이 0개 | **안 움직이는 애님**이 나옴 | 「스켈레톤이 틀렸나」 (§7) |
| 에디터를 켠 채 커맨드릿 | 덮어쓰기 대신 **`이름1`** 이 생김 · **저장이 전부 `False`** | 「스크립트가 두 번 돌았나」 · 「API 가 망가졌나」 |

열 가지 다 **에러가 안 난다.** 그래서 표로 박아 둔다.

### ⚠⚠ `CharacterMesh0` 의 relative yaw 는 **−90** 이다

UE `ACharacter` 규약 — 스켈레탈 메시는 자기 공간에서 **+Y** 를 보고 액터는
**+X** 를 본다. 그래서 메시 컴포넌트는 `(0, −90, 0)` 으로 돌려 놓는다.

`BP_Customer` 는 **위치(Z −90)만 들어가 있고 회전이 0 이었다.** 그러면 모든
손님이 액터 정면에서 **90도 왼쪽**을 본다.

실측(레퍼런스 포즈, 허벅지·어깨 둘 다 일치) — 왼→오른 벡터가 메시 −X 이므로
정면은 **+Y**, 따라서 컴포넌트 yaw 는 **−90**.

⚠⚠ **이게 좌석 각도 문제의 진짜 원인이었다.** 90도가 통째로 밀려 있으니
「의자는 맞고 소파만 틀렸다」처럼 보였고, 슬롯 쪽 `UseYawOffset` 을 맞출수록
오히려 화면이 나빠졌다. **좌석을 의심하기 전에 이 값을 먼저 볼 것.**

⚠ 걷는 것도 같이 틀어져 있었다 — `bOrientRotationToMovement=false` ·
`bUseControllerRotationYaw=true` 라 액터는 진행 방향을 보는데 몸만 90도 돌아가
**옆걸음**이었다. 애님이 정상이라 눈치채기 어렵다.

### ⚠⚠ 커맨드릿에서 레벨 저장 — `True` 를 믿지 말 것

액터 프로퍼티만 `set_editor_property` 로 고치면 **레벨 패키지가 dirty 로 안
잡힌다.** 그 상태로 부르면 —

```python
les.save_current_level()          # -> True  (그런데 파일은 안 써진다)
EditorAssetLibrary.save_asset(맵) # -> False
```

`True` 가 돌아오고 같은 프로세스에서 되읽으면 값도 맞게 나오니 **완전히 성공한
것처럼 보인다.** 프로세스를 새로 띄우면 원래 값이다. 이것 때문에 좌석 각도
수정이 두 번 헛돌았다.

```python
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False)
```

`only_dirty=False` 로 **dirty 플래그를 무시**하게 해야 실제로 써진다.
(`Package` 객체에는 `is_dirty()`·`mark_package_dirty()` 가 노출돼 있지 않다.)

⚠ 액터를 **스폰**할 때는 이 문제가 없다 — 스폰이 레벨을 dirty 로 만든다.
그래서 `ll_slots.py` 는 `save_current_level()` 로도 잘 돌아갔다.

⚠ 검산은 **반드시 새 프로세스**에서. 같은 실행 안에서의 되읽기는 무의미하다.

### ⚠⚠ `Auto Possess AI` 가 C++ 의 방어를 **통째로 무효화한다**

`CustomerSpawner::SpawnOne()` 은 컨트롤러를 **일부러 안 붙인다.** 붙이면
`SpawnDefaultController() → OnPossess → StartLogic()` 이 **초기 상태 선택을 그
자리에서 동기로** 끝내는데, 역할·일행 통보는 무리가 다 태어난 뒤에야 옳기
때문이다. 그 주석이 `CustomerSpawner.cpp:167` 에 길게 적혀 있다.

**그런데 `BP_Customer > Auto Possess AI` 가 `Placed in World or Spawned` 이면
엔진이 `SpawnActor` 안에서(`PostInitializeComponents`) 알아서 붙인다.** C++ 이
안 부르는 것과 무관하게 붙으므로, 위 방어가 통째로 무너진다.

```
SpawnOne()                         ← 엔진이 컨트롤러를 붙이고 트리가 갈래를 고른다
                                      이 시점 StartRole = 비어 있음
SetDecoration / OnAssignedSpawnRole ← 역할은 **이제서야** 들어간다
```

루트의 세 조건(`Start Role is Eating/Waiting/Ordering`)이 전부 거짓이 되어
**프리필 전원이 조건 없는 기본 갈래 `Enter` 로** 떨어진다.

증상 —

| 보이는 것 | 실제 |
|---|---|
| 앉은 손님이 **폰 보는 모션**을 1초쯤 한다 | `Enter ▸ Play Action Idle_Look` |
| 앉은 손님이 **손 뻗는 모션**을 1초쯤 한다 | `Kiosk ▸ K_Act ▸ Play Action Interaction_Reach` |
| 픽업대에 안 찾아가는 주문이 쌓인다 | `K_Order ▸ Place Order` 를 장식 손님이 넣는다 |
| 키오스크 2대가 계속 잡혀 있다 | `Claim Slot Kiosk` 를 장식 손님이 잡는다 |
| `bPrefillRunsAI` 를 꺼도 아무 일도 안 일어난다 | `StartControllers` 는 컨트롤러가 없을 때만 스폰한다 |

⚠ **1초만 스치는 이유** — 트리와 C++ 장식 루프가 **같은 `DefaultSlot`** 을 두고
싸운다. 트리가 몽타주를 걸면 `PlayLoopedAction` 타이머가 돌아와 `Sit_Idle` 로
덮는다. 보이는 시간은 그 주기의 절반쯤이다 (`AM_SitIdle_Male` 2.35초 → 약 1.2초).
**주기가 긴 변형을 받은 손님일수록 오래 보인다** (`AM_Sitting_Talking` 43.7초).

⚠⚠ `P_Eating` · `P_Waiting` · `P_Ordering` 갈래와 그 아래 `PE_*` · `PW_*` ·
  `PO_*` 가 전부 **도달 불가능한 죽은 코드**가 된다. 트리만 들여다보면
  「갈래는 멀쩡한데 왜 안 타지」로 며칠을 보낸다.

→ **`BP_Customer > Auto Possess AI = Disabled`.** 워크인은 스포너가
  `StartControllers()` 로 직접 붙이므로 영향이 없다. 레벨에 손으로 놓인
  `BP_Customer` 도 0개라 컨트롤러를 잃을 액터가 없다 (확인했다).

⚠ 진단은 `ll_prefill_ai_probe.py` 가 해 준다.

### ⚠⚠ **에디터가 켜져 있으면 저장이 전부 `False` 다**

에셋 저장 API 가 **하나도 안 통한다.** 편집·컴파일은 멀쩡히 성공하고 같은 실행
안의 되읽기도 통과하는데, 파일만 안 써진다.

```
편집 없이 그냥 저장해도 —
  save_state_tree=False  save_packages=False  save_asset=False
```

⚠ 진단이 엉뚱한 데로 샌다. 실제로 `remove_task` 를 의심하고 한참 헤맸는데,
  **편집 없이 저장해도 False** 인 것을 보고서야 원인이 드러났다.

→ 커맨드릿을 돌리기 전에 **에디터를 닫는다.** 확인은 프로세스 목록이 가장 빠르다.

⚠ 파일 **수정 시각**을 보는 습관을 들일 것. 반환값과 로그만 보면 성공처럼 보인다.

### ⚠⚠ `compile_blueprint` **뒤에는 블루프린트가 저장되지 않는다**

CDO 기본값을 고치고 컴파일한 다음 저장하면 — **모든 저장 경로가 `False` 를
돌려주고 파일이 안 써진다.**

```
compile 뒤 :  save_asset False · save_packages False · save_loaded_asset False
compile 없이:  전부 True            ← 실측
```

⚠⚠ 그런데 **같은 프로세스의 되읽기는 통과한다.** 메모리의 CDO 는 제대로 바뀌어
있기 때문이다. 그래서 로그만 보면 완전히 성공한 것처럼 보인다.

실제로 당했다 — `ll_montages.py` 가 몽타주 6개는 새로 썼는데 `BP_Customer.uasset`
만 13분 전 실행 그대로였고, 스크립트의 되읽기는 6개를 정상으로 찍었다.
**파일 수정 시각을 보고서야** 알았다.

→ **저장을 컴파일보다 먼저** 한다. `Actions` 같은 CDO 기본값은 컴파일 없이
  저장해도 반영되고, 컴파일은 에디터에 올라와 있는 인스턴스 갱신용이다.

```python
pkg = bp.get_outermost()
ok = unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)   # ← 저장 뒤에
```

⚠ 반환값을 **버리지 말 것.** `ll_montages.py` 는 `False` 면 로그에 크게 찍는다.
  진단은 `ll_bp_save_probe.py` 가 경로별로 하나씩 시험해 준다.

---

## 7. 몽타주

`Content/BurgerHouse/Anims/Montages/` 에 11개가 있고 `BP_Customer > Actions` 에
꽂혀 있다. `ll_montages.py` 가 만든다 (다시 만들려면 그 스크립트를 돌린다).

```
슬롯 DefaultSlot (전신) — 전부 여기다
  Idle_Look       AM_Idle 1.0 · AM_Texting 0.3
  Interact_Reach  AM_Reaching_Out 1.0
  Sit_Idle        AM_Eating_a_Burger 1.0 · AM_Eating_Fries 1.0            ← 프리필 전용
                  AM_Eating_a_Drumstick 1.0 · AM_Eating_Frantically 0.6
                  AM_Sitting_Talking 0.8 · AM_SitIdle_Male 0.5
  Sit_Eat         AM_Eating_* 4개 (전부 1.0 = 균등)                        ← 트리 전용
```

`Sit_Idle` 은 **프리필(장식) 손님이 돌리는 풀**, `Sit_Eat` 은 걸어 들어온 손님이
트리에서 쓰는 것이다 (§7-1).

⚠ `ST_Customer` 는 `Sit_Idle` 을 **한 번도 안 쓴다** — `ll_st_dump.py` 로 확인했다.
  트리가 쓰는 이름은 `Sit_Eat` · `Sit-Eat` · `Idle_Look` · `Interact_Reach` ·
  `Interaction_Reach` 다. 그래서 `Sit_Idle` 을 바꿔도 워크인 손님에겐 영향이 없다.

### 7-1. 프리필 손님 idle — 버거집 손님답게

앉아 있는 장식 손님이 무엇을 하고 있어야 하는가. **대다수는 먹고 있어야** 하고,
일부는 일행과 얘기하고, 한둘은 그냥 앉아 있다. 가중치 합 4.9 중 먹는 것이 3.6
(**73%**) 이라 홀을 보면 「밥 먹는 가게」로 읽힌다.

```
Eating_a_Burger 1.0 · Eating_Fries 1.0 · Eating_a_Drumstick 1.0   먹는 중
Eating_Frantically 0.6        ← 과장된 동작이라 낮게. 흔하면 홀이 소란스럽다
Sitting_Talking 0.8           ← 일행과 대화 (44.07s, 변화가 풍부)
SitIdle_Male 0.5              ← 다 먹고 쉬는 사람
```

한 사람은 **하나만** 평생 반복한다(`bDecorationSingleAction`). 다양성은 한 사람
안이 아니라 **손님들 사이에서** 나온다 — 좌석 번호 해시로 결정적으로 뽑으므로
같은 의자는 늘 같은 몸짓이고 옆자리와 안 겹친다. 다시 섞으려면
`DecorationSeed` **하나만** 바꾼다.

⚠ `PrefillPlacements` 가 3인석(party 6)·2인석(party 4) 두 테이블이라
  `Sitting_Talking` 이 말이 된다. 혼자 앉은 자리에 걸리면 어색하니, 자리 구성을
  바꿀 때 가중치를 다시 볼 것.

#### ⚠⚠ 후보를 고르는 기준은 **골반 높이**다 — 이름이 아니다

이름에 `Sitting` 이 들어가도 **서 있는 동작**인 것이 섞여 있다. `ll_sit_candidates.py`
로 전부 재서 골랐다. 기준은 화면 확인된 먹는 애님의 **57.4~57.6**.

| 후보 | 골반 Z | |
|---|---|---|
| `Eating_*` 4종 | 57.4~57.6 | 기준 |
| `Sitting_Talking` | 57.5 | **쓴다** |
| `SitIdle_Male` | 57.9 | **쓴다** |
| `Sitting_Yell` | 61.7 | 뺐다 — 4cm 뜨고 동작도 과하다 |
| `Sitting_Thumbs_Up` | 76.2 | 뺐다 — 19cm. 앉은 동작이 아니다 |
| `Sitting` · `Texting` · `Idle` | 101~103 | **서 있는 동작** |

⚠⚠ 「섬」인 것을 전신 슬롯으로 틀면 **앉은 손님이 일어선다** (아래 상체 분리 항목).
  에러가 안 나고 화면에서만 틀린다. `ll_sit_verify.py` 가 풀 전체의 골반 Z 를
  재서 기준에서 ±2cm 를 넘으면 실패로 잡는다 — **그 가드를 지우지 말 것.**

#### 새로 만든 것

```
Content/BurgerHouse/Anims/Mixamo/Retarget/  RTG_MannySit  (IK_MannyWalk 재사용)
Content/BurgerHouse/Anims/Mixamo/Anims/     SitIdle_Male   2.700s · 골반 Z 57.9
```

`ll_sit_retarget.py` 가 마네퀸 `Characters/ChairAnimation/Male/SitIdle_Male` 을
옮기고, `ll_montages.py` 가 `AM_SitIdle_Male` 을 만들어 꽂는다. 나머지 5종은
이미 손님 스켈레톤에 있어 리타겟이 필요 없었다.

#### 왜 프리필 전원에 적용되나

`L_BurgerHouse` 의 스포너는 `PrefillPlacements` 5개를 쓰고 **전부 역할이
`Eating`** 이다. `PrefillPlacements` 가 비어 있지 않으면 `PrefillRoles` 는
통째로 무시되므로, 실제로 생기는 프리필 손님은 `Eating → Sit_Idle` 하나뿐이다.

⚠ `PrefillPlacements` 를 비우고 `PrefillRoles`(Eating 3 · Waiting 1 · Ordering 1)로
  돌리면 **서 있는 손님이 생긴다.** 그들은 `Idle_Look` 을 쓰므로 이번 교체 밖이다.
  거기에 앉은 애님을 씌우면 **공중에 앉는다.**

#### ⚠⚠ 골반 yaw 보정은 `ll_eat_retarget.py` 와 **다르다**

| | 먹는 애님 | 이것 |
|---|---|---|
| `blend_to_source_translation` | 1.0 | 1.0 (같다 — 좌면 높이가 정답이다) |
| `rotation_offset_global.yaw` | **+90** | **0** |

+90 을 물려받아 넣었더니 골반 yaw 가 **+82.6** 이 나왔다 (기준: 화면 확인된
`Eating_a_Burger` +2.1). 두 번 돌아간 것이다. 먹는 애님은 팩마다 딸려온 제
스켈레톤을 쓰고 소스 리그를 급조했지만, 이쪽은 `IK_MannyWalk` 를 재사용해
`auto_align_all_bones` + `run_op_initial_setup` 이 회전 델타를 이미 잡아 준다.

→ **값을 물려받지 말고 잴 것.** 되읽기가 기준과 나란히 찍어 준다.

#### 루프 이음매 — 변형마다 다르다

첫↔끝 프레임 자세 차이(13본 합) 실측 —

| | 차이 |
|---|---|
| `Sitting_Talking` | 0.3도 |
| `SitIdle_Male` | 1.0도 |
| `Eating_Frantically` | 73.8도 |
| `Eating_a_Drumstick` | 92.1도 |
| `Eating_Fries` | 128.2도 |
| `Eating_a_Burger` | 221.0도 |

먹는 것들은 여전히 크다 — **`DecorationLoopCrossfade` 0.35 를 그대로 둬야 한다.**
그 값이 왜 필요한지는 §7 의 「루프 이음매」 항목에 있다.

⚠ 0 으로 내리지 말 것. `AM_*` 의 `BlendOut` 이 0.35 라 **자동 블렌드아웃 시작
  시점**(길이 − 0.35)과 다음 재생 시점이 정확히 겹치게 맞춰 둔 값이다. 내리면
  기본 포즈가 0.35초 비친다.

#### 더 늘리고 싶으면

`SitIdle_UpHand_Male` · `SitIdle_Sleep_Male` · `SitIdle_UpHand_SpineHead_Male` ·
`SitIdle_FeMale` · `Sit_Idle_Think_FeMale` 이 전부 골반 Z **57.9** 로 기준에
맞는다 (`ll_sit_candidates.py` 실측). `ll_sit_retarget.py` 의 `SRC_ANIM` 만 바꿔
돌리고 `ll_montages.py` 의 `Sit_Idle` 에 줄을 더하면 된다.

⚠ `Sit_Idle_Write_FeMale`(글쓰기)은 버거집에 안 맞는다. 높이는 맞지만 뺐다.

### ⚠⚠ 상체 분리(`UpperBody`)는 **되돌렸다** — 전제가 틀렸었다

한동안 먹는 애님을 `UpperBody` 슬롯에 넣고 하체는 AnimBP 의 Sit 상태로 덮었다.
근거는 「먹는 애님은 서서 먹는 동작이니 전신으로 틀면 앉은 손님이 일어선다」
였는데, **틀렸다.**

```
원본(마네퀸) Eating_a_Burger_Anim  골반 Z = 57.4     ← 앉음
기준:  앉음 57~63  ·  섬 103
```

**원래부터 앉아서 먹는 동작**이다. 그 오판은 리타겟 결과가 **빈 애님**이던
시절에 내린 것이라 자세를 확인할 방법이 없었다 (움직임이 0이었다).

⚠ 앉은 동작을 상체만 떼어 얹으면 상체는 애님의 좌면 높이, 하체는 Sit 상태의
높이를 따라 **상체가 공중에 뜬다.** 실제로 그렇게 보였다.

→ 앉은 동작은 앉은 동작 그대로 **전신**으로 튼다. 슬롯은 하나면 된다.

⚠ `AM_Sit_Texting` · `AM_Sit_Talking_On_A_Cell_Phone` 은 이제 **아무 액션도 안
쓴다.** 그 둘은 진짜 서 있는 동작이라 전신으로 틀면 손님이 일어선다. 에셋은
남겨 뒀다 — 되살리려면 AnimGraph 의 상체 레이어가 필요하다.

⚠ AnimGraph 의 `Slot 'UpperBody'` + Layered blend 는 **그대로 둬도 무해하다**
(몽타주가 안 오면 입력을 그대로 통과시킨다). 정리하고 싶으면 지워도 된다.

⚠ 길이가 제각각이다 — `AM_Sit_Talking_On_A_Cell_Phone` 은 **37.2초**로 길다
(가중치 0.5 라 10번에 한 번쯤). 짧게 하려면 §8-2 처럼 구간을 자른다.

가중치는 합이 1일 필요 없다. 기본 동작을 1.0, 가끔 나오는 몸짓을 0.2 로 두면
대개 평범하고 가끔 눈에 띄는 그림이 된다. `Sit_Eat` 은 넷을 고르게 쓰려고 전부
1.0 으로 뒀다.

고르는 것은 `ACustomerCharacter::PickVariant` 가 한다 — 가중 무작위이고,
`bAvoidRepeatVariant` 가 같은 것이 연속으로 나오지 않게 막는다.

### ⚠⚠ 「앉는 동작」과 「앉아 있는 동작」은 다르다

**루프하는 상태에는 「앉아 있는 동작」만 쓴다.** 「앉는 동작」을 루프하면 매 번
서서 시작하므로 **섰다 앉았다를 반복**한다. 실제로 이걸로 한참 헤맸다.

골반(`hips`) 높이의 **시작 / 끝** 을 비교하면 바로 갈린다:

| 애님 | 시작 | 끝 | 정체 |
|---|---|---|---|
| `Sitting` | 102.8 | **63.1** | **앉는 동작** — 루프 금지 |
| `Sit_To_Stand` | 57.5 | **103.0** | 일어서는 동작 |
| `Sitting_Talking` | 57.5 | 57.5 | 앉아 있는 동작 ✓ 44s |
| `Sitting_Yell` | 61.7 | 61.7 | 앉아 있는 동작 ✓ 3.8s |
| `Idle` (기준) | 103.4 | 103.4 | 서 있음 |

⚠ 이름만 보고 고르면 안 된다 — `Sitting` 은 「앉기」지 「앉아 있기」가 아니다.
`Sitting_Thumbs_Up` 도 76 으로 어중간해서 기본 포즈로는 안 쓴다.

⚠ 앉은 골반은 **57~63**, 선 골반은 **103** 이다. 먹는 애님이 104.3 인 것이
「서서 먹는 동작」이라는 근거였다.

`Sitting` · `Sit_To_Stand` 는 나중에 `Sit_Down` · `Stand_Up` 액션으로 쓸 것 —
전이 동작이라 **한 번만** 재생해야 한다.

### 먹는 애님은 리타겟해서 넣었다

`Eat_Animation_Pack` 은 **UE 마네퀸 리그(161본 · `pelvis`)** 로 만들어졌고 손님은
**Mixamo 리그(65본 · `mixamorig:*`)** 라 그대로는 재생이 안 된다. `ll_eat_retarget.py`
가 IK 리그 + 리타게터를 만들어 손님 스켈레톤용 애님 4개를 뽑는다.

```
Anims/Eat_Animation_Pack/          원본 (마네퀸 리그)
Anims/Eat_Animation_Pack/Retarget/ IK_* · RTG_*  ← 리타겟 설정
Anims/Mixamo/Anims/Eating_*        결과 (손님 리그) 6.9~9.2s
```

⚠⚠ **자세가 맞는지는 눈으로 봐야 한다.** 자동 정렬(A포즈↔T포즈)이 대개 맞지만
손에 물건을 든 애님은 손 위치가 어긋나기 쉽다. 어긋나면 `RTG_*` 를 열어
**Edit Pose** 로 맞추고 `ll_eat_retarget.py` 를 다시 돌린다.

#### ⚠⚠ 리타게터는 **op 스택**이다 — 비어 있으면 조용히 빈 애님이 나온다

UE 5.6+ 의 IK Retargeter 는 연산 목록으로 돈다.

```
Pelvis Motion → FK Chains → Run IK Rig → Root Motion → Remap Curves
```

그런데 `IKRetargetFactory` 로 만든 새 리타게터는 **op 이 0개**다. 그 상태에서도

- 리그 지정(`set_ik_rig`) 성공
- 자동 정렬(`auto_align_all_bones`) 성공
- 체인 매핑(`auto_map_chains`) **성공한 것처럼 보이지만 실제로는 아무것도 안 붙는다**
  (매핑이 op 안에 들어 있기 때문이다)
- 배치 리타겟도 에셋을 정상적으로 만들어 낸다

**결과물만 타깃의 레퍼런스 포즈** — 즉 **모든 본이 안 움직이는 애님**이다.
스켈레톤·길이·프레임 수·트랙 65개가 전부 맞아서 어디서도 안 걸린다. 몽타주도
정상으로 만들어지고 `Montage_Play` 는 길이를 돌려주며 성공을 보고한다.

→ `ensure_ops()` 가 `add_default_ops()` + `assign_ik_rig_to_all_ops()` 를
**체인 매핑보다 먼저** 부른다. 순서가 바뀌면 안 된다.

같이 있던 두 번째 원인 — `IK_Mixamo` 의 체인이 전부 `Spine -> Spine` 처럼
**1본짜리**였다. 체인 이름을 본 이름 자리에 넣은 실수인데, Mixamo 에 마침
`Spine`·`Head`·`LeftArm` 이라는 본이 실제로 있어서 오류가 안 났다.

⚠ **되읽기에서 스켈레톤만 보면 안 된다.** 예전 검사가 그래서 빈 애님을 통과
시켰다. 지금은 본이 실제로 움직이는지(`amp`)까지 잰다 — 정상이면 200~400도,
빈 애님은 0도다.

#### ⚠ 배치 리타겟은 **에디터가 열려 있으면 덮어쓰지 못한다**

대상 에셋이 로드돼 있으면 `overwrite_existing_files=True` 라도 덮지 못하고
**`Eating_a_Burger1` 처럼 뒤에 숫자를 붙인다.** `EditorAssetLibrary.delete_asset`
도 `True` 를 돌려주면서 파일을 안 지운다 (`Device or resource busy`).

→ `ll_eat_retarget.py` 는 **반드시 에디터를 끄고** 돌릴 것.

#### ⚠⚠ 골반이 **바닥에 붙는다** — 회전만 보면 안 걸린다

op 을 채우고 나니 회전은 정상(200~400도)인데 **골반 Z 가 0** 이었다. 캐릭터가
바닥에 주저앉은 그림이 된다.

기본값(`blend_to_source_translation = 0`)에서 Pelvis Motion op 은 소스 골반의
**상대 운동**을 타깃 리타겟 포즈의 골반 높이에 얹는다. 그 기준 높이가 안 잡히면
결과가 0 이 된다.

→ 소스·타깃이 둘 다 사람 크기이므로 **소스 위치를 그대로 따라가게** 한다.

```python
s.set_editor_property('blend_to_source_translation', 1.0)
s.set_editor_property('blend_to_source_translation_weights', unreal.Vector(1, 1, 1))
```

⚠ 그래서 되읽기가 **골반 Z 도 본다.** 30 미만이면 실패로 친다. 앉은 동작이면
57~63 이 나와야 한다.

#### ⚠⚠ 그런데 위치를 따라가게 하면 **방향도 소스 규약으로 들어온다**

`blend_to_source_translation = 1.0` 을 넣자 골반 높이는 맞았는데 **손님이 90도
옆을 보고 앉았다.** 마네퀸 골반 정면은 `+X`, Mixamo `Hips` 는 `+Y` 라서다.

```
보정 없이 리타겟한 Hips yaw   -87.9 · -89.5 · -89.5 · -89.2
정상 Mixamo 애님              Sitting__1_ 5.3 · Idle 5.5 · Texting -1.5
```

→ Pelvis Motion op 의 `rotation_offset_global` 에 **+90** 을 준다
(`PELVIS_YAW_FIX`). 넣고 나면 `+0.5 ~ +2.1` 로 들어온다.

⚠ `run_op_initial_setup()` 으로는 안 잡힌다. 해 봤다.

⚠ **회전량(`amp`)과 골반 높이로는 이 오류가 안 걸린다** — 둘 다 정상으로
나온다. 그래서 되읽기가 **Hips yaw** 까지 본다 (±40도를 넘으면 실패).

정리하면 이 리타겟 하나에서 **연달아 네 번** 조용히 틀렸다.

| 무엇이 틀렸나 | 증상 | 무엇으로 잡히나 |
|---|---|---|
| op 스택이 비어 있음 | 안 움직임 | `amp` |
| `IK_Mixamo` 체인이 1본짜리 | 거의 안 움직임 | `amp` |
| Root Motion op 이 골반을 가져감 | 바닥에 주저앉음 | 골반 Z |
| 골반 방향이 소스 규약 | **90도 옆을 봄** | Hips yaw |

넷 다 에셋은 정상적으로 만들어지고 스켈레톤·길이·트랙 수가 맞는다.
**되읽기가 이 네 가지를 전부 재도록** 해 둔 이유다.

### 슬롯은 **하나**다 (`DefaultSlot`)

전부 전신이다. 상체 분리를 왜 되돌렸는지는 §2 「상체 분리는 되돌렸다」 참고 —
먹는 애님이 **원래부터 앉은 동작**이었다 (원본 골반 Z 57.4).

⚠ 몽타주를 새로 만들면 팩토리가 `DefaultSlot` 으로 만든다 — 지금은 그대로 두면
맞다. `UpperBody` 가 다시 필요해지는 건 **서 있는 동작을 앉은 손님에게 얹을
때**뿐이다.

⚠ 예전에 쓰던 `AM_Sitting*` 몽타주 4개는 그대로 남아 있다 (미사용). 다시 쓰려면
`ll_montages.py` 의 `ACTIONS` 에 되돌려 넣으면 된다.

---

## 8. 남은 에디터 작업

### ⚠⚠ 이 절의 전제가 **바뀌었다** — 이제 대부분 스크립트로 된다

예전에는 「UE 5.8 파이썬은 그래프 노드를 못 만들고 StateTree 태스크 내부
(`InstancedStruct`)는 읽기 전용」이라 적어 놨는데, **Revvy 플러그인의 서비스가
둘 다 해 준다.** 실제로 해 봤다.

| 한다 | 무엇 |
|---|---|
| `unreal.StateTreeService` | `add_task` · `remove_task` · `set_task_property_value` · `compile_state_tree` · `save_state_tree` |
| `unreal.AnimGraphService` | `add_slot_node` · `connect_anim_nodes` · `set_state_animation` (§9-2 가 이걸로 그래프를 고쳤다) |
| `unreal.BlueprintService` | `describe_graph` · `get_nodes_in_graph` · `get_connections` |

⚠ 그래서 **8-1 의 오타 두 개도 이제 스크립트로 고칠 수 있다.** 별칭으로 막아 둔
  것을 걷어낼 수 있다는 뜻이다 (아직 안 했다).

여전히 못 하는 것 — 몽타주 `SequenceLength`(§8-2) 는 읽기 전용 그대로다.

#### ⚠⚠ `StateTreeService` 가 **조용히 `False` 를 주는** 두 가지

둘 다 예외도 로그도 없다. 반환값을 안 보면 「넣었는데 안 들어갔다」가 된다.

```python
add_task(ST, 'K_Order',            'FBurgerHousePlaySoundTask')  # -> False
add_task(ST, 'Root/Kiosk/K_Order', 'BurgerHousePlaySoundTask')   # -> True
```

1. **상태는 전체 경로**여야 한다 — `K_Order` 가 아니라 `Root/Kiosk/K_Order`
2. **구조체 이름에 `F` 를 붙이지 않는다** — `get_state_tree_info` 의 `StructType`
   표기(`BurgerHousePlaceOrderTask`)와 같아야 한다

⚠ `get_state_tree_info` 는 트리 전체를 한 줄로 뱉는다(수천 자). 상태 하나만
  보려면 `all_states` 를 경로로 걸러 쓸 것 — `ll_order_sound.py` 의 `tasks_of()`.

### 8-1. 액션 이름 오타 둘 — **별칭으로 막아 뒀다**

```
K_Act    Play Action  Interaction_Reach   →  Interact_Reach
Eating   Play Action  Sit-Eat             →  Sit_Eat
```

`FName` 이라 **조용히 실패한다** — 몽타주를 꽂아도 못 찾고, 증상이 「몽타주가
없는 것」과 완전히 똑같다. 그 상태에서는 애님 없이 폴백 4초만 센다. **「워크인이
잠깐 앉았다 일어선다」의 정체가 이것이었다.**

같은 트리에 올바른 철자도 따로 있다 (`Sit_Eat` 1곳 · `Interact_Reach` 2곳) —
즉 상태에 따라 되기도 하고 안 되기도 했다.

지금은 `ll_montages.py` 가 **오타 이름도 같은 몽타주로 등록**해서 동작한다.

```python
ACTIONS['Sit-Eat'] = ACTIONS['Sit_Eat']
ACTIONS['Interaction_Reach'] = ACTIONS['Interact_Reach']
```

⚠ 에디터에서 트리의 철자를 고치면 **위 두 줄도 같이 지울 것.** 안 지우면 쓰이지
않는 항목이 남아 다음 사람이 어느 쪽이 진짜인지 헤맨다.

### 8-2. `Idle_Look` 몽타주가 너무 길다 — **긴 변형을 빼서 절반쯤 줄였다**

`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다.** `Enter` 가 `Idle_Look` 을
한 번 재생하고 끝나므로 그 길이가 곧 **문 앞 대기 시간**이다.

```
예전  최대 37.2s · 기대값 24.7s
지금  최대 18.5s · 기대값 10.8s
```

| 지금 쓰는 것 | 길이 | 가중치 | 첫끝차이 |
|---|---|---|---|
| `AM_Idle` | 8.33s | 1.0 | **0.0** (진짜 순환) |
| `AM_Texting` | 18.53s | 0.3 | 0.2 |

| 뺀 것 | 길이 |
|---|---|
| `AM_Talking_On_A_Cell_Phone` | **37.2s** |
| `AM_Texting_While_Standing` | 23.6s |

⚠⚠ **「프리필 NPC 가 서서 폰을 본다」의 정체가 이것이었다.** `BeginPlay` 가
프리필 직후 바로 `TrySpawn()` 을 부르고(`CustomerSpawner.cpp:36`), 정원 8 −
프리필 5 = **워크인 3명**이 문 앞에 생겨 30초 넘게 폰을 본다. 앉아서 먹는
프리필과 같이 보이니 프리필로 읽힌다.

**구분법** — 장식 손님만 이 줄을 찍는다 (`CustomerCharacter.cpp:499`):

```
BP_Customer_3 장식 몸짓 Sit_Idle · 변형 2 (자리 14 · 씨앗 0)
```

⚠ 장식 손님은 트리를 **탈 수 없다.** StateTree 는 `ACustomerAIController` 의
`UStateTreeAIComponent` 에만 있고 폰에는 없다 — 컨트롤러를 안 붙이면 시작될
경로가 없다. 그러니 프리필이 `Idle_Look` 을 트는 일은 구조적으로 불가능하다
(역할이 `Waiting`·`Ordering` 인 프리필을 만들지 않는 한).

아래는 **더 줄이고 싶을 때**의 이야기다.

⚠ 줄에 선 손님(`W_Idle`·`PW_Idle`)은 루프라 덜하다. 몸짓이 늦게 바뀔 뿐이다.

```
몽타주 열기 > Montage Sections 에서 5~8초 구간 하나로 자른다
```

⚠ 자를 때 **첫 프레임과 끝 프레임 포즈가 비슷한 구간**을 고른다. 안 그러면
반복마다 툭 튄다.

⚠ **파이썬으로는 반만 된다.** 세그먼트 `AnimEndTime` 은 써지는데
`SequenceLength` 가 읽기 전용이라, 애님만 짧아지고 상태 머무는 시간은 그대로다 —
마지막 프레임에서 얼어붙어 **더 나빠진다.** 에디터에서 할 것.

급하면 `Idle_Look` 에서 긴 변형(`Talking_On_A_Cell_Phone`·`Texting_While_Standing`)
을 빼는 것만으로도 문 앞 대기가 18.5초로 준다.

---

## 9. 이동 — 흔들림과 기물 파고듦

「손님이 앞뒤로 움직인다」와 「벽·기물을 파고든다」는 **같은 뿌리**에서 나온다.
`BP_Customer` 의 이동 기본값이 야외 액션 캐릭터 값 그대로였다.

| 프로퍼티 | 전 | 후 | 왜 |
|---|---|---|---|
| `MaxWalkSpeed` | 600 | **350** | **폰과 같은 값으로 고정** — §9-4 |
| `MaxAcceleration` | 2048 | 1400 | 급출발·급정지가 앞뒤 흔들림으로 보인다. 속도 × 4 |
| `BrakingDecelerationWalking` | 2048 | 2100 | 위와 같음. 속도 × 6 |
| `bOrientRotationToMovement` | false | **true** | 아래 참고 — 이것이 「앞뒤로 움직인다」의 정체다 |
| `bUseControllerRotationYaw` | true | **false** | 위와 **상호 배타**다. 둘 다 켜면 orient 가 이긴다 |
| `RotationRate.Yaw` | 360 | 540 | 좁은 통로에서 방향 전환이 따라와야 한다 |
| `MaxStepHeight` | 45 | **30** | 의자 좌면이 45cm·소파가 40cm — 45면 **의자 위로 걸어 올라간다** |
| `bUseRVOAvoidance` | false | **true** | 안 켜면 좁은 통로에서 서로 밀치며 진동한다 |
| `AvoidanceConsiderationRadius` | 500 | 200 | 실내에서 500은 홀 전원을 고려해 과조향한다 |

### ⚠⚠ 「앞뒤로 움직인다」의 정체 — 게걸음이다

`bUseControllerRotationYaw = true` 면 폰은 **컨트롤 회전**을 따르는데, AI 는 그것을
**최종 목적지**로 맞춘다. 경로가 기물을 돌아가면 몸은 목적지를 본 채 옆으로
움직인다 — **게걸음**이다.

그런데 `ABP_Customer` 에는 **`Speed` 변수 하나뿐이고 `Direction` 이 없다.**
스트레이프 블렌드스페이스가 없으니 어느 방향으로 움직이든 **앞으로 걷는 애님**만
나온다. 그래서 옆·뒤로 미끄러지는 그림이 된다.

→ `bOrientRotationToMovement = true` 로 몸이 **실제 속도 방향**을 보게 하면
전진 애님 하나로 항상 맞는다.

⚠ **부작용 — 이제 고쳤다 (§9-4).** 이동이 끝나면 마지막 진행 방향 그대로 서서
키오스크에 도착한 손님이 키오스크를 등질 수 있었다. `Play Action` 에
`FaceSlotKind` 를 넣으면 재생 직전에 슬롯 방향으로 돌려 놓는다.
(앉는 쪽은 `SitAt` 이, 프리필은 `TeleportToPoint` 가 원래 회전을 넣었다.)

⚠ `MaxWalkSpeed` 를 **5 밑으로 내리지 말 것.** Locomotion 의 Idle↔Walk 전이가
`Speed > 5` 라 그 밑에서는 **Idle 포즈로 미끄러진다.**

⚠ 예전에 이 문단이 **10** 이라고 적혀 있었다. 실측하니 전이 규칙은 `Speed > 5`
  (되돌아오는 쪽도 `Speed < 5`) 였다 — `ll_abp_structure.py` 가 규칙을 통째로
  찍어 준다. 전이 값은 **AnimBP 에서 확인할 것.** 문서만 믿고 임계값을 맞추면
  어긋난다.

### 9-1. 걷는 애님 교체 — Mixamo `Walking` → 마네퀸 `A_WalkFwd_Loop`

`ll_walk_retarget.py` 로 마네퀸 걷기를 손님 스켈레톤에 리타겟해
`Walk_Fwd_Manny` 를 만들고, `ll_walk_apply.py` 가 Walk 상태와 속도를 바꾼다.

```
Content/BurgerHouse/Anims/Mixamo/Retarget/  IK_MannyWalk · RTG_MannyWalk
Content/BurgerHouse/Anims/Mixamo/Anims/     Walk_Fwd_Manny   1.067s
```

#### ⚠⚠ 옛 `Walking` 은 **제자리 애님이 아니었다**

| | 골반 전진 | ForceRootLock | 결과 |
|---|---|---|---|
| 옛 `Walking` | **279.64cm** / 2.867s | **false** | 메시가 캡슐보다 앞서 나갔다가 2.87초마다 되돌아온다 |
| 새 `Walk_Fwd_Manny` | **0.00cm** | — | 제자리. 전진은 CharacterMovement 만 한다 |

`ABP_Customer` 의 루트모션 모드가 `RootMotionFromMontagesOnly` 라 **아무도 골반
이동을 캡슐로 뽑아가지 않는다.** 그래서 골반 트랙의 279cm 가 그대로 화면에
나왔다. 「97.5cm/s 에 맞췄으니 발이 안 미끄러진다」는 계산은 제자리 애님을
전제한 것이라 성립하지 않았다.

⚠ 손님이 쓰는 다른 애님(`Idle` 전진 0.00cm)은 전부 제자리다. 규약이 그쪽이다.

#### ⚠⚠ 제자리로 만드는 손잡이는 `Root Motion` op 이 **아니다**

`Root Motion` op 을 꺼도 골반은 195.24cm 전진했다. 소스 `root` 의 이동이
`pelvis` 의 **글로벌 트랜스폼**에 이미 섞여 있고, `Pelvis Motion` op 이 그것을
타깃 `hips` 로 옮기기 때문이다.

→ `Pelvis Motion` op 의 **`scale_horizontal = 0`** 이 답이다. 수평 성분만
  없애고 상하 바운스(`scale_vertical = 1`)와 다리 회전은 남긴다.

⚠ 좌우 흔들림(sway)도 같이 사라진다. Mixamo 의 「In Place」 내보내기와 같다.

#### 속도는 **지우기 전에** 재 둔다

수평을 죽이면 더는 못 잰다. 그래서 `scale_horizontal = 1` 로 한 번 돌려 재고,
그 값을 `MaxWalkSpeed` 에 넣는다.

```
마네퀸 원본 A_WalkFwd_Loop   179.39cm / 1.067s = 168.18 cm/s   (root 본에 실려 있다)
손님으로 리타겟             195.24cm / 1.067s = 183.0  cm/s   (체격비 101.8/93.6 = 1.088)
```

⚠ `A_WalkFwd_Loop` 는 `EnableRootMotion=True · ForceRootLock=True` 다. 마네퀸에는
`root` 본이 따로 있어서 그 규약이 성립한다. **Mixamo 에는 `root` 가 없고 `hips`
자체가 루트**라 같은 규약을 흉내 낼 수 없다 — 그래서 제자리로 굽는다.

#### 먹는 애님과 달랐던 두 값

| | 먹는 애님 | 걷기 | 왜 |
|---|---|---|---|
| `blend_to_source_translation` | **1.0** | **0.0** | 1 로 두면 마네퀸 골반 높이(93.6)가 그대로 와서 손님이 7.5cm 가라앉는다. 0 이면 손님 본래 높이 101.8 이 나온다 |
| `rotation_offset_global.yaw` | **+90** | **0** | 골반 회전이 소스 규약으로 들어오는 것은 `blend_to_source_translation=1` 의 부작용이었다. 0 이면 애초에 안 생긴다 |

#### ⚠ `run_op_initial_setup` 은 **op 하나씩** 받는다

```python
rc.run_op_initial_setup()      # TypeError — op_index 가 필수다
for i in range(n): rc.run_op_initial_setup(i)
```

인자 없이 부르면 예외로 조용히 건너뛰어지고, 각 op 이 「소스·타깃 리타겟 포즈의
차이」를 못 잡은 채로 돈다. 그리고 **초기 설정이 Pelvis Motion 값을 되돌린다** —
그래서 `ll_walk_retarget.py` 는 초기 설정 **뒤에 한 번 더** 박는다.

#### 검산 — 세 스크립트

| | 무엇 |
|---|---|
| `ll_walk_verify.py` | **새 프로세스**에서 ABP 참조·속도·제자리 여부. 같은 실행 안의 되읽기는 무의미하다 (§6) |
| `ll_walk_pose_check.py` | A포즈/T포즈 정렬. 사지가 아래와 이루는 각을 기존 `Walking` 과 견준다 (실측 팔 2.6도·다리 0.3도 차이 = OK) |
| `ll_walk_probe.py` · `ll_walk_stride.py` | 교체 전 실측용 |

⚠⚠ **`get_bone_pose_for_time` 은 부모 기준 로컬 트랜스폼이다.** 컴포넌트 스페이스로
착각하면 그럴듯한 숫자가 나온다 — 「발 − 골반」으로 보폭을 잰 줄 알았는데
사실은 골반 속도를 재고 있었다. 조상을 훑어 합성해야 한다
(`find_bone_path_to_root`, `ll_walk_pose_check.py` 참고).

### 9-2. 로코모션을 **몽타주로** 돌린다 (`LocoSlot`)

Idle·Walk·Sit 을 상태 기계의 Sequence Player 가 아니라 **몽타주**로 재생한다.
계획과 판단 근거는 `Import/PLAN_Montage_Locomotion.md`.

```
AnimGraph
  [Locomotion 상태기계] → Slot 'LocoSlot' → LocoCache
                                              ├→ Layered blend .BasePose
                                              └→ Slot 'UpperBody' → .BlendPoses_0
                                           Layered blend → Slot 'DefaultSlot' → Output

Actions        Loco_Idle → AM_Loco_Idle (Idle 8.33s)
               Loco_Walk → AM_Loco_Walk (Walk_Fwd_Manny 1.067s)
               Loco_Sit  → AM_Loco_Sit  (Eating_a_Burger 7.83s)  전부 LocoSlot · 블렌드 0.20
```

#### ⚠⚠ 슬롯 **순서**가 전부다

`LocoSlot` 이 `DefaultSlot` 보다 **앞**이라 행동 몽타주(먹기·`Idle_Look`)가
로코모션을 덮는다. 뒤집으면 걷기가 먹는 동작을 지운다.

⚠ 두 시스템이 **다른 슬롯**이라 안 싸운다. 같은 슬롯에 얹었다면 §6 의
  「`Auto Possess AI`」 항목과 똑같은 증상(1초쯤 다른 동작이 스침)이 났을 것이다.

#### ⚠ 상태 기계를 **안 지웠다** — 그게 되돌리기 장치다

슬롯은 몽타주가 없으면 Source 를 그대로 통과시킨다. 그 Source 가 상태 기계다.
→ `BP_Customer > bMontageLocomotion` 을 **끄면 즉시 예전 그림**으로 돌아간다.
에셋을 되돌릴 필요가 없다.

#### ⚠⚠ 무한 루프는 에셋이 아니라 **런타임**에서 건다

몽타주는 기본이 1회 재생이다. 끝나면 블렌드아웃되어 슬롯이 비고 상태 기계
포즈가 드러난다 — 「걷다가 갑자기 Idle 로 튄다」로 보인다.

```cpp
Anim->Montage_Play(M);
const FName Sec = M->GetSectionName(0);
Anim->Montage_SetNextSection(Sec, Sec, M);   // 자기 자신 → 끊김 없이 영원히
```

⚠ 세그먼트의 반복 횟수는 `loop_count` 가 **아니라 `looping_count`** 로 노출돼
  있다. 그리고 그것은 무한 스위치가 **아니다** — 0 으로 두면 몽타주 **길이가
  0** 이 되어 아무것도 안 나온다. 처음에 이걸 오해해서 「파이썬으로는 못 걸고
  에디터에서 손으로 해야 한다」고 계획에 적었었다. 둘 다 틀렸다.

⚠ 장식 루프(`PlayLoopedAction`)처럼 타이머로 다시 걸면 안 된다 — 걷기가
  1.067초라 **분당 83회**를 다시 걸어야 하고 타이밍이 흔들리면 보폭이 튄다.

#### ⚠⚠ 루트모션 플래그 — **리타게터가 소스에서 복사해 온다**

로코모션을 몽타주로 바꾸면서 조용히 터진 함정이다. 증상이 둘로 나뉘어 보여
원인을 찾기 어려웠다 — 「350 인데 느리게 간다」와 「회전이 안 먹는다」.

```
A_WalkFwd_Loop   EnableRootMotion=True · ForceRootLock=True   (마네퀸: root 본에 전진이 실려 있다)
Walk_Fwd_Manny   EnableRootMotion=True · ForceRootLock=True   ← 리타겟이 복사해 왔다
ABP_Customer     RootMotionMode = RootMotionFromMontagesOnly
```

세 줄이 맞물리면 —

```
AM_Loco_Walk 에 루트모션이 있다
  -> AnimInstance 가 뽑는다 (모드가 FromMontagesOnly 라서)
  -> CharacterMovement 가 **MaxWalkSpeed 를 무시하고** 루트모션으로 캡슐을 민다
  -> 그런데 이 애님은 `scale_horizontal=0` 으로 **제자리**로 구웠다 (전진 0.00cm)
  -> 루트모션이 0 -> **거의 안 움직인다**
```

⚠⚠ **전진량 0 은 「제자리로 잘 구워졌다」로 읽힌다.** 그게 함정이다 — 플래그가
  켜져 있으면 같은 0 이 **정반대 효과**를 낸다. 둘을 **같이** 봐야 한다.

⚠ `ForceRootLock` 도 꺼야 한다. Mixamo 는 **`Hips` 가 루트 본**이라, 루트를
  레퍼런스 포즈에 고정하면 걷기의 상하 바운스와 골반 높이가 통째로 죽는다.
  `root` 본이 따로 있는 마네퀸에서만 뜻이 있는 설정이다.

→ **손님 애님은 전부 둘 다 False.** 실측으로 `Walk_Fwd_Manny` 하나만 튀어 있었고
  `ll_fix_loco_rootmotion.py` 가 고쳤다. `ll_walk_retarget.py` 의 되읽기에도
  이 검사를 넣었다 — 예전에는 스켈레톤·움직임·골반Z·yaw·전진량만 보고
  **플래그를 안 봐서** 통과시켰다.

⚠ 몽타주는 따로 고칠 것이 없다. 루트모션 여부는 **물고 있는 시퀀스**가 정한다.

#### ⚠⚠ 틱을 켜야 했다

`ACustomerCharacter` 는 `bCanEverTick = false` 였다 (시간 세기는 트리가 했다).
그런데 **걷기 시작·정지는 이벤트가 없다** — 속도를 봐야만 안다. 상태 기계는 매
프레임 규칙을 평가해 줬지만 몽타주는 우리가 알아채고 걸어야 한다.

→ `bCanEverTick = true` · `TickInterval = 1/30`. 블렌드가 0.2초라 33ms 지연은
  안 보인다. `bMontageLocomotion` 이 꺼지면 `BeginPlay` 가 틱을 도로 끈다.

#### ⚠⚠ 히스테리시스가 **반드시** 필요하다

임계값이 하나면 목표 근처에서 감속할 때 속도가 5 언저리로 떨리고 Idle↔Walk 가
초당 수십 번 뒤집힌다. 그때마다 몽타주를 새로 걸어 **블렌드인이 계속 0 에서
다시 시작해 애님이 얼어붙는다.** 상태 기계는 전이 블렌드가 이걸 덮어 줬다.

```
걷기 시작:  Speed > LocoWalkSpeed + LocoSpeedHysteresis   (5 + 1)
멈춤:       Speed < LocoWalkSpeed − LocoSpeedHysteresis   (5 − 1)
```

⚠ `LocoWalkSpeed` 는 **상태 기계 전이와 같은 값**이라야 한다. 실측 `Speed > 5`.

#### ⚠ 앉음을 속도보다 **먼저** 본다

`SitAt` 이 순간이동으로 자리를 잡는 프레임에 속도가 튄다. 순서를 뒤집으면 앉는
순간 한 프레임 걷는 그림이 나온다.

#### 스크립트

| | |
|---|---|
| `ll_abp_structure.py` | 변수·상태·전이 규칙·슬롯을 통째로 찍는다 |
| `ll_loco_graph_probe.py` | 그래프 수술 **전** 배선 기록 |
| `ll_loco_slot.py` | `LocoSlot` 을 끼운다 (여러 번 돌려도 안전) |
| `ll_loco_slot_verify.py` · `ll_loco_montage_verify.py` · `ll_loco_apply_verify.py` | 단계별 검산 — **새 프로세스** |

⚠ `BlueprintService.describe_graph` 는 구조체가 아니라 **튜플**을 돌려준다.
  `get_editor_property('status')` 를 부르면 AttributeError 다.

### 남은 것 — `AgentRadius` (검증이 필요해 안 건드렸다)

`RecastNavMesh.AgentRadius` 가 **34** 로 캡슐 반지름 **34** 와 같다. 내비 가장자리가
캡슐이 닿는 자리와 사실상 붙어 있어, 경로가 가장자리를 타면 캡슐이 계속 기물을
스친다. **38~42** 로 올리는 것이 정석이다.

⚠ 그런데 올린 만큼 통로가 양쪽에서 깎인다. 좁은 통로가 **끊겨 좌석에 못 가게 될
수 있어** 값만 바꿔 두면 위험하다. 순서는 —

```
① AgentRadius 를 38 로
② 에디터에서 Build > Build Paths      (커맨드릿은 내비를 안 굽는다)
③ ll_nav_check.py 로 커버리지·슬롯 접근성 재확인
```

③ 이 통과할 때만 유지한다.

⚠ 충돌 형상 자체는 문제가 없다 — 배치된 24종 전부 단순 충돌이 있고 `BlockAllDynamic`,
셸(`LL_Shell`)은 complex-as-simple 이다. 통과는 충돌이 없어서가 아니라 **내비가
기물에 너무 붙어 있고 `MaxStepHeight` 가 좌면보다 높아서**였다.

---

### 9-4. 워크인 속도 5배 · 키오스크에서 시계 15도

`ll_walkin_tuning.py` 가 둘을 같이 넣는다.

#### 속도 350 — 폰과 NPC를 **같은 값으로 고정**

이 레벨에서만 둘 다 350 이다. 다른 레벨의 플레이어는 500(스프린트 1000) 그대로다.

| | 어디가 정하나 | 값 |
|---|---|---|
| 플레이어 폰 | `AFebriculaCharacter::LevelWalkSpeedOverrides` | `L_BurgerHouse → 350` |
| 손님 `BP_Customer` | CDO 의 `MaxWalkSpeed` | 350 (버거집에만 있는 액터다) |

⚠⚠ **`WalkSpeed` 자체를 350 으로 내리면 안 된다.** 모든 레벨이 같이 느려진다.
  한 레벨만 바꾸려고 만든 것이 그 맵이다.

⚠⚠ **`MaxWalkSpeed` 를 되돌리는 곳은 전부 `GetEffectiveWalkSpeed()` 를 써야 한다.**
  `EndSprint` 가 `WalkSpeed` 로 되돌리면 버거집에서 스프린트를 한 번 누르는
  순간 350 이 500 으로 풀려 **다시는 안 돌아온다.**

⚠ 레벨별 값은 **`BeginPlay` 에서** 붙인다. 생성자는 어느 맵으로 갈지 모른다.

⚠ 「고정」이 성립하려면 **스프린트 차단이 같이 걸려 있어야** 한다 (아래). 안
  그러면 눌러서 1000 이 된다.

⚠ 가감속은 손님 쪽 비율(속도 ×4 / ×6)을 유지했다 — 1400 / 2100.

#### 발 미끄러짐 — 애님과 **1.91배** 어긋남

`Walk_Fwd_Manny` 의 실측 보행 속도는 **183cm/s** 다.

| 캡슐 속도 | 어긋남 |
|---|---|
| 915 | 5.00배 |
| 500 | 2.73배 |
| **350** | **1.91배** ← 지금 |

맞추려면 `AM_Loco_Walk` 재생 속도를 1.91 로 올리거나, 빠른 걷기 애님을 리타겟해야
한다. 손님 스켈레톤에 그런 애님이 지금 없다. **어느 쪽도 안 했다** — 눈으로 정할 일이다.

⚠ `MaxStepHeight 30` · RVO `AvoidanceConsiderationRadius 200` 은 **느린 실내
  기준으로 맞춘 값**이다. 350 이면 200cm 를 0.57초에 지나므로 예전(183)보다는
  빠듯하다. PIE 에서 기물을 파고들거나 서로 밀치면 여기부터 볼 것.

#### 이 레벨에서만 스프린트를 막는다

`AFebriculaCharacter::SprintBlockedLevels` 에 `L_BurgerHouse` 가 들어 있다
(C++ 기본값이라 **어떤 플레이어 폰을 쓰든 따라온다**).

⚠⚠ **입력을 끊지 않고 동작만 막는다.** 스프린트는 전용 IMC 가 없고
  `Move`·`Look` 과 **같은 입력 컴포넌트**에 묶여 있어서, 매핑을 떼면 이동까지
  죽는다 (`SetupPlayerInputComponent`).

⚠ 판정은 `UGameplayStatics::GetCurrentLevelName(..., true)` 와 비교한다.
  `GetWorld()->GetMapName()` 을 그냥 쓰면 PIE 에서 `UEDPIE_0_L_BurgerHouse` 가
  나와 **에디터에서만** 비교가 실패한다 — 패키지에서는 맞고 PIE 에서만 틀리는,
  가장 늦게 발견되는 종류다.

⚠ 경로가 아니라 **이름**이다 (`L_BurgerHouse`).

⚠⚠ **퍼시스턴트 레벨 이름**이다. BurgerHouse 를 다른 맵의 서브레벨로 스트리밍해
  넣으면 이름이 안 맞아 **스프린트가 다시 풀린다.**

⚠ `EndSprint` 는 막힌 레벨에서도 그냥 되돌린다 — 조건을 달면 레벨을 옮기는
  도중에 키를 떼는 경우처럼 `Start` 는 통과했는데 `End` 가 안 도는 상황에서
  속도가 1000 에 박힌 채로 남는다.

#### 문 앞 대기를 없앴다 — **고정값이 아니라 몽타주 길이였다**

`Enter` 에 `Play Action Idle_Look` 이 있었고 전이가 `OnStateCompleted` 였다.
`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다** — 그게 곧 문 앞 대기였다
(기대값 10.8초 · 최대 18.5초). C++ 에 박힌 대기 시간 같은 것은 없었다.

```
전   Enter ▸ Play Action Idle_Look   ·  OnStateCompleted -> Kiosk
후   Enter ▸ (태스크 없음)            ·  OnTick           -> Kiosk
```

⚠⚠ **태스크를 남긴 채 전이만 바꾸면 안 된다.** `Play Action` 이 이미 몽타주를
  `DefaultSlot` 에 걸었고 그건 `LocoSlot` 보다 **뒤**라, 걷는 동안 로코모션을
  덮는다 (§9-2) — 손님이 폰을 보며 미끄러져 온다.

⚠ `Enter` 를 **지우면 안 된다.** `PE_Seat` · `PW_Wait` · `PO_Kiosk` 의
  `OnStateFailed` 가 전부 여기를 가리킨다 (자리를 못 잡았을 때의 회수 지점).

⚠ `Idle_Look` 자체는 남는다 — 줄에서 기다리는 `W_Idle` · `PW_Idle` 이 쓴다.

#### 키오스크 도착 정밀도 — `bReachTestIncludesAgentRadius` 가 범인이다

```
전   AcceptableRadius 5 + 에이전트 반지름 34 = 최대 **39cm** 떨어져 멈춘다
후   AcceptableRadius 5 + 0                  = 최대 **5cm**
```

`bReachTestIncludesAgentRadius = true` 면 **캡슐 가장자리**가 목표에 닿으면
도착으로 친다. 끄면 **캡슐 중심**이 들어와야 하므로 34cm 가 줄어든다.

⚠ 대신 캡슐이 키오스크 메시에 더 가까워진다. 파고들면 `AcceptableRadius` 를
  10~15 로 올리는 것이 다음 수순이다 — 그래도 예전 39cm 보다 정확하다.

⚠ 더 정확히 하려면 도착 시 **좌표를 스냅**해야 한다 (`SitAt` 이 좌석에서 하듯).
  순간이동이라 눈에 띄므로 안 했다.

#### 키오스크에서 회전 — `Play Action` 이 직접 돌린다

```
K_Act ▸ Play Action   ActionName=Interaction_Reach
                      FaceSlotKind=Kiosk · FaceYawOffset=15
```

⚠⚠ **`SlotPoint` 의 `UseYawOffset` 을 쓰면 안 된다.** 도착 시 아무도 그걸 안
  읽기 때문이다 — `ClaimSlot` 이 `UseRotation` 을 내놓지만 트리가 쓰지 않는다.
  슬롯 값을 아무리 바꿔도 **화면이 안 변한다.**

⚠ **양수가 시계 방향**이다 (UE 는 X 정면 · Y 오른쪽이라 `+Yaw` 가 오른쪽으로 돈다).

⚠ `FaceYawOffset` 은 한동안 **15** 였다가 지금은 **0** 이다 — 도착 위치를 정밀하게
  고친 뒤로 각도를 틀 이유가 없어졌다. `FaceSlotKind` 는 **남긴다**: 그건 각도
  조정이 아니라 「키오스크를 등지고 서는」 버그를 고친 것이다.

⚠ 손님이 **그 슬롯을 이미 잡고 있어야** 찾는다 (`ASlotPoint::ClaimedBy`). 못
  찾으면 경고만 찍고 그냥 재생한다 — 방향 때문에 손님 루프가 깨지면 안 된다.

⚠ 다른 `Play Action` 은 `FaceSlotKind` 가 비어 있어 **회전하지 않는다** (기본값).
  `PE_Eat` · `PW_Idle` · `PO_Act` · `W_Idle` · `P_Act` · `Eating` 전부 그대로다.

##### ⚠⚠ 회전 전에 **반드시 멈춰야 한다**

`K_Move` 가 끝나고 `K_Act` 가 들어오는 순간 손님은 아직 **감속 중**이다. 그
상태에서 회전만 넣으면 `bOrientRotationToMovement = true` 가 다음 몇 프레임에
걸쳐 캐릭터를 **속도 방향으로 되돌린다** — 「15도가 어긋난다 / 회전 중심이
이상하다」로 보인다.

```cpp
AIController::StopMovement()      // 경로 추종 중단
StopMovementImmediately()         // 속도 0
StopActiveMovement()              // **가속도** 0  ← orient 가 보는 값
```

⚠⚠ `StopMovementImmediately` 만으로는 **안 된다.** 그건 속도만 0 으로 만들고
  `Acceleration` 은 남기는데, orient-to-movement 가 보는 것이 바로 그 가속도다.

⚠ **회전 중심은 원래부터 발밑 수직선 위다.** 액터 원점이 캡슐 중심(발밑보다
  88cm 위)이라 헷갈리기 쉬운데, yaw 는 수직축 둘레라 원점 높이와 무관하게 X·Y 가
  안 변한다. 메시 컴포넌트의 relative location 도 `(0, 0, −90)` 으로 X·Y 가 0 이고,
  애님의 골반 수평 오프셋도 `Reaching_Out` 기준 최대 4.1cm 다 — 눈에 띄는 공전을
  만들 크기가 아니다. **중심이 아니라 위에서 말한 되돌림이 원인이었다.**

실측: 레벨의 Kiosk 슬롯 2개 모두 액터 yaw 60 · `UseYawOffset` 0 이므로
사용 각도가 60 → **75** 가 된다.

### 9-3. 주문 알림음 — 레벨의 소리 액터를 트리에서 울린다

워크인 손님이 키오스크 `Play Action` 을 **끝낸 직후** 레벨의 `AmbientSound` 가 운다.

```
K_Act (Play Action Interaction_Reach)
   ↓ OnStateCompleted          ← 「Play Action 을 완료한 다음」의 근거가 이 전이다
K_Order ├ Place Order
        └ Play Sound At Tagged Actor   (ActorTag = OrderChime)
                ↓ 태그로 찾는다
        레벨의 AmbientSound (Tags: OrderChime · AutoActivate 꺼짐)
```

⚠ 프리필 갈래(`PO_Act` → `PO_Order`)에는 **안 얹었다.** 요청이 워크인이었고,
  `Auto Possess AI` 를 끈 뒤로 프리필은 트리를 타지도 않는다 (§6).

#### ⚠⚠ 왜 액터 참조가 아니라 **태그**인가

`ST_Customer` 는 **에셋**이고 `AmbientSound` 는 **레벨 액터**다. 에셋은 레벨
액터를 직접 못 가리킨다 — 에디터에서 꽂아도 저장되지 않는다. 그래서 이름표로
런타임에 찾는다.

⚠ **액터 태그**다 (`Actor > Tags`). 컴포넌트 태그에 넣고 헷갈리는 실수가 잦아서
  태스크가 그 경우를 따로 경고한다.

#### ⚠⚠ 이 태스크는 **절대 실패하지 않는다**

`Failed` 를 돌려주면 `K_Order` 가 실패하고 전이가 새서 **손님이 주문을 못 하고
문으로 되돌아간다.** 소리 하나 때문에 손님 루프가 깨지는 것은 말이 안 된다.

→ 태그를 못 찾아도·`Sound` 가 비어 있어도 **경고만 찍고 `Succeeded`**.

⚠ 그래서 「소리가 안 난다」의 원인은 화면이 아니라 **로그**에 있다
  (`LogBurgerHouseST`). 증상이 「아무 일도 안 일어남」이라 조용히 실패하는 쪽이다.

#### ⚠ `Auto Activate` 를 꺼야 한다

`AAmbientSound` 는 기본이 「레벨이 열리면 계속 운다」다. 그대로 두면 주문과
상관없이 처음부터 울고 있어서 태스크가 `Play()` 를 불러도 **달라지는 게 없다.**
「트리거가 안 먹는다」로 보이지만 원인은 이쪽이다.

#### 다른 데서도 쓸 수 있다

태스크는 범용이다 (`Play Sound At Tagged Actor`). 다른 상태에 얹고 태그만 바꾸면
된다. `Sound` 를 채우면 그 액터 **위치에서** 그 소리를 한 번 내고, 비워 두면
액터가 들고 있는 `AudioComponent` 를 튼다.

---

## 9-7. ⚠⚠ 손님이 `Wait` 에서 안 나가던 원인 — **태그 접두사**

```
C++ 가 쏘는 것       BurgerHouse.Order.Ready      (BurgerHouseTags.cpp)
트리가 기다린 것      Burgeria.Order.Ready         ← 개명 전 이름
```

프로젝트가 `Burgeria` → `BurgerHouse` 로 개명될 때 **C++ 태그만 바뀌고 StateTree
에셋의 `RequiredEventTag` 는 옛 이름 그대로** 남아 있었다.

### 왜 이렇게 찾기 어려웠나

`UStoreSubsystem::PushEvent` 는 트리를 찾아 이벤트를 **정상적으로 보낸다.** 트리가
그 태그를 안 기다릴 뿐이다 —

- **에러도 경고도 없다.** 전송은 성공한다
- 증상은 「손님이 줄에서 안 나간다」 **하나뿐**이다
- 조리·주문·`Ready` 로그는 전부 정상으로 찍힌다

실측 로그가 그림을 정확히 보여 준다:

```
08:19:50  주문 1 접수 (BP_Customer_C_5) — 조리 12.0s
08:20:02  주문 1 준비됨               ← Ready 는 났다
          (이후 100초간 아무 일도 없음)
```

`Pickup` 에 들어갔다면 `ClaimSlot(Pickup)` 이 찍혔을 텐데 **그것이 없다** →
전이 자체가 안 걸린 것이다.

### 죽어 있던 전이는 **셋**

| 태그 | 죽으면 |
|---|---|
| `Order.Ready` | 주문이 나와도 **픽업하러 안 간다** (관측된 증상) |
| `Queue.Advanced` | 앞사람이 빠져도 **줄이 안 당겨진다** |
| `Order.Lost` | 주문이 사라져도 **안 나간다** |

`Wait` 와 `PW_Wait` 양쪽에 셋씩, 총 **6개**를 `ll_fix_event_tags.py` 가 고쳤다.

### ⚠ 재발 방지 — `PushEvent` 가 이제 **보낸 태그를 찍는다**

```
LogBurgerHouse: Verbose: 이벤트 BurgerHouse.Order.Ready -> BP_Customer_C_5
```

⚠ `Verbose` 다. 보려면 `Log LogBurgerHouse Verbose`. 이 줄이 없으면 어느 태그가
  오갔는지 알 길이 없어, 「이벤트가 안 온다」와 「이벤트는 왔는데 아무도 안 받는다」를
  구별할 수 없다.

⚠⚠ **태그를 옮길 때는 C++ 과 에셋을 같이 본다.** C++ 은 컴파일이 잡아 주지만
  에셋의 `RequiredEventTag` 는 **문자열이라 아무도 안 잡는다.**

---

## 9-6. 트레이 세트 · 양손 들기 · 주문 알림음

### ⚠⚠ 트레이 메시가 **애초에 비어 있었다**

`BP_Customer > Tray` 의 `StaticMesh` 가 `None` 이었다. `HoldTray`·`PlaceTrayAt`·
`DropTray` 와 트리의 `Tray` 태스크 5곳이 멀쩡히 돌고 있었는데 **보이는 것이 없어서**
아무도 눈치채지 못했다. 이번에 `Props/Tray` 를 꽂았다.

### 트레이 위에 얹히는 것

```
Tray (30 × 40 × 2.2)
 ├ BurgerBox   SM_BurgerBox  ×5.0        -> 12.6 × 13.1 × 6.6
 ├ Fries       SM_Fries      ×0.4        ->  9.8 ×  4.6 ×  9.6
 └ Receipt     Plane (0.08, 0.15, 1)     ->  8   × 15        (평평한 흰 종이)
```

⚠⚠ **전부 `Tray` 의 자식**이다. 그래서 `HoldTray`/`PlaceTrayAt`/`DropTray` 가
  트레이를 옮기면 **같이 따라간다** — 손에 붙이는 코드도, 테이블에 놓는 코드도
  손댈 필요가 없었다.

⚠ 숨김도 `SetHiddenInGame(true, **bPropagateToChildren=true**)` 로 내린다.
  빠뜨리면 트레이는 숨었는데 **음식만 공중에 뜬 채로** 보인다.

⚠ 콜리전은 전부 껐다. 켜 두면 손에 든 음식이 손님 캡슐을 밀거나 내비를 막는데,
  증상이 「손님이 이상하게 걷는다」로 보여 원인 찾기가 고약하다.

⚠ 임포트 원본은 단위가 제각각이었다 — 버거박스 **2.5cm**(너무 작음) · 감자
  **24.5cm**(너무 큼). 위 스케일은 실측에서 나온 값이다.

### ⚠⚠ 양손으로 들려면 **애님**이 그래야 한다

트레이를 몸통에 붙이는 것만으로는 **손이 따라오지 않는다.** 손 위치는 애님이 정한다.

```
TraySocket   RightHand      ->  spine2      (한 손에 대롱대롱 매달리던 것)
애님         Walk_Fwd_Manny ->  Carrying    (bHasTray 일 때만)
```

`ACustomerCharacter::UpdateLocomotionMontage` 가 `bHasTray` 를 보고
`Loco_CarryWalk`/`Loco_CarryIdle` 로 갈아 끼운다 (`AM_Loco_Carry*`, `LocoSlot`).

⚠ 그 이름이 `Actions` 맵에 없으면 `PlayAction` 이 몽타주를 못 찾아 시간만 세고
  슬롯이 비어 상태 기계 포즈가 나온다 — 「트레이는 들었는데 팔이 그대로」로 보인다.

⚠ `Carrying` 시퀀스의 루트모션 플래그도 확인해서 껐다 (§9-2 의 그 함정).

### 주문 알림음

```
Wait ──(Order.Ready)──> Pickup ▸ Play Sound At Tagged Actor (OrderReadyChime · 0.2)
                                       ↓
                         AmbientSound_OrderReady  (픽업 카운터 위 z+150)
                         Sound = SW_Chime_1 · AutoActivate **꺼짐**
```

⚠ 「주문이 나온 순간」은 `Wait` 가 `Order.Ready` 이벤트로 `Pickup` 에 들어가는 때다.
  `K_Order` 의 알림음(§9-3)은 **주문을 넣을 때**라 서로 다른 소리다.

### 들고 자리까지 가는 경로는 **원래 있던 것**이다

```
P_Take (Claim Order + Tray[Hold]) → S_Move (Move To) → S_Eat (Tray + Sit At Seat)
```

이번에 바꾼 것은 **트레이에 무엇이 얹히나**와 **드는 동안 어떤 애님인가** 뿐이다.

### ⚠ 눈으로 맞춰야 하는 값

| | 지금 값 | |
|---|---|---|
| `TrayHandOffset` | 위치 (18, 0, −8) · yaw 90 | 몸통 기준 **추정** |

음식 세 개의 위치는 **더 이상 추정이 아니다** — §9-8 에서 바운즈로 역산했다.
전부 `BP_Customer` 에서 편집 가능하다.

---

## 9-8. ⚠⚠ 「감자만 스폰된다」 — 손으로 찍은 오프셋이 음식을 묻었다

### 증상

트레이 위에 버거박스·감자·영수증 셋을 얹었는데 **감자만 보였다.** 스폰은 셋 다
되고 있었다. 에러도 경고도 없다.

### 원인 — 메시마다 **바운즈 원점이 제각각**이다

| 메시 | 실제 크기(cm) | 바운즈 원점 | 함정 |
|---|---|---|---|
| `Tray` | 30 × 40 × **2.2** | (0, 0, 1.1) | 상판이 **z = 2.2** |
| `SM_BurgerBox` | **2.5 × 2.6 × 1.3** | (0, 0, 0) | 원점이 **한가운데** |
| `SM_Fries` | 24.5 × 11.6 × 24.0 | **(4.4, −15.1, 12.3)** | 원점이 **메시 바깥** |
| `Plane`(영수증) | 100 × 100 × **0** | (0, 0, 0) | **두께 0** |

- **버거박스** — 원점이 한가운데인데 z=2.0 에 놨다. ×5 면 반높이가 3.25 이니
  **바닥이 z = −1.25**. 트레이(0~2.2)를 아래로 뚫고, 남은 윗부분은 트레이 벽에
  가려 거의 안 보인다.
- **영수증** — z=1.2 인데 트레이 상단이 2.2 다. **트레이 안에 묻혔다.**
- **감자** — 원점이 메시보다 +z 12.3 만큼 위에 있어서, 같은 z=2.0 을 줬는데도
  **우연히** 상판 위로 떠올랐다. 그래서 혼자 보였다.

⚠⚠ 셋 다 같은 z 를 줬는데 결과가 갈렸다. 「같은 숫자를 줬으니 같은 높이」가
  **아니다** — 원점이 다르면 다른 높이다.

### 고친 법 — 숫자를 찍지 말고 **역산**한다

회전이 없을 때,

```
월드 bbox 중심 = loc + origin*scale
월드 bbox 바닥 = loc.z + (origin.z − extent.z)*scale
```

「바닥을 상판에 놓고 중심을 (Xc,Yc) 에 둔다」를 풀면

```
loc.x = Xc − origin.x*scale
loc.y = Yc − origin.y*scale
loc.z = Zsurf − (origin.z − extent.z)*scale
```

`ll_tray_layout.py` 가 이 식으로 뽑는다. 결과 — 셋 다 **바닥 z = 2.25** 로 정렬:

```
Tray (X 30 × Y 40), 긴 축 Y 를 따라 늘어놓는다
 ├ BurgerBox  loc( 0.00, −10.00, 5.57)  ×5.00              12.6 × 13.1 × 6.6
 ├ Fries      loc(−1.77,   9.05, 2.12)  ×0.40               9.8 ×  4.6 × 9.6
 └ Receipt    loc( 0.00,  14.00, 2.25)  ×(0.08, 0.10, 1)    8   × 10
```

⚠ 앞으로 트레이에 뭘 더 얹을 때도 **이 식으로** 뽑을 것. 눈대중 오프셋은
  반드시 한 번은 묻힌다.

⚠ 두께 0 인 `Plane` 은 상판에 딱 붙이면 z-파이팅이 난다. **+0.05** 띄웠다.

### ⚠ 음식 텍스처가 처음부터 없었다

`ll_food_import.py` 를 `import_textures=True` 로 돌렸는데 **텍스처가 하나도
안 따라왔다.** 원본이 `.obj`/`.fbx` 는 `source/`, 텍스처는 `textures/` 로
**폴더가 갈려** 있었고 임포터는 파일 옆만 본다.

⚠⚠ 그래도 **빈 머티리얼은 생겨서 메시에 꽂힌다.** 그래서 임포트는 「성공」으로
  보이고, 회색으로 나오는 것을 **눈으로 봐야만** 안다.

⚠ 임포터가 만드는 것은 `MaterialInstanceConstant` 라
  `MaterialEditingLibrary.create_material_expression` 이 **거부한다.**
  `ll_food_materials.py` 에서 `Material` 을 새로 만들어 물렸다.

```
Props/Food/Textures/  T_BurgerBox_BC · T_BurgerBox_N · T_Fries_BC · T_Fries_N
Materials/            M_BurgerBox · M_Fries · M_Receipt
```

⚠ 노멀맵은 `TC_NORMALMAP` + `srgb=False` 로 바꿔야 한다. 안 바꾸면 표면이
  번들거리고 음영 방향이 틀어진다.

### ⚠⚠ `unreal.Rotator` 는 **(roll, pitch, yaw)** 순서다

카운터 트레이를 `unreal.Rotator(0, 90, 0)` 으로 스폰했다. yaw 90 을 준 줄
알았으나 들어간 것은 **pitch 90** — 트레이가 옆으로 **서 있었다.**

⚠ 되읽기에서 `yaw` 만 찍어 봤더니 0 이라 정상으로 보였다. **셋 다 찍을 것.**

### 카운터 트레이 위치의 근거

```
x 255    픽업 슬롯 2개(x 220 · 290)의 중점
y −1110  픽업 슬롯 y − 70 (POS 와 Trays 사이)
z 105    블렌더 Cnt_Counter z 0~1.050 m -> 상판
yaw 90   트레이 긴 축(Y 40)을 카운터 길이 방향으로 눕힌다 = 「가로」
```

⚠ 이 좌표는 아직 **추정**이다 — 블렌더 문서에 `Cnt_POS`·`Cnt_Trays` 의 좌표가
  없고 `Cnt_Counter` 만 있다. 눈으로 한 번 볼 것.

---

## 9-9. 라운드 2 — 나머지 잔손질

| | 무엇 | 어디 |
|---|---|---|
| ① | `OrderChime` 볼륨 0.2 → **0.05** | `ST_Customer ▸ Root/Kiosk/K_Order` |
| ⑤ | 자리로 **순간이동**하던 것 | 아래 |
| ⑥ | 프리필과 워크인이 **같은 테이블**에 앉던 것 | `UStoreSubsystem::ResolvePartyTable` |

⚠ 주문 **나올 때** 울리는 `OrderReadyChime` 은 0.2 그대로다 — 서로 다른 소리다.

### ⑤ 왜 자리로 순간이동처럼 보였나

`ACustomerCharacter::SitAt` 은 좌석 트랜스폼으로 **스냅**한다
(`SetActorLocationAndRotation(..., ETeleportType::TeleportPhysics)`). 걸어서
가까이 오지 않으면 그 스냅이 통째로 「순간이동」이 된다.

`StateTreeMoveToTask` 의 `bReachTestIncludesAgentRadius = true` 면 **캡슐
가장자리**가 닿을 때 도착으로 쳐서 최대 **39cm** 떨어져 멈춘다 — 그 39cm 가
전부 순간이동이었다.

```
K_Move  이미 false 였다 (§9-4 에서 키오스크 정확도 잡을 때 껐다)
S_Move  true -> false     좌석
P_Move  true -> false     픽업
W_Move  true -> false     대기줄
```

⚠ 도착 오차 39cm → **5cm**. 완전히 없애려면 `SitAt` 을 스냅 대신 보간으로
  바꿔야 하는데, 그건 애님 루프와 얽혀 있어 손대지 않았다.

### ⑥ 테이블이 겹치던 것

`ResolvePartyTable` 이 **빈 좌석만** 보고 골랐다. 프리필 5명이 앉아 있는
테이블에도 빈 자리가 남아 있으면 워크인이 그리로 갔다.

이제 **남이 잡은 테이블을 통째로 뺀다** —

```cpp
TSet<int32> TablesTakenByOthers;
for (TActorIterator<ASlotPoint> It(GetWorld()); It; ++It)
{
    // Seat 이고, 누가 잡고 있고, 그 누가 **내 파티가 아니면** 그 GroupId 를 뺀다
}
```

⚠ 뺐더니 **남는 테이블이 없으면** 그때는 합석을 허용한다(로그를 남긴다). 손님이
  영원히 못 앉는 것보다는 겹치는 편이 낫다.

⚠⚠ 「테이블마다 변수를 두자」는 방향은 **일부러 안 했다.** 예약 주체가 둘이 되면
  손님이 사라졌을 때 유령 예약이 남는다 — §9.1 의 「소유자를 분리하는 것이 설계의
  전부다」. 좌석의 `ClaimedBy` **하나만**이 진실이고, 테이블 배제는 그것을 읽어서
  **계산**한다.

---

## 9-10. ⚠⚠ 「손님이 출입문만 본다」 — 애니메이션이 아니었다

증상은 하나인데 **원인이 둘**이고, 둘 다 애님이 아니라 **좌표 설정**이었다.

### 원인 A — 키오스크 슬롯 yaw 가 **가구의 yaw 그대로**였다

```
키오스크 가구     (570, −275)  yaw 60     <- 블렌더 rot_z −60 을 y반전한 값
슬롯 LocalOffset  (0, 60, 0) 로컬        -> 손님은 (518, −245) 에 선다
슬롯 UseYawOffset 0                      -> 손님도 yaw 60 을 본다
```

손님이 선 자리에서 키오스크까지의 방향은 `atan2(−275+245, 570−518)` = **−30°**
인데 **60°** 를 보고 있었다. 정확히 **90도** 어긋난다 — 그리고 60° 는 +Y 쪽,
곧 출입문(420, −55)이 있는 쪽이다.

⚠⚠ `ASlotPoint::UseYawOffset` 이 **바로 이것을 위한 손잡이**다 (주석: 「가구
  정면이 캐릭터 정면(+X)과 다르면 yaw 를 더 돌려야 한다」). 아무도 안 채워
  넣었을 뿐이다. 의자·소파는 채워져 있고 키오스크만 0 이었다.

고친 값은 눈대중이 아니다 — 오프셋이 **로컬 +Y** 니까 바라볼 방향은 **로컬 −Y**,
그래서 `UseYawOffset = −90` 이 유일한 답이다.

```
SLOT_Kiosk_00 / 01   UseYawOffset 0 -> −90   (바라보는 yaw 60 -> −30)
```

### 원인 B — 대기열은 **아무도 방향을 안 잡아 줬다**

`W_Idle` 의 `FaceSlotKind` 가 비어 있었다. 대기 자리는 `ASlotPoint` 가 아니라
`Get Queue Spot` 이 `QueueStart + QueueStep*index` 로 **계산**하는 좌표라 슬롯
기반 회전으로는 애초에 잡을 수 없다.

그래서 손님은 `bOrientRotationToMovement` 가 남긴 **마지막 진행 방향** 그대로 섰다.

```
QueueStart (220, −960)   QueueStep (0, +70)   -> 줄은 +Y, 곧 문 쪽으로 뻗는다
픽업 카운터 y −1110                           -> 봐야 하는 쪽은 −Y = yaw −90
```

C++ 에 **절대 각도** 길을 냈다 —

```cpp
// FBurgerHouseActionInstanceData
bool  bFaceWorldYaw = false;   // FaceSlotKind 가 비었을 때만 본다
float FaceWorldYaw  = 0.f;
```

`FaceClaimedSlot` 은 「멈추고 돌린다」 부분을 `FaceYaw(C, Yaw)` 로 뽑아 두 길이
같은 코드를 쓴다 — **멈추지 않으면 회전이 다음 프레임에 풀린다**는 것이 이
함수의 존재 이유라 갈라 두면 한쪽만 고치게 된다.

### 같이 고친 곳

| 상태 | 전 | 후 |
|---|---|---|
| `K_Act` (워크인 주문) | `FaceSlotKind=Kiosk` | 그대로 — 슬롯 yaw 가 고쳐져 이제 맞는다 |
| `PO_Act` (프리필 주문) | 비어 있음 | `Kiosk` |
| `P_Act` (픽업) | 비어 있음 | `Pickup` — 슬롯 yaw −90 이 이미 카운터를 본다 |
| `W_Idle` · `PW_Idle` (대기) | 비어 있음 | `bFaceWorldYaw` + `−90` |

⚠ `P_Act` 는 요청에 없었지만 **같은 버그**다. 픽업대에서 카운터를 등지고 손을
  뻗고 있었다.

### ⚠ 검산은 각도로 되짚는다

「−90 을 넣었다」는 검산이 아니다. 손님이 서는 자리와 가구 위치로 **필요한 yaw 를
다시 계산해** 실제 값과 견준다 (`ll_fixes_round3_verify.py`) —

```
SLOT_Kiosk_00  손님(518,−245) -> 키오스크(570,−275)  필요 −30.0 · 실제 −30.0 · 오차 0.0
```

---

## 9-11. 좌석 순간이동 — 스냅을 **미끄러짐**으로

§9-9 에서 도착 오차를 39cm → 5cm 로 줄였지만 여전히 튀었다. 남은 거리를 없애는
것이 아니라 **한 프레임에 없애는 것**이 문제였기 때문이다.

```cpp
// ACustomerCharacter
float SeatBlendTime = 0.35f;   // 0 이면 예전처럼 즉시 스냅
void  BeginSeatBlend(const FTransform& To, bool bRestoreWalkingWhenDone);
```

`SitAt` 과 `StandUp` 이 둘 다 이것을 쓴다. `Tick` 이 `SmoothStep` 으로 보간한다.

⚠ **로코모션보다 먼저** 보간한다 — 위치를 옮긴 뒤 속도를 봐야 한다.

⚠ 등속으로 옮기면 출발·도착에서 툭 끊긴다. `SmoothStep` 이 양끝을 죽인다.

⚠ 미끄러지는 동안 이동 모드는 이미 `MOVE_None` 이라 캡슐이 의자를 안 민다.
  그래서 위치를 직접 써도 안전하다.

⚠⚠ `StandUp` 은 `MOVE_Walking` 복구를 **보간이 끝난 뒤**로 미룬다. 도중에 켜면
  캡슐이 의자 **안에서** 움직이기 시작해 기물을 뚫는다.

⚠ `bMontageLocomotion` 이 꺼져 있으면 `BeginPlay` 가 틱도 끈다. `BeginSeatBlend`
  가 틱을 켜고, 보간이 끝나면 `SetActorTickEnabled(bMontageLocomotion)` 으로
  되돌린다 — 안 되돌리면 앉은 손님 전원이 영구히 틱을 돈다.

⚠ 너무 길면 앉는 애님이 먼저 끝나 **공중에서 앉은 자세로 미끄러진다.** 0.3~0.4
  근처가 눈에 안 띈다.

---

## 9-12. ⚠⚠ 트레이를 들었는데 **음식이 안 보이던** 것

```cpp
// 생성자
Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);   // 자식도 각자 true 가 된다

// HoldTray — 전파를 안 줬다
Tray->SetHiddenInGame(false);                                 // ← 트레이만 보인다
```

전파로 내리면 자식이 **각자** 숨김 플래그를 든다. 전파 없이 올리면 부모만 풀려
**버거박스·감자·영수증은 계속 숨어 있다.** `HoldTray`·`PlaceTrayAt`·`DropTray`
셋 다 전파를 안 주고 있었다.

⚠ 카운터 트레이(`ACounterTray`)는 액터째 `SetActorHiddenInGame` 이라 이 함정이
  없다 — 그래서 **카운터 위에서는 음식이 보이고 손에서는 안 보이는** 그림이
  나왔고, 원인이 애님 쪽으로 보였다.

### 들 때의 애님은 배선 자체는 맞았다

```
Actions          Loco_CarryWalk -> AM_Loco_CarryWalk
                 Loco_CarryIdle -> AM_Loco_CarryIdle
P_Take ▸ Tray    Op = Hold          -> bHasTray = true
UpdateLocomotionMontage            -> bHasTray 면 Carry 쪽으로 갈아 끼운다
```

안 보인 것은 **트레이 위의 물건**이지 팔 동작이 아니었다.

---

## 9-13. 라운드 3 — 나머지

| | |
|---|---|
| `OrderChime` | 0.05 → **0.02** (주문 나올 때 `OrderReadyChime` 은 0.2 그대로) |
| 카운터 트레이 | y −1110 → **−1140** (카운터 쪽 −Y 로 30) |
| 버거박스·감자 | **×1.5** (5.0→7.5 · 0.4→0.6) |
| 영수증 | **×0.5** ((0.08,0.10)→(0.04,0.05)) |
| `Telling_A_Secret` | 프로젝트에서 **삭제** |

크기를 바꾸면 위치도 다시 풀어야 한다 — §9-8 의 역산식을 그대로 쓴다. 셋 다
바닥 z = 2.25 를 유지하고, 트레이(X 30 × Y 40) 밖으로 나가지 않는지 검산이 본다.

```
BurgerBox  ×7.50  18.8 × 19.6 × 10.0  중심( 0, −9)
Fries      ×0.60  14.7 ×  7.0 × 14.4  중심( 0,  6)
Receipt    ×0.04   4.0 ×  5.0 ×  0.0  중심( 0, 15)
```

### `Telling_A_Secret` 을 지운 순서

⚠⚠ **참조를 먼저 끊고 지운다.** `Idle_Look` 변형에서 빼고 `BP_Customer` 를 저장한
  **뒤에** 에셋을 지웠다. 순서를 뒤집으면 삭제해도 블루프린트가 다시 로드한다.

⚠ `EditorAssetLibrary.delete_asset` 은 **True 를 주고도 파일을 남긴 적이 있다**
  (§6). 지운 뒤 `git status` 로 `D` 가 찍히는지 눈으로 봤다.

```
Idle_Look  AM_Idle · AM_Telling_A_Secret · AM_Texting  ->  AM_Idle · AM_Texting
```

문서·스크립트(`ll_montages.py` · `ll_sit_candidates.py` · 이 문서)의 이름도 같이
지웠다.

---

## 9-14. ⚠⚠⚠ 「자리로 순간이동한다」 — 원인이 **셋**이었고 전부 딴 데 있었다

§9-9(도착 판정)도 §9-11(좌석 보간)도 **증상 완화**였다. 화면만 보고 두 번 엉뚱한
곳을 고쳤다. 헤드리스 PIE 를 돌려 `LogStateTree Verbose` 로 전이를 찍고서야 보였다.

```
UnrealEditor-Cmd.exe <uproject> /Game/BurgerHouse/Maps/L_BurgerHouse
    -game -unattended -nosplash -nullrhi -nosound
    -LogCmds="LogStateTree Verbose, LogBurgerHouseST Verbose, LogBurgerHouseNPC Verbose"
    -log=pie.log
```

⚠⚠ **이 방법을 먼저 쓸 것.** 이 로그 한 번이 며칠치 추측을 이긴다.

### 계측이 먼저 — 한 틱 도약 검출기

`ACustomerCharacter::TeleportWarnDistance`(기본 40cm). 한 틱에 그보다 크게
움직이면 **위치·앉음·보간·속도**를 같이 찍는다.

⚠ 틱 간격이 1/30 이라 350cm/s 로 걸어도 한 틱에 12cm 다. 40 은 그 세 배 —
  걷기로는 안 찍히고 스냅만 찍히는 값이다.

첫 측정에서 **도약 46건, 전부 `좌석보간=1` · 거리 4.5m**. 즉 보간은 제대로
돌고 있었고, 문제는 **보간이 메워야 할 거리가 4.5m 라는 것**이었다.

### 원인 A — 알림음 태스크가 `Pickup` 상태를 통째로 끝냈다

```
Enter  Root/Pickup ; P_Move
       Task 'Claim Slot'.EnterState()
       Task 'Play Sound At Tagged Actor'.EnterState()   <- Succeeded 를 돌려줬다
       State Completed Succeeded                        <- **같은 프레임**
       Transition  Pickup -> Seat
```

`Root/Pickup` 의 완료 정책이 `Any` 라, §9-3 에서 붙인 **연출용** 태스크가
`Succeeded` 를 돌려주는 순간 픽업 갈래 전체(`P_Move`·`P_Act`·`P_Take`)가
건너뛰어졌다. 손님은 픽업대로 걸어가지도, **트레이를 받지도 않고** 카운터에서
좌석으로 날아갔다.

⚠⚠ 에러가 하나도 없다. 손님은 멀쩡히 앉고 먹고 나간다 — **중간이 빠졌을 뿐**이다.

고침: `FBurgerHousePlaySoundTask` 의 모든 `return` 을 **`Running`** 으로.

⚠⚠ **스스로 끝날 일이 없는 태스크는 끝났다고 말하면 안 된다.** `Claim Slot` 이
  영원히 `Running` 인 것과 같은 이유다. 상태를 끝내는 것은 자식 상태와 전이지
  연출 태스크가 아니다. 앞으로 태스크를 새로 붙일 때 **제일 먼저 볼 것.**

### 원인 B — `S_Move` 가 진입한 프레임에 `Failed`

```
Enter  Root/Seat ; S_Move
       Task '이동'.EnterState()
       State Completed **Failed**          <- 같은 프레임
       Transition  S_Move -> S_Eat
```

⚠ `S_Move → S_Eat` 전이가 성공/실패를 **안 가린다.** 그래서 실패해도 그냥 앉는다.

바인딩을 하나씩 바꿔 가며 PIE 로 재 봤지만 전부 그대로였다 —

| 시도 | 결과 |
|---|---|
| `bRequireNavigableEndLocation = false` | 그대로 Failed |
| `AIController` 를 컨텍스트에 바인딩 | 그대로 Failed |
| `TargetActor` 바인딩 해제 | 그대로 Failed |
| `Destination` 을 내비 위 리터럴로 | 그대로 Failed |

⚠ 파이썬 API 로는 **바인딩을 읽을 수가 없다** (`EditorData` 접근 불가,
  `get_state_bindings` 없음). 그래서 여기까지가 스크립트의 한계였다.

⚠ `S_Move` 는 지금도 죽어 있다. 아래 C 가 그 일을 대신하므로 동작에는 문제가
  없지만, **에디터에서 한 번 열어 볼 값**이다.

### 원인 C — 좌석 지점은 **애초에 갈 수 없는 자리**다

`AAIController::MoveToLocation` 을 직접 불러 보니 그것도 **`Failed`(0)** 였다.
바인딩 문제가 아니었다.

```
좌석 사용 지점 = 의자 원점 + LocalOffset(좌면 높이만큼 위, 앞으로 조금)   §9.2
의자 = 내비메시 **구멍**
-> 그 좌표는 내비 밖이고, 엔진 기본 투영 범위로는 못 건진다
```

⚠⚠ `bProjectDestinationToNavigation = true` 로도 실패한다. **엔진 기본 투영
  범위가 좁아서**다. `TeleportToPoint` 가 쓰는 것과 같은 넉넉한 범위
  (`150 × 150 × 300`)로 **우리가 직접 투영**하면 건져진다.

그래서 `FBurgerHouseSitTask` 가 **스스로 걸어간다** —

```
EnterState  좌석까지 ApproachRadius(60) 밖이면
              ProjectPointToNavigation(좌석, 150×150×300)
              MoveToLocation(투영된 자리, AcceptanceRadius = 10)
            안이면 그 자리에서 앉는다
Tick        경로 추종이 끝나면(또는 ApproachTimeout 8초) 앉는다
```

⚠ `AcceptanceRadius` 에 `ApproachRadius` 를 넣으면 **그만큼 더 멀리서 멈춘다.**
  실측으로 투영 56cm + 반경 60cm = 116cm 를 미끄러졌다. 10 으로 낮춰 64cm.

⚠ **시간 제한이 있어야 한다.** 경로가 막히면 손님이 영원히 서 있고, 그 자리는
  이미 예약돼 있어 다른 손님도 못 앉는다.

⚠ 앉았다 일어난 직후면 이동 모드가 아직 `MOVE_None` 이다. 그대로 이동을 걸면
  명령은 나가는데 캡슐이 안 움직인다 — `EnterState` 에서 `MOVE_Walking` 으로
  되돌린다.

### 결과

| | 전 | 후 |
|---|---|---|
| 한 틱 도약(>40cm) | **46건** | **0건** |
| 착석 시 남은 거리 | 4.5~6.1m | **64~126cm** |
| 픽업대까지 걷기 | 안 함 | 466ms |
| 트레이 수령 | **안 함** | 함 |

⚠⚠ 남은 **60~120cm 는 없앨 수 없다.** 손님이 의자 위에 설 수 없기 때문이고,
  그 거리는 그 좌석 주변 내비가 얼마나 파였는지에 그대로 비례한다 (실측: 어떤
  자리는 56cm, 어떤 자리는 117cm). 더 줄이려면 **레벨 쪽 일**이다 —
  의자/테이블 콜리전을 줄여 내비를 좌석 가까이 붙이거나, 좌석마다 「접근
  지점」을 따로 두는 것. 지금은 `SeatBlendTime`(0.5초)이 미끄러져 메운다.

⚠ 프리필 손님도 99cm 가 남는다 — `PrefillPlacements` 좌표가 가장 가까운 의자에서
  99cm 떨어져 있다. 이건 스포너 데이터 문제고, 지금은 역시 미끄러진다.

---

## 9-15. ⚠ 트레이를 들었는데 음식이 안 보이던 것 (이어서)

`HoldTray`/`PlaceTrayAt`/`DropTray` 의 `SetHiddenInGame` 에 **전파를 안 줬다.**
§9-12 에 적었다. 생성자가 `SetHiddenInGame(true, true)` 로 내려서 자식이 각자
숨김 플래그를 들고 있기 때문에, 부모만 풀면 음식은 계속 안 보인다.

---

## 9-16. 라운드 4 — 나머지

| | |
|---|---|
| `OrderChime` | 0.05 → **0.02** |
| 카운터 트레이 | 사용자가 에디터에서 옮긴 **(228, −1164, 105)** 를 기준으로 삼는다 |
| 영수증 | 세로 **3배** — 스케일 Y 0.05 → **0.15** (4 × 5 → 4 × 15cm) |

⚠⚠ 카운터 트레이 좌표는 이제 **스크립트가 건드리지 않는다.** 사람이 눈으로
  맞춘 값이 정답이고, 스크립트가 다시 계산해 덮으면 그 손질이 매번 날아간다.
  `ll_fixes_round4.py` 는 읽어서 찍기만 한다.

### 영수증이 길어지면서 한 줄로 못 세운다

```
19.6(버거박스) + 7.0(감자) + 15.0(영수증) = 41.6 > 트레이 Y 40
```

그래서 감자와 영수증을 **X 로 나란히** 놓는다. 스크립트가 겹침을 사각형으로
검사한다 — 눈으로 보기 전에 잡으려고.

```
BurgerBox  중심( 0.0, −10.0)  X[−9.4..9.4]  Y[−19.8..−0.2]
Fries      중심(−5.0,   7.0)  X[−12.3..2.3] Y[  3.5.. 10.5]
Receipt    중심( 9.5,   8.0)  X[  7.5..11.5] Y[  0.5.. 15.5]
```

---

## 9-17. 트레이를 테이블에 올리기

### ⚠⚠ 원인 둘 — 빈 바인딩 + 순서

**1) 놓는 자리가 비어 있었다.** `S_Eat ▸ Tray[PlaceOnTable]` 의
`TableLocation`/`TableRotation` 이 **둘 다 0** 이다. 그대로 쓰면 트레이가
**월드 원점**으로 날아간다 — 화면에서는 「트레이가 사라졌다」로 보인다.

`ACustomerCharacter::PlaceTrayAt` 이 받은 자리가 0 이면 **앉은 좌석**
(`SitSeatTransform`)으로 대신한다. 소리 내서 찍는다 — 조용히 메우면 바인딩이
빈 채로 영원히 남는다.

**2) 놓는 시점이 틀렸다.** `Tray[PlaceOnTable]` 은 상태에 **들어가는 순간** 돈다.
그런데 `Sit At Seat` 는 이제 좌석까지 **걸어간 뒤** 앉는다(§9-14). 그대로 놓으면
손님이 아직 걸어오는 중에 트레이가 테이블에 떨어지고 **빈손으로 걸어온다.**

→ 앉기 전이면 `bPendingTrayOnTable` 로 **미뤄 두고**, 좌석 보간이 끝나는 순간
놓는다.

### ⚠⚠ Z 는 **상판 높이**다 — 좌면 높이가 아니다

```
좌석 지점 Z = 0 (바닥)                         §9.2 · SitAt 주석
Table_A     높이 70 (원점 z 35 · 반높이 35)  -> 상판 z = **70**
TrayTableOffset 예전 (45, 0, **30**)         -> 상판보다 40cm 아래 = 테이블 **안**
```

⚠ 트레이 메시는 원점이 바닥이다 (`Props/Tray` 는 z 0~2.2). 상판 높이를 그대로
  주면 상판 **위**에 얹힌다 — 반높이를 더하지 말 것.

⚠ X 45 는 앉은 사람 **앞**으로 45cm. 방향은 좌석의 사용 회전을 따르므로 의자마다
  알아서 맞는다 (실측: 좌석 yaw 179 인 자리는 −X 로 45 갔다).

실측 결과 — 프리필 5명 · 워크인 전부 `z = 70`:

```
BP_Customer_C_0  V(85.53, −716.56, 70.00)   좌석 V(130.52, −717.10) + (45, 0, 70)
BP_Customer_C_5  V(84.55, −487.80, 70.00)   좌석 V(129.52, −489.34) + (45, 0, 70)
```

---

## 9-18. 어떤 상황에 어떤 애님이 도나

⚠⚠ 애님을 정하는 곳이 **넷**이라 하나만 보면 늘 틀린 답이 나온다.

| 층 | 정하는 곳 | 무엇을 정하나 |
|---|---|---|
| 1 | `ST_Customer` 의 `Play Action` | 상태별 **한 번 돌고 끝나는** 몸짓 이름 |
| 2 | `BP_Customer ▸ Actions` | 그 이름이 **어떤 몽타주들**로 풀리나 (가중치) |
| 3 | `BP_Customer ▸ DecorationActions` | **장식 손님**(트리를 안 탄다)의 몸짓 |
| 4 | `UpdateLocomotionMontage` | 서기/걷기/앉기 — **속도와 상태**로 매 틱 |

`ll_anim_map.py` 가 이 넷을 한 번에 훑는다.

### 워크인 — 트리를 탄다

| 상태 | Action | 몽타주 | 시간 |
|---|---|---|---|
| `Kiosk/K_Act` | `Interaction_Reach` | `AM_Reaching_Out` | 3s |
| `Wait/W_Idle` | `Idle_Look` | `AM_Idle`(1.0) · `AM_Texting`(0.3) | 4s |
| `Pickup/P_Act` | `Interact_Reach` | `AM_Reaching_Out` | 2s |
| `Seat/S_Eat/Eating` | `Sit-Eat` | `AM_Eating_a_Burger` · `_a_Drumstick` · `_Frantically` · `_Fries` (각 1.0) | 10s + 0~2회 |

그 사이 이동은 전부 **층 4** 가 낸다.

### 프리필 — 둘로 갈린다

**(a) 장식 손님** (`bDecoration`) — 트리를 **안 탄다.** `StartDecorationLoop` 가
역할별로 몸짓 하나를 걸고 그것만 반복한다.

| 역할 | DecorationActions | 몽타주 |
|---|---|---|
| `Eating` | `Sit_Idle` | `AM_Eating_a_Burger`·`AM_Eating_Fries`·`AM_Eating_a_Drumstick`(1.0) · `AM_Eating_Frantically`(0.6) · `AM_Sitting_Talking`(0.8) · `AM_SitIdle_Male`(0.5) |
| `Ordering` | `Idle_Look` | `AM_Idle`(1.0) · `AM_Texting`(0.3) |
| `Waiting` | `Idle_Look` | 〃 |

⚠ `bDecorationSingleAction = true` 라 **한 사람은 한 가지 몸짓만** 한다. 다양성은
  손님들 **사이에서** 나온다 — 좌석 번호로 결정적으로 뽑으므로 같은 의자는 늘 같은
  몸짓이고 옆자리와는 겹치지 않는다 (`DecorationSeed = 0`).

**(b) 장식이 아닌 프리필** — 트리의 `P_` 갈래를 탄다.

| 상태 | Action | 몽타주 | 시간 |
|---|---|---|---|
| `P_Ordering/PO_Kiosk/PO_Act` | `Interact_Reach` | `AM_Reaching_Out` | 3s |
| `P_Waiting/PW_Wait/PW_Idle` | `Idle_Look` | `AM_Idle` · `AM_Texting` | 4s |
| `P_Eating/PE_Seat/PE_Sit/PE_Eat` | `Sit_Eat` | 먹는 것 4종 | 10s + 0~2회 |

⚠ 지금 레벨의 `PrefillPlacements` **5개는 전부 역할 `Eating`** 이다. 그래서
  `Ordering`/`Waiting` 쪽 프리필 그림은 **현재 안 나온다.**

### 층 4 — 이동 애님 (`LocoSlot`)

```
앉음이면                 -> Loco_Sit        AM_Loco_Sit
아니면 속도 > 임계        -> Loco_Walk       AM_Loco_Walk
       속도 <= 임계       -> Loco_Idle       AM_Loco_Idle
트레이를 들었으면 위 둘이  -> Loco_CarryWalk / Loco_CarryIdle  (AM_Loco_Carry*)

임계 = 5 ± 1 (히스테리시스)   ← 걷는 중이면 4까지 버티고, 서 있으면 6을 넘어야 걷는다
```

⚠ 히스테리시스가 없으면 감속 구간에서 Idle↔Walk 가 초당 수십 번 뒤집히고, 그때마다
  블렌드인이 0 에서 다시 시작해 **애님이 얼어붙는다.**

⚠ `Play Action`(층 1·2)은 `DefaultSlot`(전신), 로코모션(층 4)은 `LocoSlot` 이다.
  그래서 몸짓이 도는 동안은 몸짓이 이긴다.

### ⚠ 이름이 둘씩 있다 — 정리하면 좋을 것

```
Sit-Eat  /  Sit_Eat            둘 다 같은 4종을 가리킨다
Interact_Reach / Interaction_Reach   둘 다 AM_Reaching_Out
```

워크인은 `Sit-Eat`·`Interaction_Reach`, 프리필은 `Sit_Eat`·`Interact_Reach` 를
쓴다. 지금은 양쪽 다 맵에 있어 동작하지만, **한쪽을 지우면 그쪽이 조용히 죽는다.**

---

## 9-19. 손님을 **언리얼 기본 마네퀸**으로

### 왜 — 손님만 혼자 Mixamo 였다

```
BP_Customer                                           Hip_Hop_Dancing (Mixamo)
BP_ThirdPersonCharacter · BP_Passerby · BP_SubNpc …   SKM_Manny_Simple
BP_MainNpc_Sample                                     SKM_Quinn_Simple
```

`SKM_Manny_Simple` 과 `SKM_Quinn_Simple` 은 **같은 스켈레톤**
(`/Game/Characters/Mannequins/Meshes/SK_Mannequin`)이라, 여기로 오면 나중에 퀸으로
바꾸는 것은 메시만 갈아 끼우면 된다.

⚠ 풀로 들어온 `/Game/Characters/NPCSequence/NPC_Idle/Mannequins/` 에도
  `SKM_Manny`·`SK_Mannequin` 이 있지만 **별개의 스켈레톤**이다. 그쪽으로 가면
  프로젝트의 나머지 캐릭터와 애님을 공유 못 한다. 안 썼다.

### 바뀐 것

| | 전 | 후 |
|---|---|---|
| `Mesh ▸ SkeletalMesh` | `Hip_Hop_Dancing` | `SKM_Manny_Simple` |
| `Mesh ▸ AnimClass` | `Mixamo/ABP_Customer` | `Manny/ABP_Customer` |
| 애님 시퀀스 24개 | `Anims/Mixamo/Anims` | `Anims/Manny` |
| 몽타주 11종 | `Anims/Montages` | `Anims/Manny/Montages` |
| `TraySocket` | `spine2` | `spine_05` |

메시 상대 트랜스폼(`z −90` · `yaw −90`)은 **안 건드렸다** — 마네퀸도 같은 규약이다.

### 리타게터

```
RTG_MixamoToManny   소스 IK_Mixamo        타깃 IK_MannyTarget(새로 만듦)
체인 6개            Spine · Head · LeftArm · RightArm · LeftLeg · RightLeg
Pelvis Motion       blend_to_source 0 · scale_horizontal 1
Root Motion op      **끔**
```

⚠⚠ 에픽의 `IK_Manne` 은 **안 건드렸다.** 다른 리타게터들이 이미 걸려 있어서
  체인을 고치면 그쪽이 조용히 틀어진다. 타깃 리그를 새로 만들었다.

⚠ `blend_to_source = 0` 이라 골반이 마네퀸 비율로 온다 — 실측 Mixamo 103.4 →
  마네퀸 95.1. 1 로 두면 Mixamo 높이가 그대로 들어와 캡슐 위로 뜬다.

⚠ `Root Motion` op 은 껐다. 마네퀸에는 `root` 본이 있어서 켜 두면 소스에 없는
  루트 이동을 만들어 낸다 — `RootMotionFromMontagesOnly` 와 부딪힌다 (§9-2).

### ⚠⚠⚠ 일괄 리타겟은 **참조를 안 옮긴다**

`IKRetargetBatchOperation` 이 뱉은 것은 **껍데기**였다 —

```
Manny/AM_Loco_Walk   스켈레톤 SK_Mannequin   안의 시퀀스는 Mixamo/Walk_Fwd_Manny
Manny/ABP_Customer   스켈레톤 SK_Mannequin   노드가 무는 시퀀스는 Mixamo/Idle …
```

**22개 몽타주 전부**가 그랬다.

⚠⚠ 에러가 없다. 에셋도 생기고 스켈레톤도 맞아서 어디서도 안 걸린다. 화면에서
  손님이 레퍼런스 포즈로 서 있어야만 안다.

- **몽타주** → `ll_montages.py` 로 마네퀸 시퀀스 위에서 **다시 만들었다.**
  (`ANIM_DIR`·`MTG_DIR` 을 Manny 폴더로 옮겼다.)
- **ABP** → `AnimGraphService` 로 Sequence Player 노드 3개를 갈아 끼웠다.

⚠ 노드 지정은 **GUID** 로 한다. 표시 이름으로는 `set_anim_graph_node_property`
  가 조용히 `False` 만 돌려준다.

⚠ `ll_montages.py` 에 `Loco_CarryWalk`·`Loco_CarryIdle` 이 **빠져 있었다** —
  §9-6 때 다른 경로로 만들었던 탓이다. 이번에 넣었으니 이제 이 스크립트 하나가
  몽타주의 단일 출처다.

### ⚠ 배치 결과는 **저장까지 확인**해야 한다

`run_batch_retarget` 이 돌려주는 목록에 **ABP 가 안 들어 있었다.** 메모리에는
만들어졌는데 디스크에는 없었다. 반환값에 기대지 말고 **출력 폴더의 더티 패키지를
전부** 저장할 것.

### 리타겟 품질 — 원본 대비로 잰다

「각도가 작다」만으로는 못 고른다. 원본부터 잔잔한 애님(Idle 류)이 있다. 그래서
**원본(Mixamo)을 같은 자로 재서 보존율**을 본다 (`ll_manny_verify.py`).

```
Walk_Fwd_Manny 100%  ·  Reaching_Out 98%  ·  Sitting_Talking 98%
Idle 100%  ·  SitIdle_Male 98%  ·  Carrying 100%  ·  Eating_a_Burger 98%
```

⚠⚠ 측정 함수를 두 번 틀렸다. 남겨 둔다 —

1. `get_bone_pose_for_time(..., use_source=False)` 는 **부모 기준(로컬)** 이다.
   본 길이는 안 변하므로 루트가 아닌 본의 **위치는 상수**다. 위치 흔들림으로 재면
   전부 0 이 나와 「레퍼런스 포즈」로 오독한다.
2. 오일러 한 축(pitch)만 봐도 안 된다. 축이 다른 회전이 통째로 안 보인다.
   **쿼터니언 각거리**로 축과 무관하게 잰다.

### ⚠ 눈으로 맞춰야 하는 것

- `TrayHandOffset` — 본의 **방향 규약**이 달라졌다. 트레이가 손에 어떻게 물리는지
  다시 봐야 한다. 스크립트가 정할 수 있는 값이 아니다.
- `TraySocket = spine_05` 는 Mixamo `Spine2`(척추 3단 중 맨 위)의 대응으로 잡은
  값이다. ⚠ 본을 못 찾으면 `GetSocketTransform` 이 **컴포넌트 원점(발밑)** 으로
  폴백한다 — 죽지 않고 트레이만 발밑에 붙어서 조용히 지나간다.
- 마네퀸이 Mixamo 보다 **작다**(골반 95 vs 103). 의자·테이블과의 높이가 안 맞으면
  `SitZAdjust` 로 맞출 것.

### 남은 것

- 옛 `Anims/Mixamo`·`Anims/Montages` 는 **안 지웠다.** `BP_Customer` 는 더 이상
  참조하지 않지만(검산 0건), 되돌릴 여지를 남겨 뒀다. 확인 뒤 지우면 된다.
- 퀸으로 바꾸려면 `BP_Customer ▸ Mesh ▸ SkeletalMesh` 만 `SKM_Quinn_Simple` 로
  바꾸면 된다 — 스켈레톤이 같아 애님은 그대로 돈다.

### ⚠⚠ `git pull` 이 지운 에셋을 되살린다

§9-13 에서 지운 `Telling_A_Secret` 이 다시 생겨 있었다. 삭제가
`git status` 에서 ` D`(스테이지 안 된 워킹트리 삭제)였는데, 풀이 fast-forward
체크아웃을 돌면서 **워킹트리에 없는 파일을 복원**했다. 그 커밋에 해당 파일
변경이 없어도 그렇게 된다.

⚠ 에셋을 지웠으면 **그 자리에서 커밋하거나**, 풀 뒤에 다시 확인할 것.

---

## 9-20. ⚠⚠⚠ 버거가 **발밑**에 붙어 있던 것 — 버그 둘이 겹쳐 있었다

증상은 하나였다: 워크인 손님이 문에 들어오는 **순간부터** 음식이 발밑에 보인다.
원인은 **생성자 한 함수 안에 있는 서로 다른 버그 둘**이다.

### A — 생성자가 쓰는 소켓 이름은 **C++ 기본값**이다

```cpp
// 생성자
Tray->SetupAttachment(GetMesh(), TraySocket);   // <- 이때 TraySocket 은 "mixamorig:RightHand"
```

`TraySocket` 은 `UPROPERTY` 라 **블루프린트가 덮어쓴 값은 생성자가 끝난 뒤에**
들어온다. 그래서 손님을 마네퀸으로 바꾸고 BP 에서 `spine_05` 로 고쳐도, 실제
부착은 옛 기본값 `mixamorig:RightHand` 로 일어난다.

마네퀸에 그런 이름이 없으니 `GetSocketTransform` 이 **컴포넌트 원점으로 폴백**
한다. 그런데 메시 컴포넌트는 캡슐 기준 `z −90` 이라 그 자리가 곧 **발밑**이다.

⚠⚠ 죽지 않는다. 경고도 없다. 「발밑에 붙어 있다」로만 보인다.

고침: `BeginPlay` 에서 **다시 붙인다.** 그때는 BP 값이 들어와 있다.

```cpp
Tray->AttachToComponent(GetMesh(), SnapToTargetNotIncludingScale, TraySocket);
Tray->SetRelativeTransform(TrayHandOffset);
Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);
```

⚠ 생성자에서 `UPROPERTY` 값을 **읽어서** 무언가를 배선하면 언제나 이 함정이다.
  기본값으로만 도는 코드라는 것을 잊지 말 것.

### B — 숨기기가 **자식을 만들기 전**에 있었다

```cpp
Tray = CreateDefaultSubobject(...);
Tray->SetHiddenInGame(true, /*bPropagateToChildren=*/true);   // <- 여기
…
BurgerBox = MakeTrayItem(...);   // <- 자식은 **이 뒤**에 생긴다
```

`bPropagateToChildren` 은 **그 순간 붙어 있는 자식**에게만 내려간다. 그래서
트레이는 숨고 **버거·감자·영수증만 그대로 보였다.**

⚠ §9-12 에서 `HoldTray`/`DropTray` 의 전파를 고쳤는데, **생성자 쪽은 순서 때문에
  여전히 안 먹고 있었다.** 같은 함정을 두 번 다른 모습으로 만났다.

고침: 숨기기를 자식 부착 **뒤로** 옮겼다.

---

## 9-21. 트레이를 **양손 사이**로 — 실측으로 오프셋을 푼다

`Carrying` 애님에서 `hand_l`·`hand_r` 의 **컴포넌트 공간** 위치를 재고, 그
중점에 트레이가 오도록 `TrayHandOffset` 을 역산한다 (`ll_tray_hands.py`).

```
t=0.00  L( 29.5  18.8 113.7)   R(-32.7  19.5 116.9)   중점(-1.6 19.1 115.3)  간격 62.3
…
중점 흔들림  X 6.5 · Y 3.9 · Z 0.7      간격 62.1~62.9
```

손 중점이 애님 내내 거의 안 움직인다 → **고정 오프셋으로 충분하다.**

```
붙는 본         spine_05
TrayHandOffset  loc(−30.84, 19.12, 2.13) · rot(−85.3, 2.2, 107.2)
되짚기 오차     0.4 ~ 1.0cm
```

⚠ 손보다 **몸통(`spine_05`)** 에 붙인다. 손에 붙이면 그 손만 따라가 트레이가
  흔들린다. 몸통에 붙여도 위 실측대로 손 사이에 머문다.

### ⚠⚠ 이 계산에서 두 번 틀렸다 — 둘 다 조용히 틀린다

**1) 본 합성 순서.** `get_bone_pose_for_time` 은 **부모 기준(로컬)** 이라 루트부터
   내려가며 합성해야 한다. 리프부터 합성했더니 손이 골반보다 **108cm 아래**,
   양손 간격 **199cm** 가 나왔다. 숫자가 터무니없어서 겨우 알아챘다.

**2) 좌우 축 부호.** UE 는 왼손 좌표계(X 앞 · Y 오른쪽 · Z 위)다. 「오른손 →
   왼손」으로 가로축을 잡으면 축 둘이 뒤바뀌어 행렬식이 **음수**가 되고 회전이
   거울상이 된다 — 트레이 위 음식 배치가 좌우로 뒤집힌다. **왼손 → 오른손**이다.

```
right = normalize(Pr − Pl)        # 캐릭터 오른쪽
fwd   = right × up                # X = Y × Z
```

### ⚠ 트레이가 손 간격보다 **좁다**

```
양손 간격  62cm
트레이 긴 축(Y)  40cm
```

중점에는 맞췄지만 손이 트레이 테두리보다 **양쪽으로 11cm씩** 바깥이다. 눈에
거슬리면 둘 중 하나다 — 트레이 Y 스케일을 **1.55배**로 키우거나, `Carrying` 의
손 간격을 좁힌 애님을 쓰는 것. 지금은 **안 건드렸다** (요청 범위 밖).

---

## 9-22. 라운드 5 — 영수증 비율 · 키오스크 몸짓

| | 전 | 후 |
|---|---|---|
| 영수증 | 4 × 15 | **12 × 7.5** (가로 3배 · 세로 절반) |
| `K_Act` | `Reaching_Out` 1배속 1회 | **2배속 3회** |

### 영수증이 넓어지면 배치를 되돌려야 한다

§9-16 에서 영수증이 길어져 감자와 **X 로 나란히** 놨었다. 이번엔 반대로 넓어져서
그 자리에 안 들어간다 → 다시 **Y 한 줄**로.

```
19.6(버거박스) + 7.0(감자) + 7.5(영수증) = 34.1 < 트레이 Y 40
BurgerBox 중심( 0, −9)   Fries ( 0, 5)   Receipt ( 0, 13)
```

### `Plays` 를 새로 만들었다

`RandomExtraRepeats` 는 **무작위**(0~N)라 「정확히 3번」을 못 만든다.

```cpp
int32 Plays = 1;      // 총 재생 횟수. RandomExtraRepeats 는 여기에 더해진다
float PlayRate = 1.f; // 2 면 두 배 빠르다
```

⚠⚠ `Montage_Play` 가 돌려주는 것은 **재생 속도를 안 나눈 원본 길이**다. 그대로
  쓰면 2배속일 때 태스크가 **두 배**를 기다린다 — 「애님은 빨리 끝났는데 손님이
  멍하니 서 있다」. `PlayAction` 이 `Len / Rate` 를 돌려주도록 고쳤다.

실측: `AM_Reaching_Out` 4.00s → 2배속 2.00s → 3번 **6.00s** (+ `RandomExtra` 여유).
PIE 에서 K_Act 체류 7.1~7.9초로 나온다.

---

## 9-23. 2인용 테이블 트레이

### 마주보고 앉으면 트레이가 겹친다

프리필 손님은 각자 자기 앞 `TrayTableOffset`(45cm)에 놓으므로 마주보는 짝은
테이블 한가운데서 만난다. 실측 —

```
C_3 (344.80, −464.60)  /  C_4 (343.32, −471.08)    6.5cm
C_0 ( 85.53, −716.56)  /  C_1 ( 69.00, −704.88)   20.2cm
C_2 ( 81.27, −763.42)                              가장 가까운 남의 것이 47cm — 혼자
```

### 푸는 법 — 액터가 이기고, 손님은 비켜 준다

`BP_TableTray`(= `ACounterTray` 자식)를 짝의 **중점**에 하나 놓는다. 손님 쪽은
`ACustomerCharacter` 가 놓을 자리 **`SharedTrayRadius`(40cm) 안에 트레이 액터가
있으면 자기 것을 안 놓는다.**

⚠ 「누가 먼저냐」를 안 따진다. 레벨에 놓인 액터가 늘 이긴다 — 사람이 눈으로 맞춘
  배치가 자동 배치보다 정확하기 때문이다.

⚠ 반경이 넓으면 옆 테이블까지 물어 혼자 앉은 손님의 트레이가 사라진다. 실측으로
  남의 트레이가 47cm 였으므로 **40** 이 마주보는 짝(6~20cm)만 잡는 값이다.

### `ACounterTray` 에 붙인 손잡이 둘

| | |
|---|---|
| `bTwoPersonSet` | 켜면 `BurgerBox2` 가 보인다 |
| `bBindToStore` | **테이블 트레이는 끈다** — 주문 상태와 무관하다 |

⚠⚠ `bBindToStore` 를 안 끄면 주문이 없을 때 테이블 트레이가 통째로 사라진다.
  픽업대 트레이의 「준비된 주문이 있어야 보인다」 규칙이 그대로 적용되기 때문이다.

### 2인용 배치 — **마주보는 축을 따라** 나눈다

버거박스 둘을 나란히 두면 **둘 다 한 사람 앞**에 몰린다. 트레이 긴 축(Y 40)을
두 사람을 잇는 선에 맞추고 양 끝에 하나씩 둔다.

```
burger_box  ( 0, −13)  ×5.0   12.6 × 13.1
burger_box2 ( 0,  13)  ×5.0   12.6 × 13.1
fries       (−6,   0)  ×0.5   12.2 ×  5.8
receipt     ( 7,   0)         12.0 ×  7.5
```

⚠ 1인용 버거박스(×7.5, 18.8 넓이) 둘은 트레이 X 30 에 안 들어간다. ×5.0 으로
  줄였다 — 「크기는 같지만 담는 것이 다르다」의 대가다.

⚠ 액터 yaw 는 **+X 기준**이라 긴 축(+Y)을 그 선에 맞추려면 **−90** 을 더한다.

⚠ 파이썬은 bool `UPROPERTY` 의 앞 `b` 를 뗀다 — `bTwoPersonSet` → `two_person_set`.

---

## 9-24. ⚠⚠⚠ 워크인이 **영원히 안 나가던** 것 — 퇴장 배선이 셋 다 끊겨 있었다

증상: 먹기가 끝나면 `Ordering` 부터 다시 시작한다. 손님이 하나도 안 사라져
정원을 계속 문다.

### A — `Root/Leave` 가 진입한 프레임에 끝난다

```
Root/Leave   tasks_completion = All   태스크 [Tray(Drop), Get Store Point]
             둘 다 즉시 Succeeded  ->  All 충족  ->  상태 완료
             전이 [0] OnStateCompleted -> Root    ->  Root 는 첫 자식 Enter 를 고른다
```

**§9-14 원인 A 와 같은 종류다.** 스스로 끝날 일이 없는 태스크가 `Succeeded` 를
돌려주면 상태를 통째로 끝낸다.

→ `FBurgerHouseGetPointTask` 가 **`Running`** 을 돌려주도록 (`Root/Wait` 의
  `Enter Wait Area` 와 같은 규약).

### B — `L_Gone` 에 디스폰 태스크가 **안 붙어 있었다**

`FBurgerHouseDespawnTask`(`Despawn Customer`)는 C++ 에 멀쩡히 있는데 트리의
`L_Gone` 상태에는 태스크가 **하나도 없었다.**

→ `add_task` 로 붙이고 `Customer` 를 컨텍스트 Actor 에 바인딩.

⚠⚠ 태스크의 인스턴스 데이터에 `UPROPERTY` 를 **추가하면 트리를 다시 컴파일해야**
  새 기본값이 들어간다. 안 하면 새 프로퍼티가 **0** 으로 읽혀 그 기능이 통째로
  안 돈다 — 실측으로 `WalkOutTimeout` 이 0 이라 출구 걷기가 아예 안 돌았고,
  **로그 한 줄도 안 나와서** 코드가 안 들어간 줄 알았다.

### C — `Despawn` 이 `Succeeded` 라 그 사이 루프가 한 바퀴 돈다

수명이 다하기 전 0.2초 동안 손님이 `Enter → Kiosk` 로 돌아가 **키오스크 슬롯을
다시 잡았다가** 사라졌다. → `Running` 으로.

### 덤 — `L_Move`(출구까지 걷기)도 죽어 있다

`S_Move` 와 같은 증상으로 진입한 프레임에 끝난다 (§9-14 원인 B). 그래서 손님이
**자리에서 그대로 증발**했다.

`Sit At Seat` 과 같은 수를 쓴다 — 디스폰 태스크가 **직접 걸어간다.** 출구 지점은
서브시스템에 물으면 되므로 바인딩이 필요 없다.

⚠⚠ 이동 요청은 **될 때까지 다시 건다.** `S_Eat` 를 나오며 `StandUp()` 이 시작한
  좌석 보간이 매 틱 `TeleportPhysics` 로 캐릭터를 옮겨 **첫 요청이 그 자리에서
  실패한다**(실측 `Failed`(0)). 보간은 0.5초면 끝나므로 다음 틱에 붙는다 —
  실측 **0.07초** 만에 붙었다.

⚠ 시간 제한(`WalkOutTimeout` 12초)이 있어야 한다. 경로가 막히면 손님이 영원히 안
  사라져 정원을 문다.

### 결과

```
출구로 걸어간다 (0.07초 만에 붙음)
출구에 닿았다 (4.3초)
루프 재시작 1회(최초 스폰) · 파괴 1회
```

---

## 9-25. 팔이 너무 벌어진 것 — **리타겟 포즈**로 오므린다

애님을 하나씩 고치지 않는다. 리타게터의 **타깃 리타겟 포즈**를 돌리면 모든 애님이
같이 바뀌고, 손잡이가 하나라 되돌리기도 쉽다.

```
출력 본 회전 ≈ 타깃_리타겟포즈 ∘ (소스_현재 ∘ 소스_리타겟포즈⁻¹)
```

### ⚠⚠ 좌우 부호는 축마다 다르다 — 실험으로 정했다

`upperarm_l` 에 +X, `upperarm_r` 에 −X 로 **거울처럼** 주는 것이 자연스러워 보이지만
`pitch` 는 그렇지 않았다. `Idle` 의 손 간격 실측 —

```
기준                    handL.x  39.4 · handR.x −40.6 · 간격 80.0
pitch 거울로 ±20        한쪽만 들어오고 다른 쪽은 나갔다 (간격 78.1)
pitch **양쪽 −20**      handL.x  23.3 · handR.x −25.0 · 간격 48.5
```

roll·yaw 는 팔을 앞뒤로 돌려서 원하는 그림이 아니었다.

### 1도당 손 간격 약 1.6cm — 거의 선형

```
pitch −10  64.7   pitch −20  48.5   pitch −30  32.0   pitch −40  16.0
```

**−12** 를 기본으로 잡았다 (`ll_arms_in.py` 의 `ARM_PITCH`). 숫자 하나만 바꾸고
다시 돌리면 된다.

```
Idle            80.0 -> 61.5
Carrying        62.3 -> 44.3     <- 트레이 긴 축 40 과 거의 맞는다
Walk_Fwd_Manny       -> 57.6
```

⚠ `Carrying` 이 바뀌면 `TrayHandOffset` 도 다시 재야 한다 — `ll_arms_in.py` 뒤에
  **반드시 `ll_tray_hands.py`** 를 돌릴 것.

⚠ 다시 구울 때 **시퀀스만** 한다. 몽타주·ABP 를 같이 구우면 참조가 옛 Mixamo 로
  되돌아간다 (§9-19). 시퀀스는 제자리에 덮어써지므로 몽타주·ABP 가 그대로 새
  내용을 본다.

---

## 9-26. ⚠⚠ 프리필의 **스폰 지점은 위치가 아니라 「어느 의자냐」다**

3인 일행 중 벽에 붙은 손님을 −Y 로 30cm 옮기려다 알게 된 것.

```cpp
// ACustomerCharacter::ApplyStartRole — Eating 갈래
ASlotPoint* Seat = Store->ClaimNearestSlot(this, Seat);   // 스폰 지점에서 **가장 가까운** 의자
SitAt(Seat->GetUseTransform());                           // 그 의자 자리로 앉힌다
```

`PrefillPlacements` 의 좌표는 **어느 의자를 고를지 정하는 선택자**일 뿐이다. 실제
위치는 `ASlotPoint` 가 정한다.

⚠⚠ 그래서 스폰 지점만 옮기면 **아무것도 안 바뀐다.** 태어나자마자 원래 자리로
  0.5초 미끄러져 돌아갈 뿐이다 (`SeatBlendTime`). 눈으로는 「반영이 안 됐다」로
  보인다.

→ 스폰 지점과 **좌석 슬롯을 같이** 옮겨야 한다.

### 어느 것이 「벽에 가까운」가 — 숫자로 고른다

내비 경계(X −10~610 · Y −1140~0)까지의 거리로 고른다.

```
[0] party=6 (130.5, −717.1)  벽까지 141
[1] party=6 ( 24.0, −704.9)  벽까지  34   <- 이것
[2] party=6 (126.3, −764.1)  벽까지 136
```

⚠ 1등과 2등이 40cm 안쪽으로 붙어 있으면 「벽에 붙은 하나」라는 전제가 안 선다.
  스크립트가 그때 경고한다.

### 옮겨도 되는 자리인가 — 소파 위다

```
Sofa_Unit           48 × 103.8
소파 #4             (24, −722.9)   Y 범위 −774.8 ~ −671.0
SLOT_Seat_33 (전)   (24, −722.9)
SLOT_Seat_33 (후)   (24, −752.9)   여유 위 21.9 · 아래 81.9
```

⚠⚠ **긴 소파**라 Y 로 미는 것이 자연스럽다. 등받이 의자였다면 의자 밖으로 나간다
  — 같은 수를 쓰기 전에 그 슬롯이 어디에 놓였는지 먼저 볼 것.

⚠ 옮긴 뒤 **최근접 의자가 그대로인지** 다시 확인한다. 바뀌면 손님이 엉뚱한 자리에
  앉는데, 그 증상은 「배치가 이상하다」로만 보인다.

### ⚠⚠ 옮기면 **공용 트레이 반경이 남을 문다**

좌석이 움직이면 공용 트레이 중점도 움직인다. `TableTray_0` 이 (77,−711) →
(77,−726) 으로 오면서 **혼자 앉은 손님**(38cm)까지 `SharedTrayRadius`(40) 안에
들어와 그 사람 트레이가 통째로 사라졌다.

```
짝을 이룬 둘      공용 트레이까지  3 · 12cm
혼자 앉은 사람                    38cm     <- 물면 안 된다
```

→ `SharedTrayRadius` 40 → **25** (양쪽에 13cm 여유).

⚠ 좌석을 옮길 때마다 이 반경을 다시 볼 것. 스크립트 순서는
  `ll_prefill_nudge.py` → `ll_prefill_nudge_seat.py` → **`ll_table_tray.py` 다시**.

⚠ 두 nudge 스크립트는 **상대 이동**이라 한 번만 돌린다. 시작 위치를 확인하고
  아니면 멈춘다.

---

## 9-27. 감자튀김 세트를 트레이에 — `FRIES_ASSET.md`

### ⚠⚠ 이 넷은 원래 **셸에 합쳐져** 들어온다

`ll_import.py` 는 `LL_Shell.fbx` 를 `combine_static_meshes_behavior =
ALL_MESHES_INTO_ONE` 으로 굽는다. 즉 `Food_Coke`·`Food_Fries`·`Food_FriesLoose`·
`Food_Ketchup` 은 **레벨 지오메트리의 일부**이지 따로 쓸 수 있는 에셋이 아니다.

⚠ `LL_placement.json` 의 `singles` 항목은 **메타데이터일 뿐** 아무 에셋도 안 만든다.
  이름이 있길래 에셋이 있는 줄 알기 쉽다.

그래서 `ll_fries_set_import.py` 가 같은 FBX 를 **합치지 않고** 임시 폴더에 한 번 더
굽고(230개), 넷만 `Props/Food/` 로 옮기고 나머지 226개를 지운다.

⚠ 셸 쪽은 **안 건드린다.** 테이블 위(셸)에도 있고 트레이 위(개별 에셋)에도 있게
  된다 — 같은 모양이 두 군데 있는 것이 정상이다.

### ⚠⚠ 피벗이 **월드 원점**이다

블렌더에서 배치를 **버텍스에 구워** 내보냈기 때문에(`FRIES_ASSET.md` §4) 트랜스폼은
항등이고 정점만 테이블 좌표에 있다.

```
Food_Coke        피벗에서 중심까지 (307.1, −270.7, 82.0)
Food_Fries                        (294.5, −272.0, 79.5)
Food_FriesLoose                   (299.0, −268.6, 71.1)
Food_Ketchup                      (291.1, −264.7, 71.1)
```

그래서 컴포넌트 상대 위치가 **−466, 394, −102** 같은 값이 된다. 수학은 맞지만
**사람이 손으로 만지기 나쁘다.** 제대로 하려면 블렌더에서 피벗을 각자 중심으로
옮겨 다시 내보내야 한다.

### 회전이 들어가면 역산식이 달라진다

§9-8 의 식은 회전이 없을 때만 맞다.

```
회전 없음   중심 = loc + origin*scale
회전 있음   중심 = loc + R·(origin*scale)
            바닥 = 중심.z − (회전된 z 반높이)
```

⚠ 회전된 축정렬 반extent 는 원래 extent 를 **축별로 돌려 절대값을 합**해야 한다.
  단순히 축을 맞바꾸면 45도 같은 각도에서 틀린다.

⚠ 「눕히기」는 **roll +90** 이다. 오른손 법칙으로 +X 축 둘레라 local +Z → world −Y —
  봉투 입구가 트레이 **안쪽**을 향한다. `−90` 이면 바깥을 향한다.

### 배치 (트레이 30 × 40) — **1인용**

| | 크기(cm) | 중심 | 비고 |
|---|---|---|---|
| `Food_Fries` 눕힘 | 14.6 × 19.0 × 8.2 | (−6, −10) | roll **−90** (입구가 바깥) |
| `SM_BurgerBox` ×5.0 | 12.6 × 13.1 × 6.6 | (8.5, −12) | ×7.5 로는 안 들어간다 |
| 영수증 | 12.0 × 7.5 | (8.8, −7.5) | 버거박스 앞으로 **1.72cm 삐져나옴** |
| `Food_Coke` ×0.75 | 8.0 × 6.7 × 18.0 | (−7, 6) | |
| `Food_Ketchup` ×0.75 | 3.4 × 3.4 × 1.7 | (8, 2) | |
| `Food_FriesLoose` | 11.1 × 6.2 × 2.0 | (8, 12) | 그대로 |

⚠ 영수증만 상판 +0.05, **버거박스는 +0.35** 로 띄운다 — 영수증 위에 얹히는
  그림이고 z-파이팅도 막는다.

⚠⚠ **정확히 겹치면 안 보인다.** 영수증(12 × 7.5)이 버거박스(12.6 × 13.1)보다
  작아 중심을 맞추면 통째로 가려진다. 앞으로 어긋내 **23%** 만 드러낸다.

⚠ 콜라·봉투감자의 자리를 맞바꿀 때 **그대로 옮기면 안 된다.** 봉투가 훨씬 넓어
  Y 로 21.5 까지 나가 트레이(±20) 밖이다 — 중심을 −12 에서 **−10** 으로 당겼다.

### ⚠ 검산은 **다른 방법**으로 한다

배치에 쓴 식을 되풀이하면 검산이 아니다. bbox **꼭짓점 8개를 실제로 변환**해
AABB 를 다시 구해 중심·바닥·트레이 안쪽을 본다.

### 남은 것

- **2인용 `BP_TableTray` 는 옛 구성 그대로다** (버거박스 2 · 옛 감자 · 영수증).
  같은 세트로 맞추려면 따로 해야 한다 — 2인용은 이미 64% 라 콜라(×1.5)가 안 들어간다.
- `SM_Fries`(옛 감자)는 이제 아무도 안 쓴다. 지우지 않았다.

---

## 9-28. 대기 몸짓에 「서서 폰 보기」를 반반으로

`Idle_Look` 에 `Texting_While_Standing` 을 **나머지 전부와 같은 가중치**로 넣는다.

```
AM_Idle                    w=1.0  ->  38%
AM_Texting                 w=0.3  ->  12%      <- 둘이 합쳐 50%
AM_Texting_While_Standing  w=1.3  ->  50%
```

⚠ 가중치는 **합이 1일 필요가 없다.** 「반반」은 기존 합(1.3)을 그대로 주면 된다.

⚠⚠ 한때 이것을 **뺐던** 이유는 23.6초라 손님이 문 앞에 오래 서 있어서였다
  (§9-x). 그때는 `Idle_Look` 이 `Enter` 에서도 돌았다. 지금은 `Wait` 에서만 돌고,
  그 상태는 `Order.Ready` **이벤트가 빼내므로** 애님 길이가 체류 시간을 안 정한다.
  실측으로 대기 8.2초 만에 `Pickup` 으로 넘어갔다.

⚠ `Idle_Look` 을 쓰는 곳은 `W_Idle`·`PW_Idle` 둘뿐이다 (둘 다 주문 뒤 대기).
  `DecorationActions` 의 `Ordering`/`Waiting` 도 같은 이름을 쓰지만 지금
  `PrefillPlacements` 는 전부 `Eating` 이라 안 돈다 (§9-18).

---

## 9-29. ⚠⚠⚠ 트레이는 **접시**다 — 음식이 전부 2.05cm 떠 있었다

「감자봉투가 살짝 떠 있다」로 보였지만 **여섯 개 전부** 떠 있었다.

### 실측 — 위를 보는 면이 **둘**이다

`Props/Tray` 는 높이 2.2 인데, 법선이 +Z 인 정점을 뽑아 보면 —

```
z = 0.20   8개   X ±12.3 · Y ±17.3    <- **안쪽 바닥** (물건이 얹히는 곳)
z = 2.20   8개   X ±14.1 · Y ±19.1    <- 테두리 윗면
```

§9-8 이후 줄곧 **AABB 상단(2.2)** 을 「상판」으로 잡아 왔다. 접시라는 생각을
안 했다. 그래서 전부 **2.05cm** 떠 있었고, 큰 것일수록 티가 났다.

⚠⚠ 쓸 수 있는 넓이도 **30 × 40 이 아니라 24.7 × 34.7** 이다. AABB 크기를 그대로
  믿으면 안 된다 — 예전 배치는 몇 개가 테두리 밖으로 걸쳐 있었다.

### 어떻게 알아냈나 — 정점을 직접 읽는다

```python
verts, tris, normals, uvs, tangents = \
    unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)
# 법선 z > 0.9 인 정점의 z 분포 = **얹히는 면**들
```

⚠ 바운즈만 보면 평생 못 찾는다. `positive_bounds_extension` 도 0 이라 패딩도
  아니었다. **정점을 보는 것 말고는 방법이 없었다.**

⚠ 같은 방법으로 봉투감자의 밑면이 **둥글다**는 것도 확인했다 — 최저점에서 0.48cm
  안에 정점이 0.5% 뿐이다. 접시 문제를 고치고 나면 이건 눈에 안 띄는 수준이다.

### 고친 값

```
TRAY_FLOOR = 0.2        (전: TRAY_TOP = 2.2)
INNER_X = 12.35         (전: 15.0)
INNER_Y = 17.35         (전: 20.0)
```

`ll_fries_set_layout.py` · `ll_table_tray.py` 둘 다.

---

## 9-30. ⚠ 사람이 에디터에서 옮긴 자리를 스크립트가 **지우고 있었다**

`ll_table_tray.py` 는 돌 때마다 기존 트레이를 **destroy 하고 다시 스폰**했다.
그래서 눈으로 5.3cm 맞춰 놓은 것이 다음 실행에 날아갔다.

```python
if len(existing) == len(pairs) and existing:
    # 위치를 안 건드린다 — 사람이 맞춘 값이 이긴다
else:
    # 짝 수가 바뀐 때만 다시 만든다
```

⚠ §9-16 에서 카운터 트레이에 같은 규칙을 넣어 놓고 2인용에는 안 넣었다.
  **자동 배치 스크립트는 전부 이 성질을 가져야 한다.**

---

## 9-31. 2인용 트레이에 감자 세트 — 「배치 그대로」는 불가능했다

1인용과 **같은 크기**로 넷을 올리려는데, 현재 배치(버거박스 둘을 양 끝 ±10.8)를
지키면 남는 곳이 이것뿐이다 —

```
가운데 띠   깊이  8.50
양옆 띠     폭    6.05
```

| | 크기 | 가운데 띠(8.5) | 옆 띠(6.05) |
|---|---|---|---|
| `Food_Fries` ×0.64 | 9.3 × 12.2 | **안 들어감** | **안 들어감** |
| `Food_Coke` ×0.75 | 8.0 × 6.7 | 들어감 | **안 들어감** |
| `Food_FriesLoose` ×0.8 | 8.9 × 5.0 | 들어감 | 들어감 |

면적으로는 65% 라 여유가 있는데 **모양이 안 맞는다.** 버거박스 둘이 깊이를
통째로 먹어서 봉투감자가 갈 곳이 없다.

→ 버거박스 둘을 **왼쪽 열**로 모으고 오른쪽을 음식 열로 썼다.

```
burger_box   (−6.0, −10.8)     fries        (7.2, −10.0)   roll −90 · yaw −180
burger_box2  (−6.0,  10.8)     coke         (7.5,   4.0)
receipt      ( 6.35, −5.0)     fries_loose  (7.5,  10.5)
                               ketchup      (7.5,  15.3)
```

⚠⚠ 대신 **마주보는 둘이 각자 자기 앞에서 버거를 집는 그림은 약해진다.** 「버거를
  양 끝에」가 더 중요하면 감자 세트 쪽을 줄여야 한다 — 둘 다는 못 한다.

⚠ 영수증은 버거박스가 아니라 **봉투감자 밑**에 깐다 (요청). 그래서 띄움(0.35)도
  봉투감자가 받는다.

### 1인용 — 셋을 오른쪽으로 2cm

```
coke        (−7, 6)  -> (−7, 8)
fries_loose ( 7.9, −2) -> ( 7.9, 0)
ketchup     ( 8,  2.5) -> ( 8, 4.5)
```

⚠ 「트레이 기준 오른쪽」은 **+Y** 다. `ll_tray_hands.py` 가 트레이 +Y 를 **든
  사람의 오른쪽**으로 맞춰 놨다 (§9-21). +X 로 밀면 흩어진 감자가 안쪽 바닥
  밖으로 나간다 (14.35 > 12.35).

---

## 9-32. 손에 드는 버거 (`Eating_a_Burger` 용)

`Downloads/burger/` 의 FBX + PNG 를 들여왔다 (`ll_burger_held_import.py`).

```
/Game/BurgerHouse/Props/Food/Food_BurgerHeld          17,982면 · 슬롯 1
/Game/BurgerHouse/Props/Food/Textures/T_BurgerHeld_BC
/Game/BurgerHouse/Materials/M_BurgerHeld              베이스컬러 = 텍스처 · 러프 0.7
```

⚠ 임포트하면 이름이 **원본 파일명 그대로**(`A_detailed_3D_model_o_1207173805_refine`)
  들어온다. 규약에 맞춰 바꿨다.

⚠ `Props/Burger` 가 이미 있다 (셸의 진열용). 손에 드는 것과 구별하려고
  **`Food_BurgerHeld`** 로 이름을 나눴다.

⚠ 콜리전은 **안 만들었다.** 손에 드는 장식이라 켜면 손님 캡슐을 밀거나 내비를
  막는다 (§9-6).

### ⚠⚠ 크기가 **20배** 다

```
들어온 크기   195.9 × 199.4 × 160.1 cm
실물 버거      약 10cm  ->  스케일 **0.05**
```

붙일 때 스케일 0.05 를 기본으로 잡을 것. 피벗은 거의 중심(−1.4, −1.2, −0.4)이라
§9-27 의 `Food_*` 처럼 월드 원점에 있지는 않다 — 그쪽보다 다루기 쉽다.

⚠ 17,982면은 손에 드는 소품치고 많다. 손님이 8명이면 14만면이다. 신경 쓰이면
  `SM_BurgerBox`(12만 → 4천으로 줄였던 것)처럼 데시메이트할 것.

### 아직 **안 붙였다**

「임포트만」 요청이라 `BP_Customer` 에 컴포넌트를 추가하지 않았다. 붙이려면
`Eating_a_Burger` 몽타주가 도는 동안만 보이게 해야 하는데, 지금 구조에는
「행동별로 소품을 켜고 끄는」 길이 없다 — `FCustomerActionVariant` 에 소품 필드를
두거나 애님 노티파이를 쓰는 것이 자연스럽다.

---

## 9-33. 착석 체류 시간을 **1~3분**으로

### 시간이 어떻게 만들어지나

```
총 재생 횟수 N = Plays + rand(0, RandomExtraRepeats)        (균등)
한 번 재생    = 몽타주 길이 + rand(0, RandomExtraSeconds)   (균등)
체류 = Σ (몽타주 + 덤)
```

`Sit-Eat` 몽타주 넷은 가중치가 같다 — 6.87 / 7.83 / 8.03 / 9.20 (평균 **7.98**).

### 값 푸는 법 — **양 끝을 둘 다** 묶는다

```
최소 = Plays × 6.87                       ≥  60
최대 = (Plays + Repeats) × (9.20 + 덤)    ≤ 180
```

- `Plays ≥ 60 / 6.87 = 8.73`  →  **9**
- 덤 3 일 때 `(9 + R) × 12.20 ≤ 180`  →  `R ≤ 5.75`  →  **5**

| | 전 | 후 |
|---|---|---|
| `Plays` | 1 | **9** |
| `RandomExtraRepeats` | 2 | **5** |
| `RandomExtraSeconds` | 6 | **3** |
| 최소 | 6.9초 | **61.8초** |
| 최대 | 45.6초 | **170.8초** |
| 기대값 | 22.0초 | **109.1초** |

⚠ 덤을 0 으로 두면 2분 내내 **쉬지 않고 먹는다.** 3초를 남겨 한 입마다 잠깐
  멈추게 했다 — 그 동안은 `LocoSlot` 의 앉은 몽타주가 비쳐 자연스럽게 앉아 있다.

⚠ 「최소 61.8초」는 **가장 짧은 몽타주가 9번 연속 뽑히고 덤이 전부 0** 인 경우다.
  평균 재생 길이가 7.98 이라 보통은 72초 밑으로 안 내려간다.

⚠ 워크인(`Root/Seat/S_Eat/Eating`)과 프리필(`Root/P_Eating/.../PE_Eat`) **둘 다**
  같은 값으로 뒀다. 같은 「테이블에서 먹는」 상태라 갈라 둘 이유가 없다.
  (지금 프리필은 전부 장식이라 이 갈래를 안 타지만, 갈라 두면 나중에 안 맞는다.)

### 실측 — 7분 PIE

```
121.0초 · 90.4초      (앞선 설정에서는 7.0 ~ 32.1초 · 평균 17.9초, 38건)
```

⚠ 이 시간에는 **좌석까지 걸어가는 시간**(`Sit At Seat` 이 직접 걷는다, §9-14)이
  포함돼 있다.

⚠⚠ 체류가 길어지면 **정원이 더 오래 묶인다.** `FixedDesiredAlive 8` 에서 장식
  5명을 빼면 워크인은 상시 3명인데, 한 명이 2분을 앉아 있으면 회전이 그만큼
  느려진다. 스폰 간격(240초)과 같이 볼 것.

---

## 9-34. 대기 중 통화 — 폰을 손에 붙이기

### ⚠⚠ FBX 가 폰 한 대가 아니라 **쇼케이스 장면**이었다

`Downloads/phone/I+phone+17+pro.fbx` 를 합치지 않고 구우면 메시가 **30개**, 전부
겹친 범위가 **729 × 36 × 404 cm** 다.

```
Cylinder_024  중심 x   22.33   196.65 × 31.46 × 404.06   <- 폰 A **본체**
Cylinder_038  중심 x −233.23   같은 크기                  <- 폰 B
Cylinder_045  중심 x  299.01   같은 크기                  <- 폰 C
Cylinder_022… 44.95 원반   Cube_003… 16.32 사각판   Torx… 3.98 나사
```

색상 변형 **세 대**(textures 의 Blue/Orange/Silver 와 짝이 맞는다)가 나란히 있고,
한 대에 부품 10개씩 정확히 30개다.

⚠⚠ 그래서 `combine_static_meshes_behavior = ALL` 로 합치면 **세 대가 한 덩어리**가
  된다. 「합치기가 안 먹는다」가 아니라 **합치면 안 되는 것**이었다.

⚠ `combine_static_meshes` 불리언은 **deprecated** — behavior 만 본다.

⚠ 「본체」를 얇은 사각판(`Cube_003`, 16.32 × 1.08 × 16.32)으로 착각하기 쉽다.
  **정사각형**이라 폰이 아니다. 부피가 가장 큰 것을 골라야 한다.

### 머지는 못 했다 — **본체 하나만** 쓴다

```
EditorLevelLibrary.merge_static_mesh_actors        deprecated · 조용히 None
get_editor_subsystem(StaticMeshEditorSubsystem)    -> None (커맨드릿에 모듈이 안 뜬다)
```

그래서 `Cylinder_024` 하나를 `Prop_Phone` 으로 쓴다. 손에 들고 귀에 대는
크기(15cm)에서 카메라 원반·나사는 1mm 급이라 거의 안 보인다.

⚠ 카메라 범프까지 원하면 **에디터에서** 부품 10개를 레벨에 놓고
  `Actor > Merge Actors` 로 구우면 된다. 이름은 스크립트 로그가 찍는다 —
  `Cube_003 · Cylinder_022~028 · Torx_Flat_Top_006/007`.

⚠⚠ 임시 폴더를 지울 때 **머티리얼을 먼저 옮겨야** 한다. 메시가 참조하므로
  폴더째 지우면 슬롯이 비어 회색이 된다 — 실제로 한 번 그랬다. 11개를 옮겼다.

⚠ `textures/` 의 색상 변형이 **webp** 라 언리얼이 못 읽고 **조용히 건너뛴다.**
  머티리얼은 FBX 가 품고 온 것으로 붙었다 (슬롯 11개, 빈 슬롯 0).

### 어느 손인가 — 숫자로 고른다

`Talking_On_A_Cell_Phone` 에서 머리까지의 거리 —

```
hand_r  최소 26.5 · 평균 37.7      <- 이쪽이 귀로 간다
hand_l  최소 40.5 · 평균 65.1
```

손 **로컬 공간**에서 머리는 애님 내내 거의 안 움직인다 —
(−21.8, −11.9, 10.0) ~ (−23.0, −11.9, 11.6). 고정 오프셋으로 충분하다.

```
화면(폰 −Y)  머리 쪽을 본다
긴 축(폰 +Z) 손 기준 「위」에서 Y 성분을 뺀 방향
X = Y × Z                                  (§9-21 의 그 순서)

PhoneHandOffset  loc(−19.42, −5.01, 17.16) · rot(154.0, −30.8, 131.9) · scale 0.0371
런타임 크기 7.30 × 1.17 × 15.00 cm
되짚기: 폰 중심이 머리에서 2.5 ~ 4.2cm
```

⚠ `Prop_Phone` 은 긴 축이 **Z**(404), 얇은 축이 **Y**(31.5) 다. 축을 잘못 잡으면
  옆으로 눕는다.

### 보이고 숨기는 규칙

`FCustomerActionVariant::bShowPhone` 을 켜면 **그 몽타주가 도는 동안만** 보인다.

```cpp
// PlayAction — 고른 변형이 요구하면 기억한다
PhoneMontage = Found->Variants[Idx].bShowPhone ? M : nullptr;

// Tick — 재생 중인지로 켜고 끈다
const bool bShow = PhoneMontage && Anim->Montage_IsPlaying(PhoneMontage);
```

⚠⚠ 「행동이 끝났는지」로 판단하면 안 된다. 대기 상태는 몽타주가 끝나도
  `Order.Ready` 이벤트를 기다리며 **남아 있어서** 폰이 빈손에 떠 있게 된다.

⚠ 생성자에서 붙인 소켓은 못 믿는다 — 트레이와 **같은 이유**로 `BeginPlay` 에서
  다시 붙인다 (§9-20).

### ⚠⚠⚠ `try/except` 가 조용한 실패를 삼켰다

파이썬은 bool `UPROPERTY` 의 앞 `b` 를 **뗀다** — `bShowPhone` 은 `show_phone` 이다.
`b_show_phone` 으로 쓰면 예외가 나는데, 그것을 `try/except` 로 감쌌더니
**플래그가 영원히 꺼진 채** 스크립트는 「성공」으로 보였다.

⚠ 에셋을 쓰는 코드에 `try/except: pass` 를 두지 말 것. 이 프로젝트에서 조용한
  실패가 몇 번째인지 세기 어렵다.

### 대기 몸짓 구성 (최종)

```
AM_Idle                     w=1.0  29%
AM_Texting                  w=0.3   9%
AM_Texting_While_Standing   w=1.3  38%
AM_Talking_On_A_Cell_Phone  w=0.8  24%   <- 이때만 폰이 보인다
```

⚠ 37.2초짜리라 예전에 뺐던 것이다. `Wait` 는 이벤트가 빼내므로 길이가 체류
  시간을 안 정한다 (§9-28 과 같은 이유).

---

## 9-35. `Sitting__1_` 삭제 — 앉은 기준 자세를 **먹는 애님**으로

### ⚠⚠ 그냥 지우면 앉은 자세가 통째로 사라진다

`Sitting__1_` 을 쓰던 곳은 둘이다 —

```
AM_Loco_Sit                앉은 손님 **전원**의 LocoSlot 기준 자세
ABP_Customer ▸ Sit 상태    상태 기계 쪽 (몽타주가 비면 이게 나온다)
```

### 무엇으로 바꾸나 — 골반 높이로 고른다

`Loco_Sit` 은 `LocoSlot` 에 **깔리는 기준 자세**다. 그 위에 `DefaultSlot` 의 행동
몽타주(먹기)가 얹히고, 먹기가 끝나 슬롯이 비면 이 자세가 드러난다.

```
Eating_a_Burger    골반 53.3 · 발 5.7      <- 행동 몽타주
Eating_Fries       골반 52.8 · 발 5.6
Eating_a_Drumstick 골반 52.8 · 발 5.7
Eating_Frantically 골반 52.8 · 발 5.7
SitIdle_Male       골반 52.8 · 발 2.2      골반은 맞고 발이 3.5cm 낮다
Sitting__1_        골반 58.5 · 발 12.6     <- **5.7cm 높다**
```

⚠⚠ `Sitting__1_` 은 먹는 애님보다 골반이 **5.7cm 높았다.** 즉 **한 입이 끝날
  때마다 손님이 5.7cm 튀어 올랐다.** 지우면서 그 버그도 같이 사라진다.

→ `Eating_a_Burger` 로 옮겼다. 기준 자세와 행동이 같은 계열이라 이음매가 없다.

⚠ `Sitting` 은 이름과 달리 골반이 57.5~94.5 로 **앉았다 일어나는** 동작이다.
  이름으로 고르면 안 된다.

### ⚠⚠⚠ 몽타주가 **옛 시퀀스를 계속 물고 있었다**

`ll_montages.py` 의 `make_montage` 는 「이미 있으면 슬롯·블렌드만 맞춘다」였다.
그래서 `ACTIONS` 의 시퀀스를 바꿔도 몽타주는 그대로였고, 로그에는 **옛 길이**가
찍혔다 (5.93s). 아무도 안 본다.

고친 것 둘 —

1. 만들기 전에 **의존성**을 보고 소스가 다르면 지운다 (길이로 비교하면 다른
   시퀀스가 같은 길이일 때 속는다)
2. 만든 뒤 **되읽어** 확인하고, 안 맞으면 크게 찍는다

⚠⚠ 그런데 `delete_asset` 은 **같은 프로세스에서 이미 로드된 에셋을 못 지운다** —
  `True` 를 돌려주고도 `does_asset_exist` 가 계속 참이라 옛 것을 다시 불러온다.
  §9-13 의 「True 를 주고도 파일을 남긴다」와 같은 계열이다.

→ 확실한 길은 **디스크에서 `rm`** 하고 새 프로세스로 다시 굽는 것이다.

---

## 9-36. 관통은 콜리전 문제가 **아니다** — 평가

### 실측

```
캡슐        반지름 34 · 반높이 88 · QUERY_AND_PHYSICS
메시        QUERY_ONLY · 피직스에셋 PA_Mannequin (있다)
기물        Table_A 3 · Chair_A 3 · Sofa_Unit 1 · Kiosk_A 4 프리미티브
셸          프리미티브 0 · **CTF_USE_COMPLEX_AS_SIMPLE** (삼각형 단위로 막는다)
```

팔다리가 캡슐 축에서 얼마나 벗어나는지 —

| 애님 | 최대 | 캡슐(34) 밖 |
|---|---|---|
| `Reaching_Out` | 70.1 (hand_r) | **+36.1** |
| `Talking_On_A_Cell_Phone` | 56.8 (hand_l) | **+22.8** |
| `Eating_a_Burger` | 52.0 (hand_l) | **+18.0** |
| `Walk_Fwd_Manny` | 46.6 (foot_l) | +12.6 |
| `Texting_While_Standing` | 39.8 | +5.8 |
| `Idle` | 33.5 | −0.5 |

### 평가 — **콜리전으로는 못 고친다**

⚠⚠ 언리얼의 캐릭터 이동은 **캡슐 하나만** 쓴다. 피직스에셋은 쿼리·래그돌용이고
  **이동을 막지 않는다.** 그래서 캡슐 밖으로 나간 팔다리는 무엇을 하든 통과한다.

- **캡슐을 키우면**: 반지름 70 이면 `Reaching_Out` 은 막히지만, 문(내비 폭)과
  테이블 사이를 못 지나간다. 손님이 길을 잃는다. 못 쓴다.
- **기물 콜리전을 더 촘촘히**: 이미 있다 (셸은 삼각형 단위). **막는 쪽이 아니라
  뚫는 쪽이 문제**라 효과가 없다.
- **피직스에셋으로 막기**: 캐릭터무브먼트가 안 본다. 별도 밀어내기 코드를 붙여야
  하고, 앉은 손님은 `MOVE_None` 이라 그것마저 안 돈다.

### 그럼 무엇을 고치나 — **자세와 자리**다

관통이 실제로 보이는 곳은 셋이고, 셋 다 **배치·애님** 문제다.

| 어디 | 무엇이 뚫나 | 고칠 곳 |
|---|---|---|
| 앉은 손님 | 다리가 테이블·의자를 뚫는다 | `ASlotPoint` 의 `LocalOffset`·`UseYawOffset`, `SitZAdjust` |
| 키오스크·픽업 | 손이 기물을 뚫는다 (`Reaching_Out` +36cm) | 슬롯을 기물에서 더 떼거나 팔이 덜 뻗는 애님 |
| 좁은 통로 | 어깨·팔이 벽을 스친다 | 내비 에이전트 반경(34)보다 **통로를 넓게** |

⚠ 이번에 `Sitting__1_` → 먹는 애님으로 바꾸면서 **골반 5.7cm 차이**가 사라졌다.
  앉은 쪽 관통이 그만큼 줄어든다 — 「애님을 고치는 쪽」의 실례다.

⚠ 그래도 **완전히는 안 없어진다.** 손이 테이블 위 30cm 를 지나가는 것은 캡슐이
  표현할 수 없는 일이고, 상용 게임도 대개 그냥 둔다. 눈에 띄는 것부터 —
  **좌석 오프셋 → 키오스크 슬롯 거리 → 통로 폭** 순으로 손보는 것이 값이 싸다.

---

## 9-37. 에디터에서 **손으로** 조정하는 법 — 손잡이와 현재 값

9-36 의 결론은 「콜리전이 아니라 **배치와 자세**」였다. 그 배치·자세는 전부
**에디터 Details 패널에서 바로** 고칠 수 있다 — C++ 빌드도 스크립트도 필요 없다.
아래는 **실측한 현재 값**이다.

### A. 앉은 손님이 테이블·의자를 뚫을 때

레벨에서 `Slot_Seat_**` 액터를 고른다 (전부 37개).

| 속성 | 현재 값 | 무엇을 하나 |
|---|---|---|
| `LocalOffset` | 의자 29석 `(0, 6, 0)` · 소파 8석 `(0, 18, 0)` | 가구 기준 오프셋. **+Y 가 앉은 사람의 앞(테이블 쪽)** 이다. 줄이면 몸이 테이블에서 멀어진다 |
| `UseYawOffset` | 의자 `90` · 소파 `0` | 앉은 방향. 무릎이 팔걸이를 스치면 여기를 ±5 씩 |

⚠ `LocalOffset` 은 **가구 로컬**이다. 월드 Y 가 아니다 — 의자가 돌아 있으면
  방향도 같이 돈다. 값을 바꾸고 액터를 움직여 보면 눈으로 확인된다.

⚠ 의자와 소파는 **정면 축이 다르다** (의자 +Y · 소파 +X). 그래서 `UseYawOffset`
  이 다른 것이고, 한 값으로 통일하면 소파 8석이 벽을 본다 (예전에 겪었다).

`BP_Customer` Details —

| 속성 | 현재 값 | 무엇을 하나 |
|---|---|---|
| `SitZAdjust` | `0` | 앉은 몸의 높이. 올리면 다리가 의자를 덜 뚫지만 뜬다 |
| `SeatBlendTime` | `0.5` | 서다 -> 앉기 보간 시간. 줄이면 순간이동처럼 보인다 |

### B. 키오스크·픽업에서 손이 기물을 뚫을 때

`Reaching_Out` 의 손은 캡슐 축에서 **70cm**(캡슐 밖 +36cm) 까지 나간다.

| 어디 | 현재 | 무엇을 하나 |
|---|---|---|
| `Slot_Kiosk_**` 의 `LocalOffset` | `(0, 60, 0)` · `UseYawOffset −90` | 키오스크에서 60cm 앞에 선다. 손 36cm 는 아직 안 닿는다. 더 뚫으면 **70~80** 으로 |
| `Slot_Pickup_**` | `LocalOffset (0,0,0)` — **월드 위치가 곧 서는 자리** | 카운터 앞면에서 약 97cm. 레벨에서 **그냥 끌어서** 조정한다 |

⚠ 픽업 슬롯만 `LocalOffset` 이 0 이다. 가구에 붙여 만든 것이 아니라 좌표를
  손으로 적어 놓은 것이라서다 — 그래서 **액터를 직접 옮기는 게 맞다.**

### C. 애니메이션 자체를 바꾸고 싶을 때

`BP_Customer` > Details > **`Actions`** 맵을 열면 동작마다 `Variants` 가 있고
각 변형에 `Montage` 와 `Weight` 가 있다. **`Weight` 를 0 으로 두면 그 동작만
꺼진다** — 에셋을 지울 필요가 없다.

캡슐 밖으로 나가는 양(작을수록 안전) —

```
Reaching_Out              +36.1    <- 키오스크
Talking_On_A_Cell_Phone   +22.8
Eating_a_Burger           +18.0
Walk_Fwd_Manny            +12.6
Texting_While_Standing     +5.8    <- 가장 얌전하다
Idle                       -0.5
```

예: 대기 중 통화가 옆 테이블을 스치면 `Idle_Look` 의
`AM_Talking_On_A_Cell_Phone` 가중치 `0.8` 을 낮추고
`AM_Texting_While_Standing`(현재 `1.3`) 을 올린다.

### D. 통로가 좁을 때 — 여기는 **건드리지 말 것**

| 어디 | 현재 | 왜 |
|---|---|---|
| `BP_Customer` > `CapsuleComponent` > `Capsule Radius` | `34` | 내비 `AgentRadius` 와 **짝이다.** 한쪽만 바꾸면 손님이 못 지나가거나 벽에 낀다 |
| `Config/DefaultEngine.ini` > `AgentRadius` | `34` | 통로 최소 폭 **68cm** 를 정한다. 올리면 내비메시를 다시 구워야 한다 |

통로가 좁으면 **손님이 아니라 가구를 옮기는 쪽이 싸다.**

### ⚠⚠ 고친 값이 다음 스크립트 실행에 지워진다

`ll_slots.py` 는 돌 때마다 슬롯을 **다시 스폰하면서** `LocalOffset` ·
`UseYawOffset` 을 자기 표(`SPEC` · `USE_YAW_OFFSET`, 70~97행)에서 덮어쓴다.
에디터에서 고친 값을 지키려면 둘 중 하나다 —

1. 같은 값을 `ll_slots.py` 의 표에도 적어 둔다 (권장 — 재생성이 된다), 또는
2. 그 스크립트를 다시 돌리지 않는다.

§9-30 에서 이미 한 번 당했다 (`ll_table_tray.py` 가 손으로 옮긴 트레이를 날렸다).
`SitZAdjust` · `Actions` 가중치 · 픽업 액터 위치는 덮어쓰는 스크립트가 **없다.**

### 값을 바꾼 뒤

- 슬롯 액터 · `BP_Customer` 를 고쳤으면 **레벨 저장** 후 PIE 로 확인한다.
- 내비 관련(`AgentRadius`)만 **Build > Build Paths** 가 필요하다.
- 나머지는 다시 굽거나 컴파일할 것이 없다.

---

## 10. 그 밖에 남은 것

- 좌석 선택이 **결정적**이다 — 늘 같은 순서로 같은 자리부터 찬다.
  다양성이 필요하면 `ClaimSlot(bRandom=true)`.
- 주문번호 표시기 — 재질 `M_OrderDisp` 만 있고 UMG 위젯이 없다.
- `UCrowdFollowingComponent` 는 아직 미적용 (RVO 만 켰다). 손님이 더 늘면
  디투어 크라우드로 올리는 것이 다음 수다.
- **정원에 장식 손님이 포함된다** — `FixedDesiredAlive 8` 에서 불멸 장식 5명을
  빼면 워크인은 상시 **3명**뿐이다. 가게를 붐비게 하려면 `Alive` 를 장식과
  워크인으로 나누거나 정원을 올려야 한다.
- `PrefillPlacements` 를 만드는 `ll_spawner.py` 가 **저장소에 없다.** 지금 레벨의
  5개 항목은 재생성이 안 된다 — 의자를 옮기면 손으로 고쳐야 한다.
