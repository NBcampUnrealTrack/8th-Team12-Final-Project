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
ACTIONS = {
    'Idle_Look': [
        ('Texting_While_Standing', 1.0, 8.0),
        ('Talking_On_A_Cell_Phone', 0.5, 8.0),
        ('Texting', 0.4, 8.0),
        ('Telling_A_Secret', 0.2, 8.0),
    ],
    'Interact_Reach': [
        ('Reaching_Out', 1.0, None),        # 4초 — 자를 필요 없다
    ],
    # 먹는 동작 — Eat_Animation_Pack 을 손님 스켈레톤으로 리타겟한 것
    # (`ll_eat_retarget.py`). 길이가 6.9~9.2초로 고르고 자를 필요가 없다.
    #
    # 가중치를 전부 1.0 으로 둬서 **네 개가 고르게** 나오게 한다. 고르는 것은
    # `ACustomerCharacter::PickVariant` 가 하고, `bAvoidRepeatVariant` 가
    # 같은 것이 연속으로 나오지 않게 막는다.
    'Sit_Eat': [
        ('Eating_a_Burger', 1.0, None),
        ('Eating_a_Drumstick', 1.0, None),
        ('Eating_Frantically', 1.0, None),
        ('Eating_Fries', 1.0, None),
    ],
}


def make_montage(seq_name, trim):
    """시퀀스 하나로 몽타주 하나. 이미 있으면 그대로 쓴다."""
    seq = EAL.load_asset('%s/%s' % (ANIM_DIR, seq_name))
    if not seq:
        E('  %s — 시퀀스가 없다' % seq_name)
        return None

    path = '%s/AM_%s' % (MTG_DIR, seq_name)
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)

    fac = unreal.AnimMontageFactory()
    fac.set_editor_property('target_skeleton', seq.get_editor_property('skeleton'))
    fac.set_editor_property('source_animation', seq)
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'AM_%s' % seq_name, MTG_DIR, unreal.AnimMontage, fac)
    if not m:
        E('  %s — 몽타주 생성 실패' % seq_name)
        return None

    if trim:
        trim_montage(m, trim)
    return m


def trim_montage(m, seconds):
    """앞에서부터 `seconds` 만큼만 남긴다.

    ⚠ 구조체는 **복사본**으로 돌아온다 — 고쳐서 되넣어야 반영된다.
    """
    full = m.get_editor_property('sequence_length')
    if full <= seconds:
        return
    try:
        tracks = m.get_editor_property('slot_anim_tracks')
        for t in tracks:
            track = t.get_editor_property('anim_track')
            segs = track.get_editor_property('anim_segments')
            for s in segs:
                s.set_editor_property('anim_end_time', seconds)
            track.set_editor_property('anim_segments', segs)
            t.set_editor_property('anim_track', track)
        m.set_editor_property('slot_anim_tracks', tracks)
        m.set_editor_property('sequence_length', seconds)
    except Exception as ex:
        L('    자르기 실패 (원본 길이로 둔다): %s' % str(ex)[:80])


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
        for seq_name, weight, trim in variants:
            m = make_montage(seq_name, trim)
            if not m:
                continue
            EAL.save_asset(m.get_path_name().split('.')[0], only_if_is_dirty=False)
            L('  %-16s AM_%-26s %5.2fs  w=%.1f' % (
                action, seq_name, m.get_editor_property('sequence_length'), weight))
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
    L('⚠ ST_Customer 에 액션 이름 오타가 둘 있다 — 에디터에서 고칠 것:')
    L('    K_Act    Interaction_Reach -> Interact_Reach')
    L('    Eating   Sit-Eat           -> Sit_Eat')
    L('  이 둘은 몽타주를 꽂아도 못 찾는다 (FName 이라 조용히 실패한다).')


main()
