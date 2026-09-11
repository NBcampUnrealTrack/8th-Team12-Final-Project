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

ANIM_DIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
MTG_DIR = '/Game/BurgerHouse/Anims/Montages'
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
    # ⚠ `Idle` 은 첫·끝 프레임 차이가 **0.0** 인 진짜 순환 동작이라 줄에서
    #   루프로 돌 때 이음매가 없다. 그래서 가장 큰 가중치를 준다.
    'Idle_Look': [
        ('Idle', 1.0, 'AM_Idle', DEFAULT),                          # 8.33s
        ('Telling_A_Secret', 0.6, 'AM_Telling_A_Secret', DEFAULT),  # 10.90s
        ('Texting', 0.3, 'AM_Texting', DEFAULT),                    # 18.53s
    ],
    'Interact_Reach': [
        ('Reaching_Out', 1.0, 'AM_Reaching_Out', DEFAULT),
    ],

    # 앉은 **장식 손님**이 끝없이 돌릴 몸짓 — 먹는 것 4종.
    #
    # ## ⚠ 한 사람은 **하나만** 돌린다
    #
    # 여기 넷을 넣어 두지만 손님 하나는 그중 **한 가지만** 평생 반복한다
    # (`ACustomerCharacter::bDecorationSingleAction`). 앉아서 밥 먹는 사람이
    # 「버거 → 폰 → 감자튀김」으로 계속 바꾸지는 않는다. 다양성은 한 사람
    # 안에서가 아니라 **손님들 사이에서** 나와야 한다.
    #
    # 어느 것을 받을지는 **좌석 번호**에서 결정적으로 뽑는다 — 같은 의자는
    # 늘 같은 몸짓, 옆자리는 다른 몸짓 (`DecorationSeed` 주석).
    #
    # ⚠ 폰 만지는 몸짓(`AM_Sit_Texting`·`AM_Sit_Talking_On_A_Cell_Phone`)은
    #   여기서 뺐다. 그 둘은 **서 있는 동작**이라 전신으로 틀면 앉은 손님이
    #   일어선다. 에셋은 남겨 둔다 — 되살리려면 AnimBP 의 상체 레이어가 필요하다.
    'Sit_Idle': [
        ('Eating_a_Burger', 1.0, 'AM_Eating_a_Burger', DEFAULT),
        ('Eating_Fries', 1.0, 'AM_Eating_Fries', DEFAULT),
        ('Eating_a_Drumstick', 1.0, 'AM_Eating_a_Drumstick', DEFAULT),
        ('Eating_Frantically', 0.6, 'AM_Eating_Frantically', DEFAULT),
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


def set_blend(m):
    """블렌드 시간을 맞춘다. ⚠ 배열이 아니라 구조체라 되넣을 필요는 없지만,
    `get_editor_property` 가 **복사본**을 주므로 고친 뒤 다시 넣어야 한다."""
    for prop in ('blend_in', 'blend_out'):
        b = m.get_editor_property(prop)
        b.set_editor_property('blend_time', BLEND)
        m.set_editor_property(prop, b)


def make_montage(seq_name, mtg_name, slot):
    """시퀀스 하나로 몽타주 하나. 이미 있으면 슬롯·블렌드만 맞춘다."""
    seq = EAL.load_asset('%s/%s' % (ANIM_DIR, seq_name))
    if not seq:
        E('  %s — 시퀀스가 없다' % seq_name)
        return None

    path = '%s/%s' % (MTG_DIR, mtg_name)
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
    set_blend(m)
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
            variants.append(v)
        ca = unreal.CustomerAction()
        ca.set_editor_property('variants', variants)
        actions[unreal.Name(action)] = ca

    cdo.set_editor_property('actions', actions)

    bp = EAL.load_asset(BP_PATH)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_asset(BP_PATH, only_if_is_dirty=False)

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
