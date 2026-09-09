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
SpawnInterval       40s    ← ±5s 로 흔들린다
SampleSpacing       40     ← 홀이 6×11m 라 촘촘하게
LateralWidth        60
PrefillOnBeginPlay  true
PrefillPlacements   5명    ← 레벨이 열릴 때부터 앉아 있는 손님
```

⚠ `FixedDesiredAlive` 를 프리필 인원으로 다 채우면 **문으로 들어오는 손님이 영영
안 생긴다.** 정원의 절반쯤을 프리필로 두는 것이 적당하다.

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

---

## 6. ⚠ 조용히 실패하는 곳 — 여기만 조심하면 된다

| 빠뜨리면 | 증상 | 오독하기 쉬운 것 |
|---|---|---|
| NavMesh 굽기 | 스폰 **0** · `Move To` 실패 | 「스포너가 고장났나」 |
| `Auto Possess AI` | StateTree 가 **시작조차 안 함** | 「애님이 없어서 안 움직이나」 |
| 통보보다 컨트롤러를 먼저 붙임 | 프리필 전원이 기본 갈래로 | 「트리 자식 순서를 잘못 놨나」 |
| `PrefillRoles` 철자 오타 | 프리필이 **전부** 기본 갈래로 | 위와 증상이 같다 |
| 루프 상태에 「앉는 동작」을 씀 | **섰다 앉았다 반복** | 「전이가 덜덜 떤다」 (§7) |
| 트리의 액션 이름 오타 | 그 상태만 **4초 폴백** | 「몽타주가 없나」 (§8-1) |
| 레벨 액터 프로퍼티만 고치고 저장 | **아무 일도 안 일어남** | 아래 참고 |
| 메시 컴포넌트 yaw 가 0 | 손님이 **전부 90도 옆을 봄** | 「좌석 각도가 틀렸나」 (아래) |

여덟 다 **에러가 안 난다.** 그래서 표로 박아 둔다.

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

---

## 7. 몽타주

`Content/BurgerHouse/Anims/Montages/` 에 11개가 있고 `BP_Customer > Actions` 에
꽂혀 있다. `ll_montages.py` 가 만든다 (다시 만들려면 그 스크립트를 돌린다).

```
슬롯 DefaultSlot (전신) — 서 있는 손님
  Idle_Look       AM_Texting_While_Standing 1.0 · AM_Talking_On_A_Cell_Phone 0.5
                  AM_Texting 0.4 · AM_Telling_A_Secret 0.2
  Interact_Reach  AM_Reaching_Out 1.0

슬롯 UpperBody (상체만) — 앉은 손님
  Sit_Idle        AM_Eating_a_Burger 1.0 · AM_Eating_Fries 1.0
                  AM_Eating_a_Drumstick 0.8 · AM_Eating_Frantically 0.5
                  AM_Sit_Texting 0.6 · AM_Sit_Talking_On_A_Cell_Phone 0.5
  Sit_Eat         AM_Eating_* 4개 (전부 1.0 = 균등)
```

`Sit_Idle` 은 **장식 손님이 끝없이 돌리는 풀**이다 (먹기 + 폰 만지기).
`Sit_Eat` 은 걸어 들어온 손님이 트리에서 쓰는 것 — 먹는 것만 들었다.

⚠ **`AM_Sit_Texting` · `AM_Sit_Talking_On_A_Cell_Phone` 은 서 있는 쪽과 애님은
같지만 몽타주 에셋이 다르다.** 몽타주는 슬롯을 **하나만** 갖기 때문이다 —
같은 애님을 앉은 손님(UpperBody)과 서 있는 손님(DefaultSlot)이 같이 쓰려면
두 벌이 필요하다.

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
| `Sitting__1_` | 62.1 | 62.2 | **앉아 있는 동작** ✓ 5.9s — `Sit` 상태가 쓴다 |
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

### ⚠ 슬롯이 둘이다

```
Sit_Eat (AM_Eating_*)          슬롯 UpperBody     ← 상체만
Idle_Look · Interact_Reach     슬롯 DefaultSlot   ← 전신
```

`Eating_*` 은 **서서 먹는 동작이다** (hips Z 104.3 = `Idle` 과 같고, 다리는 아예
안 움직인다). 전신으로 재생하면 앉은 손님이 일어서므로 **상체만 덧씌운다.**

⚠ 그래서 AnimGraph 에 `UpperBody` 를 받는 노드가 **있어야** 한다. 없으면 그
몽타주는 아예 재생되지 않는다 — §8-1.

⚠ 몽타주를 새로 만들면 팩토리가 `DefaultSlot` 으로 만든다. `Sit_Eat` 에 넣을
것이면 슬롯을 `UpperBody` 로 바꿀 것.

⚠ 예전에 쓰던 `AM_Sitting*` 몽타주 4개는 그대로 남아 있다 (미사용). 다시 쓰려면
`ll_montages.py` 의 `ACTIONS` 에 되돌려 넣으면 된다.

---

## 8. 남은 에디터 작업

전부 **파이썬으로 못 하는 것**들이다. UE 5.8 파이썬은 그래프 노드를 만들 수 없고,
StateTree 태스크 내부(`InstancedStruct`)와 몽타주 `SequenceLength` 는 읽기 전용이다.

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

### 8-2. `Idle_Look` 몽타주가 너무 길다

`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다.**

| | |
|---|---|
| `AM_Talking_On_A_Cell_Phone` | 37.2s |
| `AM_Texting_While_Standing` | 23.6s |
| `AM_Texting` | 18.5s |
| `AM_Telling_A_Secret` | 10.9s |

**눈에 띄는 결과** — `Enter` 가 `Idle_Look` 을 한 번 재생하고 끝나므로 손님이
문 앞에서 **최대 37초를 서 있다가** 키오스크로 간다.

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
| `MaxWalkSpeed` | 600 | **100** | `Walking` 애님 실측이 **97.5cm/s** — 6배로 달려 발이 미끄러지고 목표를 지나쳐 되돌아왔다 |
| `MaxAcceleration` | 2048 | 400 | 급출발·급정지가 앞뒤 흔들림으로 보인다 |
| `BrakingDecelerationWalking` | 2048 | 600 | 위와 같음 |
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

⚠ **남은 부작용** — 이동이 끝나면 마지막 진행 방향 그대로 선다. 키오스크에
도착한 손님이 키오스크를 등질 수 있다. 앉는 쪽은 `SitAt` 이, 프리필은
`TeleportToPoint` 가 회전을 직접 넣으므로 영향이 없다. 고치려면 도착 시
`Claim Slot` 태스크의 `UseRotation` 을 적용하는 한 수가 필요하다 (트리 배선).

⚠ `MaxWalkSpeed` 를 **10 밑으로 내리지 말 것.** Locomotion 의 Idle↔Walk 전이가
`Speed > 10` 이라 그 밑에서는 **Idle 포즈로 미끄러진다.**

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
