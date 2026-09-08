# BurgerHouse — 손님 NPC 시스템

별도 프로젝트(`projectll`)에서 만든 **버거 가게 손님 루프**를 Febricula 로 가져온
것이다. 이 문서는 **무엇이 들어왔고 이식하면서 무엇이 바뀌었는지**만 적는다.

---

## 1. 무엇이 들어왔나

```
Source/Febricula/BurgerHouse/     C++ 6쌍
  CustomerCharacter               손님 폰 — 행동·착석·트레이
  CustomerAIController            StateTree 를 들고 있다
  StoreSubsystem                  주문·대기열·슬롯 예약의 **소유자**
  SlotPoint                       좌석 37 · 키오스크 2 · 픽업 2 · 문 2
  BurgerHouseStateTreeTasks       태스크 11 + 조건 2
  BurgerHouseTags                 이벤트 태그 4

Content/BurgerHouse/              에셋 277개
  Maps/L_BurgerHouse              레벨
  AI/BP_Customer · BP_CustomerAI · ST_Customer
  Anims/Mixamo/ABP_Customer + 애님 17
  Props · Materials · Shell · Textures

Import/Scripts/                   파이썬 6개 (아래 §5)
```

### 설계의 요점 하나

**주문 번호는 매장의 상태지 손님의 상태가 아니다.** NPC 가 번호를 들고 있으면
디스플레이가 NPC 를 뒤져야 하고, NPC 가 사라지면 주문이 유령이 된다. 그래서
주문·대기열·슬롯 예약을 `UStoreSubsystem` 이 전부 소유하고 NPC 와 디스플레이는
서로를 모른 채 그것만 본다.

### 손님 루프

```
Enter → Kiosk → Wait → Pickup → Seat → Leave
                 ↑ Order.Ready 이벤트로 전이 (조건 폴링이 아니다)
```

`ST_Customer` 는 상태 32 · 태스크 39 · 전이 29 로 이미 완성돼 있다.

---

## 2. ⚠ 이름이 바뀌었다 — `Burgeria` → `BurgerHouse`

모듈도 바뀌었다 (`projectll` → `Febricula`).

에셋은 타입을 **이름 문자열로** 참조하기 때문에, `Config/DefaultEngine.ini` 의
리다이렉트가 없으면 `ST_Customer` 의 태스크가 **에러 없이 조용히 사라진다.**

```
[CoreRedirects]
  +PackageRedirects   /Script/projectll -> /Script/Febricula
  +StructRedirects    Burgeria* -> BurgerHouse*   구조체 26개 × 2벌
  +EnumRedirects      EBurgeriaTrayOp -> EBurgerHouseTrayOp

[/Script/GameplayTags.GameplayTagsSettings]
  +GameplayTagRedirects   Burgeria.Order.Ready -> BurgerHouse.Order.Ready  등 4개
```

⚠ **구조체 리다이렉트를 두 벌씩 적은 이유** — 패키지 리다이렉트가 먼저 적용되면
조회 이름이 이미 `/Script/Febricula.Burgeria*` 가 되어 있을 수 있다. 둘 다 걸어 둔다.

⚠ 이 리다이렉트는 **지우면 안 된다.** 에셋을 한 번 열어 다시 저장하면 새 이름이
구워지지만, 그 전에 지우면 태스크가 통째로 날아간다.

---

## 3. ⚠ 이식하면서 **뺀 것**

### 히트맵 · 스플라인 스포너 · 혼잡도 이동 (`Heat/`)

원본에는 범용 `Heat/` 층이 있었다. 가져오지 않았다.

| 뺀 것 | 원래 하던 일 | 지금은 |
|---|---|---|
| `UHeatmapData` · `UHeatmapSubsystem` | 텍스처 → 월드 밀도장 | 없음 |
| `ASplineHeatSpawner` | **손님을 낳고 개체 수를 유지** | 없음 |
| `UHeatMovementComponent` | 붐비면 걸음이 느려짐 | 없음 |
| `ISpawnGroupMember` | 스포너가 일행·역할을 통보 | 아래로 대체 |
| 좌석 히트맵 가중 | 인기 있는 자리부터 팔림 | 첫 빈 자리 |

### ⚠⚠ 그래서 **손님을 낳는 코드가 없다**

스포너가 유일한 스폰 경로였다. 지금 상태로 PIE 를 켜면 **손님이 한 명도 안
생긴다.** 낳는 쪽을 붙일 때 아래 둘을 순서대로 불러야 일행·프리필이 동작한다:

```cpp
Customer->OnAssignedSpawnRole(TEXT("Eating"));   // ← 먼저. 프리필일 때만
Customer->OnSpawnedInGroup(GroupId, GroupSize, IndexInGroup);
```

