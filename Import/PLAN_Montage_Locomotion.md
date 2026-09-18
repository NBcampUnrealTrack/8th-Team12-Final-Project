# 계획안 — 손님 로코모션을 AnimSequence → AnimMontage 로

**대상**: `ABP_Customer` 의 `Idle` · `Walk` · `Sit` 세 상태 (BurgerHouse 손님).
다른 NPC(`ABP_Male` 계열)는 범위 밖 — 마지막 절에 확장 메모만 둔다.

> ## 상태: **실행 완료** (PIE 확인만 남음)
>
> §3 의 1~3단계를 다 돌렸고 단계마다 새 프로세스로 검산했다. 결과와 **계획이
> 틀렸던 곳**은 맨 아래 §7 에. 운영 문서는 `BURGERHOUSE.md` §9-2 로 옮겼다.
>
> §3.4(상태 기계 정리)는 **일부러 안 했다** — 그게 되돌리기 장치다.

---

## 0. 먼저 — 이 변경을 권하지 않는다

읽고 나서 그래도 하시겠다면 §3 부터가 실행 계획이다. 다만 **얻는 것과 잃는 것이
분명히 비대칭**이라 그것부터 적는다.

### 얻는 것

| | |
|---|---|
| **데이터로 교체** | 걷기 애님을 바꾸려면 지금은 AnimGraph 를 건드려야 한다. 몽타주면 `ll_montages.py` 한 줄이다 |
| **손님마다 다른 걸음** | `PickStableVariant` 가 이미 좌석 해시로 변형을 뽑는다. 로코모션도 그 위에 올릴 수 있다 |

### 잃는 것

| | |
|---|---|
| **자동 전이 블렌드** | 상태 기계는 규칙 + 0.2초 블렌드를 공짜로 준다. 몽타주는 재생/정지를 손으로 엮어야 한다 |
| **매 프레임 반응** | 상태 기계는 매 틱 규칙을 본다. 몽타주는 **우리 코드가 알아챌 때만** 반응한다 |
| **루프** | 몽타주는 기본이 1회 재생이다. §2 참고 — 파이썬으로는 루프 설정을 **못 건다** |
| **싱크 그룹** | 발 위상 맞추기가 상태 기계 쪽에만 있다 |
| **디버깅** | AnimBP 디버거가 상태를 그림으로 보여 준다. 몽타주 구동은 거기 안 나온다 |

### ⚠⚠ 가장 큰 위험 — 슬롯 경합

몽타주는 **슬롯 하나에 하나만** 재생된다. 지금 `DefaultSlot` 은 행동 몽타주
(먹기·`Idle_Look`·`Interact_Reach`)가 쓰고 있다. 여기에 로코모션까지 얹으면 둘이
서로를 끊는다.

**이건 방금 겪은 그 버그다.** StateTree 와 장식 루프가 `DefaultSlot` 을 두고 싸워서
「앉은 손님이 1초쯤 폰을 본다」가 나왔다 (`BURGERHOUSE.md` §6). 로코모션을 얹으면
같은 슬롯에 **세 번째 구동자**를 의도적으로 추가하는 셈이다.

→ 로코모션 전용 슬롯이 **반드시** 필요하다 (§3.1).

### 원하는 것만 얻는 더 가벼운 길 (권장)

위 두 이득은 **상태 기계를 그대로 두고도** 얻을 수 있다 — 시퀀스 에셋을
변수로 빼면 된다.

```
ABP_Customer 에 WalkAnim · IdleAnim · SitAnim (UAnimSequence*) 변수 추가
   ↓
각 상태의 Sequence Player 의 `Sequence` 핀을 그 변수에 바인딩
   ↓
BP_Customer 가 스폰 때 가중치 목록에서 뽑아 넣어 준다
```

- 데이터로 교체 ✅  · 손님마다 다른 걸음 ✅
- 블렌드 · 루프 · 싱크 그룹 · 디버거 **전부 유지** ✅
- 작업량: AnimGraph 노드 3개 핀 노출 + 변수 3개 + 스폰 시 대입

**같은 목적에 위험이 훨씬 적다.** 이쪽을 먼저 검토해 주시길 권한다.

---

## 1. 지금 상태 (실측)

`ll_abp_structure.py` · `ll_anim_usage.py` 결과.

```
변수   Speed (double) · bIsSitting (bool)

[Locomotion State Machine]
  Idle ← Idle              (AnimSequence,  8.33s)
  Walk ← Walk_Fwd_Manny    (AnimSequence,  1.067s)
  Sit  ← Sitting__1_       (AnimSequence,  5.93s)

  전이 (전부 블렌드 0.200s · priority 1)
    Idle → Walk   Speed > 5
    Walk → Idle   Speed < 5
    Idle → Sit    bIsSitting == true
    Sit  → Idle   bIsSitting == false
```

⚠ 전이 임계값은 **5** 다. `BURGERHOUSE.md` §9 가 10 이라고 적어 놨던 것은
오류였고 고쳤다. 계획을 세울 때 이 값을 그대로 재현해야 한다.

### 슬롯 현황

