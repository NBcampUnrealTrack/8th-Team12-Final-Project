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

## 7. 아직 안 된 것

- **몽타주 0개.** `BP_Customer > BurgerHouse|Action > Actions` 가 비어 있다.
  비어 있어도 루프는 **정상으로 돈다** — `PlayAction` 이 몽타주가 없으면 시간만
  센다. 눈에 보이는 건 미끄러지는 Idle 포즈뿐이다.
  ⚠ 「동작이 없다」와 「태스크가 안 돌았다」를 헷갈리지 말 것. 후자는 로그에 남는다.
- **`ABP_Customer` 의 Walk 상태가 `Walking_While_Texting` 을 쓴다.** `Walking` 으로
  바꿔야 폰 안 보고 걷는다.
- 좌석 선택이 **결정적**이다 — 늘 같은 순서로 같은 자리부터 찬다.
  다양성이 필요하면 `ClaimSlot(bRandom=true)`.
- 주문번호 표시기 — 재질 `M_OrderDisp` 만 있고 UMG 위젯이 없다.
- 손님끼리 회피 — `UCrowdFollowingComponent`·RVO 둘 다 미적용.