⚠ **순서가 중요하다.** 좌석 배정이 `PartyId` 를 보기 때문에 뒤집으면 일행이
같은 테이블에 안 앉는다. 둘 다 `BlueprintCallable` 이라 BP 에서도 부를 수 있다.

⚠ 안 부르면 **조용히** 전원이 「문으로 막 들어온 혼자 온 손님」이 된다.

### 그 밖에

- 좌석 선택이 **결정적**이다 — `TActorIterator` 순서라 늘 같은 자리부터 찬다.
  다양성이 필요하면 `ClaimSlot(bRandom=true)`.
- 콘솔 명령 `BurgerHouseHeat` 없어짐. 나머지(`BurgerHouseSlots`·`BurgerHouseDump`·
  `BurgerHouseReady`·`BurgerHouseOrder`·`BurgerHouseClaim`·`BurgerHouseReleaseAll`)는 그대로.
- ⚠ `L_BurgerHouse` 에 **`ASplineHeatSpawner` 액터가 놓여 있다.** 클래스가 없으니
  레벨을 처음 열 때 「클래스 없음」으로 뜬다 — 지우면 된다.
- ⚠ `BP_Customer` 에 `HeatMove` 컴포넌트가 남아 있다. 같은 이유로 경고가 뜨고,
  BP 를 한 번 열어 저장하면 사라진다.

---

## 4. ⚠ 팀 공용 설정을 건드린 곳

`Config/DefaultEngine.ini` 의 **내비게이션 에이전트**를 손님 캡슐에 맞췄다.

```
AgentRadius 34 · AgentHeight 176      (ACharacter 기본 캡슐: 반지름 34 · 반높이 88)
```

⚠⚠ **이 프로젝트의 다른 NPC 내비에도 영향을 준다.** Develop 에 머지하기 전에
다른 캐릭터 캡슐과 맞는지 확인할 것.

이 값이 없으면 엔진이 하드코딩된 35/144 로 내비를 굽고, 176 짜리 손님이 144 용
내비 위를 걷는다 — 「내비 설정이 기본값으로 되돌아간다」로 보이는 증상의 정체다.
그리고 **에이전트 크기를 실제로 정하는 건 `SupportedAgents`** 이지 `RecastNavMesh`
블록이 아니다. 되읽어 확인했다.

`Febricula.uproject` 에 `PythonScriptPlugin` · `EditorScriptingUtilities` 를 켰다
(아래 스크립트용).

---

## 5. 스크립트 — 플레이에 관계있는 것만 남겼다

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
| `ll_slots.py` | 좌석 37 · 키오스크 2 · 픽업 2 · 문 2 를 배치한다 (`LL_placement.json` 을 읽는다) |
| `ll_seat_map.py` | 어느 좌석이 어느 테이블인지 — 일행이 같이 앉는지 확인 |
| `ll_nav.py` | NavMesh 바운즈를 깐다 |
| `ll_nav_check.py` | 내비가 실제로 홀을 덮는지 **숫자로** 확인 |
| `ll_st_check.py` | `ST_Customer` · `BP_Customer` · `BP_CustomerAI` 배선 점검 |
| `ll_st_dump.py` | 트리를 통째로 덤프 — **아래 검증에 쓴다** |

레벨 아트용(조명·머티리얼·Nanite·임포트·프로브)은 전부 뺐다.

---

## 6. ⚠ 다음에 할 일 — 순서대로

### ① C++ 빌드

새 소스가 들어왔으므로 Rider/VS 에서 한 번 컴파일해야 에디터가 뜬다.

### ② 리다이렉트가 먹었는지 되읽어 확인

```
ll_st_dump.py  →  상태 32 · 태스크 39 · 전이 29
```

⚠ **숫자가 줄었으면 리다이렉트가 빠진 것이다.** 특히 태스크가 39 보다 적으면
그 태스크는 이미 없어진 것이고, 저장하는 순간 영영 사라진다. 저장하지 말고
`§2` 의 리다이렉트부터 확인할 것.

### ③ 스폰 경로 붙이기

`§3` 참조. 이게 없으면 손님이 안 생긴다.

### ④ 남은 것

- **몽타주 0개** — `BP_Customer > BurgerHouse|Action > Actions` 가 비어 있다.
  비어 있어도 루프는 돈다 (`PlayAction` 이 몽타주가 없으면 시간만 센다).
  눈에 보이는 건 미끄러지는 Idle 포즈뿐이다.
- 주문번호 표시기 UMG — 재질 `M_OrderDisp` 만 있고 위젯이 없다.
- 손님끼리 회피 — `UCrowdFollowingComponent`·RVO 둘 다 미적용.