| 슬롯 | 쓰는 몽타주 |
|---|---|
| `DefaultSlot` | 15개 — 먹기 4 · `Idle_Look` 3 · `Reaching_Out` · `Sitting_*` 등 |
| `UpperBody` | 2개 — `AM_Sit_Texting` · `AM_Sit_Talking_On_A_Cell_Phone` (**현재 미사용**) |

AnimGraph 에 `UpperBody` 슬롯 + Layered blend 가 이미 들어 있다 (§7 에서 상체
분리를 되돌리며 무해하다고 남겨 둔 것). 로코모션 슬롯은 그것과 **별개**로 하나 더
필요하다.

---

## 2. ⚠⚠ 막히는 지점 — 몽타주 루프를 파이썬으로 못 건다

실측: UE 5.8 파이썬에서 `AnimSegment` 에 `loop_count` 프로퍼티가 **노출돼 있지
않다** (`ll_sit_loop_check.py` 에서 확인).

```
AnimSegment: Failed to find property 'loop_count'
```

그래서 로코모션 몽타주 3개의 무한 루프 설정은 **에디터에서 손으로** 해야 한다.
`ll_montages.py` 로 자동화되는 범위 밖이다 — 다시 만들 때마다 손이 간다.

대안으로 C++ 타이머 재재생(`PlayLoopedAction` 방식)이 있지만 로코모션에는
나쁘다. 걷기가 1.067초라 **분당 약 83회** 다시 걸어야 하고, 타이밍이 흔들리면
보폭이 튄다. 장식 몸짓(2.7~44초)과는 성격이 다르다.

→ **에디터 수동 설정을 계획에 포함해야 한다.**

---

## 3. 실행 계획

각 단계는 **검증을 통과해야** 다음으로 간다.

### 3.1 로코모션 슬롯 추가

```
Sequence Player (Idle, 안전망 기본 포즈)
   ↓
Slot 'Locomotion'    ← 새로 추가. 걷기/서기/앉기 몽타주가 여기로
   ↓
Slot 'DefaultSlot'   ← 행동 몽타주가 위를 덮는다 (순서가 중요)
   ↓
Slot 'UpperBody' + Layered blend  (기존)
   ↓
Output Pose
```

- `unreal.AnimGraphService.add_slot_node(ABP, 'Locomotion')` 로 추가하고 재배선
- ⚠ **기본 포즈 소스를 반드시 남긴다.** 아무 몽타주도 안 걸린 순간
  (스폰 직후·전환 틈) 소스 포즈가 없으면 **T포즈가 보인다**
- ⚠ 순서를 뒤집어 `DefaultSlot` 이 아래로 가면 먹는 몽타주가 걷기에 덮인다

**검증** — 컴파일 통과 · `validate_state_machine` · PIE 에서 T포즈 없음

### 3.2 로코모션 몽타주 만들기

`ll_montages.py` 에 슬롯 인자를 쓴 항목 추가 (`make_montage` 가 이미 슬롯을 받는다):

```python
LOCO = 'Locomotion'
ACTIONS['Loco_Idle'] = [('Idle',           1.0, 'AM_Loco_Idle', LOCO)]
ACTIONS['Loco_Walk'] = [('Walk_Fwd_Manny', 1.0, 'AM_Loco_Walk', LOCO)]
ACTIONS['Loco_Sit']  = [('Sitting__1_',    1.0, 'AM_Loco_Sit',  LOCO)]
```

- ⚠ **BlendIn/Out 을 0.2 로** 맞춘다 — 현재 전이 블렌드와 같게. `ll_montages.py`
  의 `BLEND` 는 0.35(장식용)이므로 **로코모션만 따로** 줘야 한다
- ⚠ 그다음 **에디터에서 LoopCount = 0** (§2). 파이썬으로는 안 된다

**검증** — 슬롯이 `Locomotion` 인가 · 블렌드 0.2 인가 · 스켈레톤이 맞는가
(`ll_sit_verify.py` 와 같은 형태의 검사 스크립트 하나 추가)

### 3.3 구동 코드

`ACustomerCharacter` 에 로코모션 상태 추적 추가.

```
Tick (또는 이동/착석 이벤트) 에서
   원하는 상태 = bIsSitting ? Loco_Sit : (Speed > 5 ? Loco_Walk : Loco_Idle)
   현재 상태와 다르면 -> 그 몽타주를 Locomotion 슬롯에 재생
```

- ⚠ **매 틱 재생하면 안 된다.** 바뀔 때만 건다. 안 그러면 매 프레임 블렌드인이
  다시 시작해 애님이 얼어붙는다
- ⚠ 임계값 5 에 **히스테리시스**를 넣을 것. 정확히 5 근처에서 Speed 가 떨리면
  Idle↔Walk 가 초당 수십 번 뒤집힌다. 상태 기계는 전이 블렌드가 이걸 덮어 줬다
- ⚠ `StopActionLoop` 과 **섞이지 않게** 한다 — 슬롯이 달라 공존하지만, 손님이
  사라질 때 로코모션 몽타주도 같이 정리해야 한다

