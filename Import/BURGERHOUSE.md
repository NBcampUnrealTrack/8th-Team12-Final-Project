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
  Anims/Mixamo/ABP_Customer + 애님 17
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

넷 다 **에러가 안 난다.** 그래서 표로 박아 둔다.

---

## 7. 몽타주

`Content/BurgerHouse/Anims/Montages/` 에 9개가 있고 `BP_Customer > Actions` 에
꽂혀 있다. `ll_montages.py` 가 만든다 (다시 만들려면 그 스크립트를 돌린다).

```
Idle_Look       AM_Texting_While_Standing 1.0 · AM_Talking_On_A_Cell_Phone 0.5
                AM_Texting 0.4 · AM_Telling_A_Secret 0.2
Interact_Reach  AM_Reaching_Out 1.0
Sit_Eat         AM_Eating_a_Burger · AM_Eating_a_Drumstick
                AM_Eating_Frantically · AM_Eating_Fries      (전부 1.0 = 균등)
```

가중치는 합이 1일 필요 없다. 기본 동작을 1.0, 가끔 나오는 몸짓을 0.2 로 두면
대개 평범하고 가끔 눈에 띄는 그림이 된다. `Sit_Eat` 은 넷을 고르게 쓰려고 전부
1.0 으로 뒀다.

고르는 것은 `ACustomerCharacter::PickVariant` 가 한다 — 가중 무작위이고,
`bAvoidRepeatVariant` 가 같은 것이 연속으로 나오지 않게 막는다.

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

⚠⚠ **이 애님들은 서서 먹는 동작이다** (hips Z 104.3 = `Idle` 과 같다). 다리는
아예 안 움직인다. 그래서 **상체만 덧씌워야** 앉아서 먹는 그림이 된다 —
그냥 `DefaultSlot` 으로 재생하면 앉아 있던 손님이 일어선다. §8-1 ④ 참조.

⚠ 예전에 쓰던 `AM_Sitting*` 몽타주 4개는 그대로 남아 있다. 다시 쓰려면
`ll_montages.py` 의 `ACTIONS` 에 되돌려 넣으면 된다.

⚠ 몽타주는 **`DefaultSlot`** 을 써야 `ABP_Customer` 의 `Slot 'DefaultSlot'` 노드를
타고 화면에 나온다. 팩토리가 기본으로 그렇게 만들지만, 손으로 만들 때는 확인할 것.

---

## 8. 에디터에서 해야 할 것 — 네 가지

아래는 전부 **파이썬으로 못 하는 것**들이다. UE 5.8 파이썬은 그래프 노드를
만들 수 없고, StateTree 태스크 내부(`InstancedStruct`)와 몽타주 길이는 읽기
전용이다.

### 8-1. ⚠⚠ 앉은 자세가 없다 — AnimBP 에 `Sit` 상태

**지금 앉은 손님은 서 있는 자세다.** `ABP_Customer` 는 앉기를 전혀 모른다 —
상태가 `Idle`·`Walk` 둘뿐이고, `SitAt()` 은 캡슐만 의자에 놓는다.

```
SitAt() → MOVE_None → Speed 0 → 상태 기계가 Idle(서 있는 자세)을 낸다
                                    ↓
        Sit_Eat 몽타주가 슬롯 위에 얹혀 4.4초 동안만 앉은 것처럼 보이고
                                    ↓
              몽타주가 끝나면 슬롯이 원래 포즈를 통과시킨다
                                    ↓
                      **의자에서 벌떡 선다**
```

⚠ **몽타주로 자세를 붙드는 건 구조적으로 깨진다.** 앉기는 일회성 몸짓이 아니라
**지속 상태**라서 상태 기계가 알아야 한다. 몽타주는 그 위에 얹는 몸짓용이다.

`bIsSitting` (bool) 변수는 **이미 만들어 뒀다.** 나머지를 잇는다:

**① Event Graph — 변수 채우기**

```
기존 [Try Get Pawn Owner] 출력에서 드래그
  → Cast to CustomerCharacter
  → 그 노드 우클릭 > Convert to Pure Cast     ← 실행 핀이 사라진다
  → As Customer Character 에서 드래그 → Is Sitting
  → [Set bIsSitting] 의 입력에 연결

흰 실행선 :  Event Blueprint Update Animation ▷ Set Speed ▷ Set bIsSitting
```

⚠ **Pure Cast 로 바꾸는 이유** — 안 바꾸면 Cast 에 실행 핀이 생겨서 흰 선을
Cast 까지 통과시켜야 한다. 순수로 만들면 데이터선만 이으면 된다.

⚠ `Set bIsSitting` 은 순수 노드가 아니다 — **흰 선을 반드시 이을 것.** 안 이으면
값이 영원히 false 이고, 증상은 「Sit 상태로 안 넘어간다」로만 보인다
(§6 의 「조용히 실패하는 곳」과 같은 부류다).

**② AnimGraph > Locomotion — `Sit` 상태 추가**

```
빈 곳 우클릭 > Add State  → 이름 Sit
Sit 더블클릭 → Content Browser 에서 Anims/Sitting 을 끌어다 놓기
  → Pose 를 Output Animation Pose 에 연결
  → Details 에서 Loop Animation ☑        ← 안 켜면 마지막 프레임에서 얼어붙는다
```

**③ 전이 두 개**

