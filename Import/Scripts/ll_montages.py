"""손님 몽타주를 만들고 `BP_Customer > Actions` 에 꽂는다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_montages.py" -unattended -nopause -nosplash

## 왜 몽타주여야 하나

`ACustomerCharacter::PlayAction` 이 `Montage_Play` 를 쓴다. 애님 시퀀스를 그냥
꽂을 수는 없다. 그리고 몽타주가 **`DefaultSlot`** 을 써야 `ABP_Customer` 의
`Slot 'DefaultSlot'` 노드를 타고 화면에 나온다 — 팩토리가 기본으로 그렇게 만든다.

⚠ **슬롯 노드가 없으면 조용히 실패한다.** `Montage_Play` 는 길이를 정상으로
  돌려주고 `PlayAction` 도 성공했다고 보고하는데 화면에만 아무 일이 없다.

## ⚠ 길이를 자르는 이유

`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다.** 원본 애님이 44초짜리도
있어서 그대로 쓰면 손님이 문 앞에서 40초를 서 있는다. 그래서 여기서 자른다.

⚠ 자른 구간의 첫·끝 포즈가 많이 다르면 반복마다 툭 튄다. 눈으로 보고 `TRIM` 을
  조정할 것.

## ⚠ 액션 이름은 `ST_Customer` 와 **철자까지** 같아야 한다

`FName` 이라 오타가 조용히 실패하고 **「몽타주가 없는 것」과 구별이 안 된다.**
아래 `ACTIONS` 의 키는 트리가 실제로 쓰는 이름에서 뽑았다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

# ⚠⚠ 2026-09-16: 손님이 **마네퀸**으로 바뀌면서 여기를 옮겼다 (§9-19).
#
# 예전 값은 Mixamo 였다 —
#     ANIM_DIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
#     MTG_DIR  = '/Game/BurgerHouse/Anims/Montages'
#
# ⚠ 몽타주는 **스켈레톤에 묶인다.** 메시를 바꾸면 몽타주를 그 스켈레톤 위에서
#   **다시 만들어야** 한다 — 일괄 리타겟이 만들어 준 몽타주는 스켈레톤만 바뀌고
#   **안에 물린 시퀀스는 옛것 그대로**였다 (실측). 그래서 여기서 다시 만든다.
ANIM_DIR = '/Game/BurgerHouse/Anims/Manny'
MTG_DIR = '/Game/BurgerHouse/Anims/Manny/Montages'
BP_PATH = '/Game/BurgerHouse/AI/BP_Customer'

# 액션 이름 -> [(애님, 가중치, 자를 길이(초). None 이면 원본 그대로)]
#
# 가중치는 **합이 1일 필요 없다.** 기본 동작을 1.0, 가끔 나오는 몸짓을 0.2 로
# 두면 대개 평범하고 가끔 눈에 띄는 그림이 된다.
# 액션 -> [(애님, 가중치, 몽타주 이름, 슬롯)]
#
# ## ⚠⚠ 슬롯은 **하나**다 — 상체 분리를 되돌렸다
#
# 한동안 먹는 애님을 `UpperBody` 슬롯에 넣고 하체는 AnimBP 의 Sit 상태로
# 덮었다. **전제가 틀렸다.**
#
#   「먹는 애님은 서서 먹는 동작이다」 ← 틀림
#   원본(마네퀸)의 골반 Z 가 **57.4** 다. 앉음 57~63 · 섬 103 이므로
#   **원래부터 앉아서 먹는 동작**이다.
#
# 그 오판은 리타겟 결과가 **빈 애님**이던 시절에 내린 것이다. 움직임이 0이라
# 자세를 확인할 방법이 없었다.
#
# ⚠ 상체만 덧씌우면 상체는 애님의 좌면 높이, 하체는 Sit 상태의 높이를 따라
#   **상체가 공중에 뜬다.** 실제로 그렇게 보였다.
#
# → 앉은 동작은 앉은 동작 그대로 **전신(`DefaultSlot`)** 으로 튼다.
DEFAULT = 'DefaultSlot'
UPPER = 'UpperBody'   # 지금은 안 쓴다. 서서 하는 몸짓을 앉은 손님에게 얹을
                      # 일이 생기면 그때 쓸 것 (AnimBP 의 상체 레이어 필요).

# 로코모션 전용 슬롯 — `ll_loco_slot.py` 가 AnimGraph 에 끼워 넣은 것.
#
#     [Locomotion SM] → Slot 'LocoSlot' → LocoCache → … → Slot 'DefaultSlot' → Output
#
# ⚠ `DefaultSlot` 보다 **앞**이라 행동 몽타주(먹기 등)가 로코모션을 덮는다.
#   그 순서가 뒤집히면 걷기가 먹는 동작을 지운다.
LOCO = 'LocoSlot'

ACTIONS = {
    # 서 있는 손님 — 문 앞·줄에서
    #
    # ## ⚠⚠ **길이가 곧 대기 시간이다**
    #
    # `Play Action` 은 **몽타주 길이만큼 그 상태에 머문다.** 트리의 `Enter` 는
    # `Idle_Look` 을 **한 번** 재생하고 끝나므로, 긴 변형이 걸리면 손님이 문
    # 앞에서 그만큼 서 있다가 키오스크로 간다.
    #
    # 예전 구성은 최대 **37.2초**였다 (`Talking_On_A_Cell_Phone`). PIE 를 켜면
    # 문 앞에 폰 보는 사람이 30초 넘게 서 있어서 「프리필 NPC 가 폰을 본다」로
    # 오해하기 딱 좋았다 — 실제로는 방금 들어온 워크인이다.
    #
    #     예전  최대 37.2s · 기대값 24.7s
    #     지금  최대 18.5s · 기대값 10.8s
    #
    # ⚠ 긴 둘(`Talking_On_A_Cell_Phone` 37.2s · `Texting_While_Standing` 23.6s)은
    #   **뺐다.** 에셋은 남아 있으니 몽타주 구간을 5~8초로 자르면 되살릴 것.
    #
    # ⚠ `Telling_A_Secret` 은 **뺐다** — 몸을 옆으로 기울여 귓속말하는 동작이라
    #   줄에 선 손님이 엉뚱한 데를 보는 것처럼 보였다. 에셋도 지웠다.
    #
    # ⚠ `Idle` 은 첫·끝 프레임 차이가 **0.0** 인 진짜 순환 동작이라 줄에서
    #   루프로 돌 때 이음매가 없다. 그래서 가장 큰 가중치를 준다.
    #
    # ## 서서 폰 보기를 **반반**으로
    #
    # `Texting_While_Standing` 을 나머지 **전부와 같은 가중치**로 준다 —
    # 1.0 + 0.3 = 1.3 이므로 1.3 을 주면 정확히 50% 다.
    #
    # ⚠ 한때 이것을 **뺐던** 이유는 23.6초라 손님이 문 앞에 오래 서 있어서였다.
    #   그때는 `Idle_Look` 이 `Enter` 에서도 돌았다. 지금은 `Wait` 에서만 돌고,
    #   그 상태는 `Order.Ready` **이벤트가 빼내므로** 길이가 체류 시간을 안 정한다.
    #
    # ⚠ `Idle_Look` 을 쓰는 곳은 `W_Idle`·`PW_Idle` 둘뿐이다 (둘 다 주문 뒤 대기).
    #   `DecorationActions` 의 `Ordering`/`Waiting` 도 이 이름을 쓰지만 지금
    #   `PrefillPlacements` 는 전부 `Eating` 이라 안 돈다.
    'Idle_Look': [
        ('Idle', 1.0, 'AM_Idle', DEFAULT),                          # 8.33s
        ('Texting', 0.3, 'AM_Texting', DEFAULT),                    # 18.53s
        ('Texting_While_Standing', 1.3,
         'AM_Texting_While_Standing', DEFAULT),                     # 23.6s · 여기서 50%
        # ⚠ 이 변형이 돌 때만 손에 폰이 보인다 (`PHONE_MONTAGES`).
        ('Talking_On_A_Cell_Phone', 0.8,
         'AM_Talking_On_A_Cell_Phone', DEFAULT),                    # 37.2s
    ],
    'Interact_Reach': [
        ('Reaching_Out', 1.0, 'AM_Reaching_Out', DEFAULT),
    ],

    # 앉은 **장식 손님**(= 프리필)이 끝없이 돌릴 몸짓 — **버거집 손님답게.**
    #
    # ## 어떤 그림을 노리나
    #
    # 버거집에 앉아 있는 사람들이다. 대다수는 **먹고 있어야** 하고, 일부는
    # 일행과 얘기하고, 한둘은 그냥 앉아 있다. 가중치 합 4.9 중 먹는 것이
    # 3.6 (73%) 이라 홀을 보면 「밥 먹는 가게」로 읽힌다.
    #
    # ⚠ 한 사람은 **하나만** 평생 반복한다 (`bDecorationSingleAction`). 다양성은
    #   한 사람 안이 아니라 **손님들 사이에서** 나온다 — 어느 것을 받을지는
    #   좌석 번호 해시로 결정적으로 뽑으므로 같은 의자는 늘 같은 몸짓이고
    #   옆자리와는 겹치지 않는다 (`DecorationSeed` 로 다시 섞는다).
    #
    # ## ⚠⚠ 후보를 고르는 기준은 **골반 높이**다 — 이름이 아니다
    #
    # 이름에 `Sitting` 이 들어가도 실제로는 서 있는 동작인 것이 섞여 있다.
    # `ll_sit_candidates.py` 로 전부 재서 골랐다 (기준: 먹는 애님 57.4~57.6).
    #
    # | 후보 | 골반 Z | 판정 |
    # |---|---|---|
    # | `Eating_*` 4종 | 57.4~57.6 | 기준 |
    # | `Sitting_Talking` | 57.5 | **쓴다** — 앉아서 대화, 44s 라 변화가 풍부 |
    # | `SitIdle_Male` | 57.9 | **쓴다** — 조용히 앉아 있음 |
    # | `Sitting_Thumbs_Up` | 76.2 | 뺐다 — 19cm. 앉은 동작이 아니다 |
    # | `Sitting_Yell` | 61.7 | 뺐다 — 4cm 뜨고 동작도 과하다 |
    # | `Sitting`·`Texting`·`Idle` | 101~103 | **서 있는 동작** |
    #
    # ⚠⚠ 「섬」인 것을 전신으로 틀면 **앉은 손님이 일어선다** (§7). 되살리려면
    #   AnimBP 의 상체 레이어가 필요하다 — 슬롯만 바꿔서는 안 된다.
    #
    # ⚠ 높이가 ±2cm 를 넘으면 의자에 뜨거나 파묻힌다. `ll_sit_verify.py` 가
    #   이 풀 전체를 재서 벗어나면 실패로 잡는다.
    #
    # ⚠⚠ **이 항목은 프리필 전용이다.** `ST_Customer` 는 `Sit_Idle` 을 쓰지
    #   않는다 (트리가 쓰는 것은 `Sit_Eat` · `Sit-Eat`) — `ll_st_dump.py` 로
    #   확인했다. 걸어 들어온 손님 쪽은 아래 `Sit_Eat` 이 그대로 맡는다.
    'Sit_Idle': [
        # 먹는 중 — 가게의 본체
        ('Eating_a_Burger', 1.0, 'AM_Eating_a_Burger', DEFAULT),        # 7.83s
        ('Eating_Fries', 1.0, 'AM_Eating_Fries', DEFAULT),              # 9.20s
        ('Eating_a_Drumstick', 1.0, 'AM_Eating_a_Drumstick', DEFAULT),  # 8.03s
        # ⚠ 과장된 동작이라 낮게. 흔해지면 홀이 소란스러워 보인다.
        ('Eating_Frantically', 0.6, 'AM_Eating_Frantically', DEFAULT),  # 6.87s

        # 일행과 대화 — `PrefillPlacements` 가 3인석·2인석 두 테이블이라 말이 된다
        ('Sitting_Talking', 0.8, 'AM_Sitting_Talking', DEFAULT),        # 44.07s
        # 그냥 앉아 있음 — 다 먹고 쉬는 사람
        ('SitIdle_Male', 0.5, 'AM_SitIdle_Male', DEFAULT),              # 2.70s
    ],

    # 걸어 들어온 손님이 앉아서 먹는 것 — 트리의 `Sit_Eat` 이 쓴다.
    # ⚠ 위와 **같은 몽타주 에셋**이다. 슬롯이 하나가 되면서 두 벌 만들 이유가
    #   없어졌다 (예전에는 서 있는 손님용/앉은 손님용으로 갈라져 있었다).
    'Sit_Eat': [
        ('Eating_a_Burger', 1.0, 'AM_Eating_a_Burger', DEFAULT),
        ('Eating_a_Drumstick', 1.0, 'AM_Eating_a_Drumstick', DEFAULT),
        ('Eating_Frantically', 1.0, 'AM_Eating_Frantically', DEFAULT),
        ('Eating_Fries', 1.0, 'AM_Eating_Fries', DEFAULT),
    ],
}

# ---- ⚠ 트리 오타 별칭 ----
#
# `ST_Customer` 에 **같은 행동인데 철자가 다른 태스크가 섞여 있다.**
#
#     Sit_Eat            1곳  ·  Sit-Eat            1곳   ← 하이픈
#     Interact_Reach     2곳  ·  Interaction_Reach  1곳
#
# `FName` 이라 오타 쪽은 조용히 실패한다 — `PlayAction` 이 폴백 4초를 돌려주고
# **손님이 4초만 앉았다가 일어선다.** 「중간에 일어난다」의 정체가 이것이다.
#
# 트리 태스크의 파라미터는 `InstancedStruct` 안에 있어 UE 5.8 파이썬으로는
# 못 고친다. 그래서 **오타 이름도 같은 몽타주로 등록해** 막아 둔다.
#
# ⚠ 에디터에서 트리의 철자를 고치면 **이 두 줄을 지울 것.** 안 지우면 쓰이지
#   않는 항목이 남아 다음 사람이 「어느 쪽이 진짜냐」로 헤맨다.
ACTIONS['Sit-Eat'] = ACTIONS['Sit_Eat']
ACTIONS['Interaction_Reach'] = ACTIONS['Interact_Reach']


# ---- 로코모션 (2단계) ----
#
# 상태 기계가 재생하던 세 시퀀스를 **같은 에셋 그대로** 몽타주로 감싼 것이다.
# 슬롯이 `LocoSlot` 이라 행동 몽타주와 싸우지 않는다.
#
# ## ⚠⚠ 무한 루프는 **에셋이 아니라 런타임**에서 건다
#
# 몽타주는 기본이 1회 재생이다. 처음에는 세그먼트의 반복 횟수를 0 으로 두려
# 했는데 — `FAnimSegment` 의 그 값은 `looping_count` 라는 이름으로 노출돼 있고
# (`loop_count` 가 아니다), **0 은 무한이 아니라 길이 0** 이다. 그대로 두면
# 몽타주 길이가 0 이 되어 아무것도 안 나온다.
#
# → 재생할 때 `Montage_SetNextSection(섹션, 같은 섹션)` 으로 자기 자신을 가리키게
#   한다. 끊김 없이 영원히 돈다. 에셋은 손댈 것이 없고, 장식 루프처럼 타이머로
#   다시 걸 필요도 없다 (걷기가 1.067초라 분당 83회를 다시 걸어야 했을 것이다).
#
# ⚠ 섹션 이름은 팩토리가 만드는 기본값이다. 3단계 C++ 가 `GetSectionName(0)` 으로
#   읽어 쓴다 — 이름을 박아 넣지 말 것.
# ## 손에 소품을 들리는 몽타주
#
# `FCustomerActionVariant::bShowPhone` 을 켠다. **몽타주가 도는 동안만** 보인다 —
# 행동이 끝났는지로 판단하면 대기 상태에서 폰이 빈손에 떠 있게 된다.
#
# ⚠ 실측: `Talking_On_A_Cell_Phone` 에서 귀로 가는 것은 **오른손**이다
#   (`hand_r` 가 머리까지 26.5cm · `hand_l` 은 40.5cm).
PHONE_MONTAGES = {'AM_Talking_On_A_Cell_Phone'}

ACTIONS['Loco_Idle'] = [('Idle', 1.0, 'AM_Loco_Idle', LOCO)]            # 8.33s
ACTIONS['Loco_Walk'] = [('Walk_Fwd_Manny', 1.0, 'AM_Loco_Walk', LOCO)]  # 1.067s
# ⚠⚠ 2026-09-17: `Sitting__1_` 을 지우면서 **먹는 애님**으로 옮겼다 (§9-35).
#
# ## 왜 먹는 애님인가 — 골반 높이가 **맞아야** 한다
#
# `Loco_Sit` 은 `LocoSlot` 에 깔리는 **기준 자세**다. 그 위에 `DefaultSlot` 의
# 행동 몽타주(먹기)가 얹힌다. 먹기가 끝나 슬롯이 비면 이 자세가 드러난다.
#
# 골반 높이를 재 보면 —
#
# ```
# Eating_a_Burger    골반 53.3 · 발 5.7      <- 행동 몽타주들
# Eating_Fries       골반 52.8 · 발 5.6
# Eating_a_Drumstick 골반 52.8 · 발 5.7
# Eating_Frantically 골반 52.8 · 발 5.7
# SitIdle_Male       골반 52.8 · 발 2.2      골반은 맞고 발이 3.5cm 낮다
# Sitting__1_        골반 58.5 · 발 12.6     <- **5.7cm 높다** (지운 것)
# ```
#
# ⚠⚠ `Sitting__1_` 은 먹는 애님보다 골반이 5.7cm 높았다. 즉 **한 입이 끝날
#   때마다 손님이 5.7cm 튀어 올랐다.** 기준 자세를 행동과 같은 계열로 두면
#   그 문제가 통째로 사라진다.
#
# ⚠ `Sitting` 은 이름과 달리 골반이 57.5~94.5 로 **앉았다 일어나는** 동작이다.
#   이름으로 고르면 안 된다.
ACTIONS['Loco_Sit'] = [('Eating_a_Burger', 1.0, 'AM_Loco_Sit', LOCO)]  # 7.83s

ACTIONS['Loco_CarryWalk'] = [('Carrying', 1.0, 'AM_Loco_CarryWalk', LOCO)]  # 10.0s
ACTIONS['Loco_CarryIdle'] = [('Idle', 1.0, 'AM_Loco_CarryIdle', LOCO)]      # 8.33s


def set_slot(m, slot):
    """몽타주 슬롯을 바꾼다.

    ⚠⚠ **배열 원소는 복사본으로 돌아온다.** 꺼내 고친 뒤 **배열에 되넣어야**
       반영된다. 안 되넣으면 `set_editor_property` 가 예외도 안 내고 그냥
       무시한다 — 「파이썬으로는 슬롯을 못 바꾼다」로 오진하기 딱 좋다.
    """
    tr = m.get_editor_property('slot_anim_tracks')
    if not tr or str(tr[0].get_editor_property('slot_name')) == slot:
        return
    s = tr[0]
    s.set_editor_property('slot_name', slot)
    tr[0] = s                                   # ← 이 줄이 핵심
    m.set_editor_property('slot_anim_tracks', tr)


# 몽타주 블렌드 시간(초).
#
# ⚠ 이 값이 **루프 이음매를 덮는 크로스페이드 길이**다. 장식 손님은 몽타주가
#   끝나기 전에 다음 것을 겹쳐 거는데(`ACustomerCharacter::DecorationLoopCrossfade`),
#   그때 실제로 섞이는 시간은 **들어오는 몽타주의 Blend In** 이 정한다.
#
# 먹는 애님은 첫·끝 자세가 팔뚝 기준 최대 68도 어긋난다. 0.35초에 걸치면
# 초당 195도 — 사람 팔이 그 정도로 움직이므로 튀는 게 아니라 동작으로 보인다.
#
# ⚠ 너무 길면 두 자세가 오래 섞여 흐물거린다. 0.25~0.4 안에서 조정할 것.
BLEND = 0.35

# ⚠⚠ **로코모션은 0.35 가 아니라 0.20 이다.**
#
# 0.35 는 장식 몸짓의 **루프 이음매를 덮는** 값이다. 로코모션은 목적이 다르다 —
# 상태 기계가 쓰던 **전이 블렌드 0.200s** 를 그대로 재현해야 한다
# (`ll_abp_structure.py` 실측: Idle↔Walk·Idle↔Sit 전부 0.200).
#
# 여기가 어긋나면 걷다 서는 순간의 느낌이 지금과 달라진다 — 숫자로는 안 잡히고
# 눈으로만 보인다.
BLEND_BY_SLOT = {
    'DefaultSlot': BLEND,
    'UpperBody': BLEND,
    'LocoSlot': 0.20,
}


def set_blend(m, slot=None):
    """블렌드 시간을 맞춘다. ⚠ 배열이 아니라 구조체라 되넣을 필요는 없지만,
    `get_editor_property` 가 **복사본**을 주므로 고친 뒤 다시 넣어야 한다."""
    t = BLEND_BY_SLOT.get(slot, BLEND)
    for prop in ('blend_in', 'blend_out'):
        b = m.get_editor_property(prop)
        b.set_editor_property('blend_time', t)
        m.set_editor_property(prop, b)


def make_montage(seq_name, mtg_name, slot):
    """시퀀스 하나로 몽타주 하나. 이미 있으면 슬롯·블렌드만 맞춘다."""
    seq = EAL.load_asset('%s/%s' % (ANIM_DIR, seq_name))
    if not seq:
        E('  %s — 시퀀스가 없다' % seq_name)
        return None

    path = '%s/%s' % (MTG_DIR, mtg_name)

    # ## ⚠⚠ 소스 시퀀스가 **바뀌었으면 다시 만든다**
    #
    # 예전에는 「있으면 그대로 두고 슬롯·블렌드만 맞춘다」였다. 그래서 위
    # `ACTIONS` 의 시퀀스를 바꿔도 몽타주는 **옛 시퀀스를 그대로 물고** 있었고,
    # 로그에는 옛 길이가 찍혔다 — `Loco_Sit` 을 `Sitting__1_`(5.93s) 에서
    # `SitIdle_Male`(2.70s) 로 옮길 때 실제로 그랬다.
    #
    # ⚠ 길이로 비교하면 안 된다 (다른 시퀀스가 같은 길이일 수 있다). **의존성**에
    #   그 시퀀스가 들어 있는지로 본다.
    if EAL.does_asset_exist(path):
        AR = unreal.AssetRegistryHelpers.get_asset_registry()
        deps = AR.get_dependencies(
            unreal.Name(path),
            unreal.AssetRegistryDependencyOptions(include_hard_package_references=True)) or []
        want = '%s/%s' % (ANIM_DIR, seq_name)
        if want not in [str(d) for d in deps]:
            L('  %s — 소스가 바뀌었다. 다시 만든다 (-> %s)' % (mtg_name, seq_name))
            EAL.delete_asset(path)

    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
    else:
        fac = unreal.AnimMontageFactory()
        fac.set_editor_property('target_skeleton', seq.get_editor_property('skeleton'))
        fac.set_editor_property('source_animation', seq)
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            mtg_name, MTG_DIR, unreal.AnimMontage, fac)
        if not m:
            E('  %s — 몽타주 생성 실패' % mtg_name)
            return None

    set_slot(m, slot)
    set_blend(m, slot)

    # ## ⚠⚠ 정말 그 시퀀스를 물었는지 **되읽어** 확인한다
    #
    # 같은 프로세스에서 `delete_asset` 은 **이미 로드된 에셋을 못 지운다** —
    # `True` 를 돌려주고도 `does_asset_exist` 가 계속 참이라, 위에서 옛 몽타주를
    # 그대로 다시 불러온다. 로그에는 옛 길이가 찍히는데 아무도 안 본다.
    #
    # → 그때는 **에디터를 껐다 켜고(= 새 프로세스로) 다시 돌려야** 한다.
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    deps = [str(d) for d in (AR.get_dependencies(
        unreal.Name(path),
        unreal.AssetRegistryDependencyOptions(include_hard_package_references=True)) or [])]
    want = '%s/%s' % (ANIM_DIR, seq_name)
    # ⚠ 방금 만든 에셋은 **의존성 색인이 아직 비어 있다.** 비었으면 길이로 본다.
    if deps and want not in deps:
        E('  ⚠⚠ %s 가 %s 를 안 물었다 — 물고 있는 것: %s'
          % (mtg_name, seq_name, [d.split('/')[-1] for d in deps if '/Anims/' in d]))
        E('     같은 프로세스에서는 못 고친다. **새 프로세스로 다시 돌릴 것.**')
    elif abs(m.get_play_length() - seq.get_play_length()) > 0.05:
        E('  ⚠⚠ %s 길이 %.2fs 가 %s(%.2fs) 와 다르다 — 옛 소스를 물고 있다'
          % (mtg_name, m.get_play_length(), seq_name, seq.get_play_length()))
    return m


def main():
    # 시험용 찌꺼기 청소
    for junk in ('%s/AM_TEST_Reaching_Out' % MTG_DIR,):
        if EAL.does_asset_exist(junk):
            EAL.delete_asset(junk)
            L('시험 에셋 삭제: %s' % junk)

    made = {}
    L('=== 몽타주 만들기 ===')
    for action, variants in ACTIONS.items():
        out = []
        for seq_name, weight, mtg_name, slot in variants:
            m = make_montage(seq_name, mtg_name, slot)
            if not m:
                continue
            EAL.save_asset(m.get_path_name().split('.')[0], only_if_is_dirty=False)
            L('  %-16s %-32s %5.2fs w=%.1f  slot=%s' % (
                action, mtg_name, m.get_editor_property('sequence_length'),
                weight, m.get_editor_property('slot_anim_tracks')[0]
                        .get_editor_property('slot_name')))
            out.append((m, weight))
        made[action] = out

    # ---- BP_Customer 의 Actions 맵에 꽂는다 ----
    L('')
    L('=== BP_Customer > Actions 채우기 ===')
    gc = unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH)
    cdo = unreal.get_default_object(gc)

    actions = unreal.Map(unreal.Name, unreal.CustomerAction)
    for action, pairs in made.items():
        variants = unreal.Array(unreal.CustomerActionVariant)
        for m, w in pairs:
            v = unreal.CustomerActionVariant()
            v.set_editor_property('montage', m)
            v.set_editor_property('weight', w)
            # ⚠ 이름으로 켠다 — 몽타주 에셋 자체는 소품을 모른다.
            # ⚠⚠ 파이썬은 bool `UPROPERTY` 의 앞 `b` 를 **뗀다** — `bShowPhone`
            #    은 `show_phone` 이다. `b_show_phone` 으로 쓰면 예외가 난다.
            #    `try/except` 로 감싸면 그 예외가 **조용히 삼켜져** 플래그가
            #    영원히 꺼진 채로 남는다 — 실제로 한 번 그랬다. 감싸지 말 것.
            v.set_editor_property('show_phone', m.get_name() in PHONE_MONTAGES)
            variants.append(v)
        ca = unreal.CustomerAction()
        ca.set_editor_property('variants', variants)
        actions[unreal.Name(action)] = ca

    cdo.set_editor_property('actions', actions)

    # ---- 저장 ----
    #
    # ## ⚠⚠ **`compile_blueprint` 뒤에는 저장이 안 된다** — 컴파일보다 먼저 쓴다
    #
    # 예전에는 `compile_blueprint()` 다음에 `save_asset()` 을 불렀다. 그런데
    # 컴파일을 거치면 **모든 저장 경로가 `False` 를 돌려주고 파일이 안 써진다.**
    #
    #     compile 뒤:  save_asset False · save_packages False · save_loaded_asset False
    #     compile 없이: 전부 True (실측)
    #
    # ⚠⚠ 그런데 **같은 프로세스의 되읽기는 통과한다.** 메모리의 CDO 는 제대로
    #   바뀌어 있기 때문이다. 그래서 로그만 보면 완전히 성공한 것처럼 보인다.
    #   실제로 한 번 당했다 — 몽타주 6개는 새로 써졌는데 `BP_Customer.uasset`
    #   만 13분 전 실행 그대로였고, 파일 수정 시각을 보고서야 알았다.
    #
    # `Actions` 는 CDO 의 기본값이라 **컴파일 없이 저장해도 반영된다.** 컴파일은
    # 그 뒤에 해서 에디터에 올라와 있는 인스턴스만 갱신한다.
    bp = EAL.load_asset(BP_PATH)
    pkg = bp.get_outermost()

    ok = unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)
    L('')
    L('  저장(컴파일 전) save_packages -> %s' % ok)
    if not ok:
        E('  ⚠⚠ BP_Customer 가 **디스크에 안 써졌다.** 아래 되읽기는 메모리를')
        E('     보는 것이라 통과해도 소용없다 — 새 프로세스로 검산할 것.')

    # ⚠ 컴파일은 **저장 뒤에.** 여기서 실패해도 디스크에는 이미 값이 들어갔다.
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    # ---- 되읽기 ----
    L('')
    L('--- 되읽기 ---')
    cdo2 = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH))
    got = cdo2.get_editor_property('actions')
    L('  Actions 항목 %d개' % len(got))
    for k in got:
        vs = got[k].get_editor_property('variants')
        names = []
        for v in vs:
            mm = v.get_editor_property('montage')
            names.append('%s(%.1f)' % (mm.get_name() if mm else '<None>',
                                       v.get_editor_property('weight')))
        L('    %-16s %d개  %s' % (k, len(vs), ' · '.join(names)))

    if len(got) < len(ACTIONS):
        E('  ⚠ 기대 %d개보다 적다' % len(ACTIONS))
    else:
        L('=== 완료 ===')
    L('')
    L('⚠ ST_Customer 에 액션 이름 오타가 둘 남아 있다 — 지금은 **별칭으로 막아 뒀다**:')
    L('    K_Act    Interaction_Reach -> Interact_Reach')
    L('    Eating   Sit-Eat           -> Sit_Eat')
    L('  에디터에서 철자를 고치면 이 스크립트의 「트리 오타 별칭」 두 줄도 지울 것.')


main()