**검증** — PIE 에서 걷기→정지→착석 전환. 특히 **떨림**과 **얼어붙음**

### 3.4 상태 기계 정리

- 3.3 이 PIE 에서 확인된 **뒤에** 상태 기계를 기본 포즈 하나로 줄인다
- ⚠ **그 전에 지우지 말 것.** 되돌릴 곳이 없어진다

### 3.5 최종 검증

| | |
|---|---|
| 새 프로세스 스크립트 | 슬롯 구성 · 몽타주 슬롯/블렌드 · Actions 배선 |
| **PIE 관찰** (숫자로 못 잡음) | 걷다 서는 순간 미끄러짐 · 착석 전환 · 먹는 몽타주가 걷기를 제대로 덮는가 · 손님 5명 동시 |

---

## 4. 되돌리기

바뀌는 것은 4개뿐이라 되돌리기는 쉽다.

```
Content/BurgerHouse/Anims/Mixamo/ABP_Customer.uasset    (그래프)
Content/BurgerHouse/AI/BP_Customer.uasset               (Actions)
Content/BurgerHouse/Anims/Montages/AM_Loco_*.uasset     (신규 3개)
Import/Scripts/ll_montages.py
Source/Febricula/BurgerHouse/CustomerCharacter.{h,cpp}
```

⚠ `.uasset` 저장은 §6 의 두 함정을 지킨다 — **컴파일보다 먼저 저장**하고,
**검산은 새 프로세스**에서.

---

## 5. 다른 NPC 로 넓힌다면

`ABP_Male` · `ABP_Suah` · `ABP_CheonYul` · `ABP_Unarmed` 계열은 사정이 다르다.

- 로코모션이 **블렌드스페이스**(`BS_Idle_Walk_Run_*`)다. 속도에 따라 보폭이
  연속으로 변한다 — **몽타주로는 이걸 재현할 수 없다.** 단일 시퀀스 3~4개로
  쪼개면 표현력이 확실히 떨어진다
- 점프·낙하·착지는 단발이라 몽타주가 어울리지만, 지금도 상태 기계가 잘 한다

→ **권하지 않는다.** 손님이 몽타주 후보가 된 유일한 이유는 애초에
블렌드스페이스가 없어서다.

---

## 7. 실행 결과 — **계획이 틀렸던 곳**

계획대로 안 된 것만 적는다. 나머지는 §3 그대로다.

### ① §2 「몽타주 루프를 파이썬으로 못 건다」 — **틀렸다**

프로퍼티 이름을 `loop_count` 로 찾아서 「없다」고 결론 냈는데, 실제 이름은
**`looping_count`** 이고 멀쩡히 노출돼 있다.

그런데 그것도 답이 아니었다 — `LoopingCount` 는 **세그먼트 반복 횟수**라 0 은
무한이 아니라 **길이 0** 이다. 진짜 답은 런타임의
`Montage_SetNextSection(섹션, 같은 섹션)` 이었다.

→ 결과적으로 계획이 걱정한 **에디터 수동 작업이 아예 없어졌다.**

⚠ 교훈: 「프로퍼티가 없다」는 결론은 **이름을 두세 개 더 시도해 보고** 내릴 것.

### ② 슬롯 이름을 `Locomotion` → `LocoSlot` 으로

상태 기계 이름도 `Locomotion`, 그 그래프 이름도 `Locomotion` 이라 로그가
「셋 중 어느 것인가」가 된다.

### ③ 계획에 없던 것 — **틱을 켜야 했다**

`ACustomerCharacter` 가 `bCanEverTick = false` 인 걸 계획 단계에서 못 봤다.
걷기 시작·정지는 이벤트가 없어 속도를 폴링해야 한다. `TickInterval = 1/30` 로
낮춰서 넣었다. 계획 §0 의 「매 프레임 반응을 잃는다」가 **구체적인 비용으로
드러난 지점**이다.

### ④ 계획에 없던 것 — `describe_graph` 는 튜플을 준다

`get_editor_property('status')` 가 AttributeError 다. 문자열로 만들어 잘라 쓴다.

### 실제로 바뀐 것

```
Content/BurgerHouse/Anims/Mixamo/ABP_Customer.uasset     Slot 'LocoSlot' 삽입
Content/BurgerHouse/AI/BP_Customer.uasset                Actions 6 -> 9개
Content/BurgerHouse/Anims/Montages/AM_Loco_{Idle,Walk,Sit}.uasset   신규
Import/Scripts/ll_montages.py                            BLEND_BY_SLOT · LOCO 항목
Source/Febricula/BurgerHouse/CustomerCharacter.{h,cpp}   틱 + 구동 코드
```

빌드: `FebriculaEditor Win64 Development` — 47초, 성공.

---

## 6. 결정이 필요한 것

1. **정말 몽타주로 갈지**, 아니면 §0 의 「시퀀스를 변수로」 쪽으로 갈지
2. 간다면 범위 — 손님만인지, 다른 NPC 도인지 (§5 는 반대)
3. 로코모션 몽타주에 **손님별 변형**을 넣을지 (넣으면 `Loco_Walk` 에 여러 줄)