```
Idle 테두리 → Sit 로 드래그 :  ⬤ 더블클릭 → bIsSitting 을 Can Enter Transition 에 바로 연결
Sit  테두리 → Idle 로 드래그 :  ⬤ 더블클릭 → bIsSitting → [NOT] → Can Enter Transition
```

⚠ `bIsSitting` 은 bool 이라 `>` 같은 비교가 필요 없다. 그대로 잇는다.
⚠ `Walk → Sit` 은 안 만들어도 된다. 앉을 때 `MOVE_None` 이라 Speed 가 0 이 되어
  `Walk → Idle → Sit` 으로 두 단계를 거친다.

**④ ⚠⚠ 먹는 동작은 상체만 덧씌운다 — 안 그러면 다시 일어선다**

`Eating_*` 애님은 **서서 먹는 동작**이다. 측정값:

```
Idle              hips Z 103.5     ← 서 있음
Walking                  100.7
Sitting                   92.5
Sitting_Thumbs_Up         77.6
Eating_* 4개             104.3     ← Idle 과 같다 = 서 있다
```

넷 다 허벅지 회전이 정확히 `180.0` 으로 같다 — **다리가 아예 안 움직인다.**
상체만 쓰는 팩이라는 뜻이고, 그래서 **상체만 덧씌우면 그대로 쓸 수 있다.**

⚠ 그냥 `DefaultSlot` 으로 재생하면 **전신을 덮어써서** 앉아 있던 손님이
일어선다. 슬롯을 나눠야 한다.

```
[상태 기계]──────────── Base Pose ──┐
                                    ├─ [Layered blend per bone] ── [Slot 'DefaultSlot'] ── [Output Pose]
[Slot 'UpperBody']───── Blend Poses ┘
```

**만드는 순서**

```
① 몽타주 4개(AM_Eating_*)를 열어 슬롯을 UpperBody 로 바꾼다
     Anim Slot Manager 에서 UpperBody 슬롯을 먼저 만든다
② AnimGraph 에 Slot 'UpperBody' 노드를 하나 추가한다 (입력은 비워 둔다)
③ Layered blend per bone 노드 추가
     Base Pose   ← 상태 기계
     Blend Poses 0 ← Slot 'UpperBody'
     Layer Setup > Branch Filters > [0] Bone Name = spine  · Blend Depth = 1
④ Layered blend 출력 → 기존 Slot 'DefaultSlot' 입력 → Output Pose
```

⚠ **본 이름이 `spine` 이다.** 이 스켈레톤은 Mixamo 인데 트랙 이름에 `mixamorig:`
접두사가 없다 — `hips` · `spine` · `spine1` · `spine2` · `neck` · `head` ·
`rightupleg` 식이다. `spine_01` 을 넣으면 안 걸리고 **조용히 아무 일도 안 한다.**

⚠ `Interact_Reach` 는 전신 동작이라 `DefaultSlot` 에 그대로 둔다. 상체만 쓰면
서 있는 손님이 손만 뻗는 그림이 되는데 그게 맞다 — 굳이 옮기지 않는다.

**⑤ Compile > Save**

### 8-2. 프리필 손님이 4.4초 뒤 일어나 나간다

`PE_Eat` 이 `Sit_Eat` 을 **한 번만** 재생하고 끝나면 `Leave` 로 간다.

```
ST_Customer > PE_Eat
  Play Action 의  Loop ☑                       ← 끝나지 않게
  전이 OnStateCompleted → Leave   삭제          ← 나가지 않게
```

⚠ **둘은 한 쌍이다.** 전이만 지우면 `Play Action` 이 끝났을 때 받아 줄 전이가
없어 **트리가 통째로 끝나고**, `Sit At Seat` 의 `ExitState` 가 손님을 일으켜
세우고 좌석을 반납한다.

### 8-3. ⚠⚠ 액션 이름 오타 둘

```
K_Act    Play Action  Interaction_Reach   →  Interact_Reach
Eating   Play Action  Sit-Eat             →  Sit_Eat
```

`FName` 이라 **조용히 실패한다** — 몽타주를 꽂아도 못 찾고, 증상이 「몽타주가
없는 것」과 완전히 똑같다. 그 상태에서는 애님 없이 시간만 센다.

### 8-4. `Idle_Look` 몽타주가 너무 길다

`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다.** `Sit_Eat` 은 리타겟한
먹는 애님으로 바꿔서 6.9~9.2초로 괜찮아졌지만, `Idle_Look` 은 아직 길다:

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

급하면 `Idle_Look` 에서 긴 변형(`Talking_On_A_Cell_Phone`·`Texting_While_Standing`)
을 빼는 것만으로도 문 앞 대기가 18.5초로 준다.

### 다 하고 나서 — 되읽어 확인

```
ll_st_dump.py    PE_Eat 에 전이가 없고 Play Action 이 (loop) 인가
ll_st_check.py   ❌ 가 0건인가
```

---

## 9. 그 밖에 남은 것

- 좌석 선택이 **결정적**이다 — 늘 같은 순서로 같은 자리부터 찬다.
  다양성이 필요하면 `ClaimSlot(bRandom=true)`.
- 주문번호 표시기 — 재질 `M_OrderDisp` 만 있고 UMG 위젯이 없다.
- 손님끼리 회피 — `UCrowdFollowingComponent`·RVO 둘 다 미적용.
