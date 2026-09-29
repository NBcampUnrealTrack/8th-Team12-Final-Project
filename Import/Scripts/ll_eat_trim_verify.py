"""ll_eat_trim.py 의 결과를 **새 프로세스**에서 되읽는다.

먹는 모션이 `AM_Eating_a_Burger` · `AM_Eating_Fries` 둘뿐인지, 지운 4개가
정말 없는지, 다른 행동이 망가지지 않았는지 본다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/BP/BP_Customer'
GONE = (
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_Frantically',
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_a_Drumstick',
    '/Game/BurgerHouse/Anims/Manny/Eating_Frantically',
    '/Game/BurgerHouse/Anims/Manny/Eating_a_Drumstick',
)
KEEP = (
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_a_Burger',
    '/Game/BurgerHouse/Anims/Manny/Montages/AM_Eating_Fries',
    '/Game/BurgerHouse/Anims/Manny/Eating_a_Burger',
    '/Game/BurgerHouse/Anims/Manny/Eating_Fries',
)
fail = []

L('=== 지워졌어야 하는 것 ===')
for p in GONE:
    ok = not EAL.does_asset_exist(p)
    L('  %-58s %s' % (p, 'OK(없음)' if ok else '*** 아직 있다 ***'))
    if not ok:
        fail.append(p)

L('')
L('=== 남아 있어야 하는 것 ===')
for p in KEEP:
    ok = EAL.does_asset_exist(p)
    L('  %-58s %s' % (p, 'OK' if ok else '*** 사라졌다 ***'))
    if not ok:
        fail.append(p)

L('')
L('=== BP_Customer ▸ Actions ===')
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
actions = cdo.get_editor_property('Actions')
for key in actions.keys():
    names = []
    for v in actions[key].get_editor_property('Variants'):
        m = v.get_editor_property('Montage')
        if not m:
            E('  %s 에 빈(None) 변형이 있다' % key)
            fail.append('%s 빈 변형' % key)
            continue
        names.append('%s %.1f' % (m.get_name(), v.get_editor_property('Weight')))
        if m.get_name() in ('AM_Eating_Frantically', 'AM_Eating_a_Drumstick'):
            fail.append('%s 에 %s 남음' % (key, m.get_name()))
    if not names:
        E('  %s 가 비었다 — PlayAction 이 몽타주를 못 찾는다' % key)
        fail.append('%s 비었음' % key)
    L('  %-18s %s' % (key, ' · '.join(names)))

L('')
L('=== DecorationActions (역할 -> 행동 이름) ===')
for k, v in cdo.get_editor_property('DecorationActions').items():
    known = str(v) in [str(x) for x in actions.keys()]
    L('  %-12s -> %-12s %s' % (k, v, '' if known else '*** Actions 에 없는 이름 ***'))
    if not known:
        fail.append('DecorationActions %s -> %s' % (k, v))

L('')
if fail:
    E('=== 실패: %s ===' % ' / '.join(str(f) for f in fail))
else:
    L('=== 전부 통과 ===')
