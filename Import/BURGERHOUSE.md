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

### 장식 손님의 몸짓 — 한 사람은 **하나만** 돌린다

앉은 장식 손님은 `Sit_Idle` 의 먹는 애님 **4종 중 하나**를 골라 그것만 계속
반복한다 (`bDecorationSingleAction`, 기본 켬).

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
| 리타게터에 op 이 0개 | **안 움직이는 애님**이 나옴 | 「스켈레톤이 틀렸나」 (§7) |
| 에디터를 켠 채 커맨드릿 | 덮어쓰기 대신 **`이름1`** 이 생김 | 「스크립트가 두 번 돌았나」 |

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

---

## 7. 몽타주

`Content/BurgerHouse/Anims/Montages/` 에 11개가 있고 `BP_Customer > Actions` 에
꽂혀 있다. `ll_montages.py` 가 만든다 (다시 만들려면 그 스크립트를 돌린다).

```
슬롯 DefaultSlot (전신) — 전부 여기다
  Idle_Look       AM_Texting_While_Standing 1.0 · AM_Talking_On_A_Cell_Phone 0.5
                  AM_Texting 0.4 · AM_Telling_A_Secret 0.2
  Interact_Reach  AM_Reaching_Out 1.0
  Sit_Idle        AM_Eating_a_Burger 1.0 · AM_Eating_Fries 1.0
                  AM_Eating_a_Drumstick 1.0 · AM_Eating_Frantically 0.6
  Sit_Eat         AM_Eating_* 4개 (전부 1.0 = 균등)
```

`Sit_Idle` 은 **장식 손님이 돌리는 풀**, `Sit_Eat` 은 걸어 들어온 손님이 트리에서
쓰는 것 — **같은 몽타주 에셋**을 공유한다.

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
| `AM_Telling_A_Secret` | 10.90s | 0.6 | 0.0 |
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
