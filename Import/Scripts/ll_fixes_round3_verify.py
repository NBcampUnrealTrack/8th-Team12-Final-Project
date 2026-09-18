"""라운드 3 검산 — **새 프로세스**로 돌릴 것.

    "...UnrealEditor-Cmd.exe" Febricula.uproject -run=pythonscript
        -script=".../ll_fixes_round3_verify.py" -unattended -nopause -nosplash

⚠ 같은 프로세스에서 되읽으면 메모리에 있는 것을 읽어 **디스크에 안 써졌어도
  통과한다** (§6). 반드시 따로 돌린다.
"""
import unreal
import math

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/AI/ST_Customer'
fails = []


def check(label, got, want, tol=None):
    if tol is None:
        ok = (str(got) == str(want))
    else:
        ok = abs(float(got) - float(want)) <= tol
    L('  %-46s %-14s %s' % (label, got, 'OK' if ok else ('<-- 기대 %s' % want)))
    if not ok:
        fails.append(label)


def taskprops(state, struct):
    return {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
            for p in (S.get_task_property_names(ST, state, struct) or [])}


L('=== 1) 알림음 ===')
check('K_Order VolumeMultiplier (주문 넣을 때)',
      taskprops('Root/Kiosk/K_Order', 'BurgerHousePlaySoundTask').get('VolumeMultiplier'),
      0.02, tol=1e-4)
check('Pickup VolumeMultiplier (주문 나올 때 — 안 건드림)',
      taskprops('Root/Pickup', 'BurgerHousePlaySoundTask').get('VolumeMultiplier'),
      0.2, tol=1e-4)

L('')
L('=== 4) 바라보는 방향 — 트리 ===')
for st, kind in (('Root/Kiosk/K_Act', 'Kiosk'),
                 ('Root/P_Ordering/PO_Kiosk/PO_Act', 'Kiosk'),
                 ('Root/Pickup/P_Act', 'Pickup')):
    check('%s FaceSlotKind' % st.split('/')[-1],
          taskprops(st, 'BurgerHouseActionTask').get('FaceSlotKind'), kind)
for st in ('Root/Wait/W_Idle', 'Root/P_Waiting/PW_Wait/PW_Idle'):
    d = taskprops(st, 'BurgerHouseActionTask')
    check('%s bFaceWorldYaw' % st.split('/')[-1], d.get('bFaceWorldYaw'), 'True')
    check('%s FaceWorldYaw' % st.split('/')[-1], d.get('FaceWorldYaw'), -90.0, tol=0.1)

L('')
L('=== 5) Telling_A_Secret 이 흔적도 없나 ===')
for p in ('/Game/BurgerHouse/Anims/Montages/AM_Telling_A_Secret',
          '/Game/BurgerHouse/Anims/Mixamo/Anims/Telling_A_Secret'):
    check('에셋 %s' % p.split('/')[-1], EAL.does_asset_exist(p), False)

cdo = unreal.get_default_object(EAL.load_asset('/Game/BurgerHouse/AI/BP_Customer').generated_class())
names = []
for k, v in cdo.get_editor_property('actions').items():
    for var in v.get_editor_property('variants'):
        m = var.get_editor_property('montage')
        names.append(m.get_name() if m else 'None')
check('Actions 맵 안의 Telling 개수', sum('Telling' in n for n in names), 0)
check('Idle_Look 변형', sorted(
    (var.get_editor_property('montage').get_name()
     for var in cdo.get_editor_property('actions')['Idle_Look'].get_editor_property('variants'))),
    "['AM_Idle', 'AM_Texting']")

L('')
L('=== 3) 음식 크기 — 바닥이 상판(2.25)에 계속 붙어 있나 ===')
WANT = {'burger_box': 7.5, 'fries': 0.6, 'receipt': 0.04}
for bp in ('/Game/BurgerHouse/AI/BP_Customer', '/Game/BurgerHouse/AI/BP_CounterTray'):
    c0 = unreal.get_default_object(EAL.load_asset(bp).generated_class())
    L('  --- %s' % bp.split('/')[-1])
    for prop, want_scale in WANT.items():
        c = c0.get_editor_property(prop)
        t = c.get_relative_transform()
        b = c.static_mesh.get_bounds()
        bottom = t.translation.z + (b.origin.z - b.box_extent.z) * t.scale3d.z
        sx = b.box_extent.x * 2 * t.scale3d.x
        sy = b.box_extent.y * 2 * t.scale3d.y
        # 트레이는 X 30 x Y 40 — 중심에서 벗어난 만큼 더한 값이 반폭을 넘으면 밖이다
        cx = t.translation.x + b.origin.x * t.scale3d.x
        cy = t.translation.y + b.origin.y * t.scale3d.y
        inside = (abs(cx) + sx / 2 <= 15.0 + 0.01) and (abs(cy) + sy / 2 <= 20.0 + 0.01)
        L('    %-11s scale %.3f  바닥z %.2f  크기 %.1f x %.1f  중심(%.1f,%.1f) %s'
          % (prop, t.scale3d.x, bottom, sx, sy, cx, cy,
             '트레이 안' if inside else '<-- 트레이 밖!'))
        if abs(t.scale3d.x - want_scale) > 1e-3:
            fails.append('%s.%s 스케일' % (bp.split('/')[-1], prop))
        if abs(bottom - 2.25) > 0.1:
            fails.append('%s.%s 바닥' % (bp.split('/')[-1], prop))
        if not inside:
            fails.append('%s.%s 트레이 밖' % (bp.split('/')[-1], prop))
        mat = c.get_material(0)
        if not mat:
            fails.append('%s.%s 머티리얼' % (bp.split('/')[-1], prop))

L('')
L('=== 2)4) 레벨 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

cls = unreal.load_object(None, '/Game/BurgerHouse/AI/BP_CounterTray.BP_CounterTray_C')
trays = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
check('CounterTray 개수', len(trays), 1)
for a in trays:
    l = a.get_actor_location()
    r = a.get_actor_rotation()
    check('CounterTray y', l.y, -1140.0, tol=0.5)
    # 가로로 눕혀야 한다. ⚠ `unreal.Rotator` 는 (roll, pitch, yaw) 순서라
    #   pitch 에 90 이 들어가면 트레이가 **서 버린다** (§9-8). 셋 다 본다.
    #   ⚠ roll 은 −0.0 으로 나올 수 있어 문자열 비교가 아니라 절대값으로 본다.
    check('CounterTray roll', abs(r.roll), 0.0, tol=0.5)
    check('CounterTray pitch', abs(r.pitch), 0.0, tol=0.5)
    check('CounterTray yaw', r.yaw, 90.0, tol=0.5)

# 키오스크 — 「손님이 정말 키오스크를 보나」를 각도로 되짚는다
for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint):
    if str(s.get_editor_property('kind')) != 'Kiosk':
        continue
    ut = s.get_use_transform()
    stand = ut.translation
    furn = s.get_actor_location()
    want = math.degrees(math.atan2(furn.y - stand.y, furn.x - stand.x))
    got = ut.rotation.rotator().yaw
    d = (got - want + 180.0) % 360.0 - 180.0
    L('  %-16s 손님(%.0f,%.0f) -> 키오스크(%.0f,%.0f) 필요 yaw %.1f · 실제 %.1f · 오차 %.1f %s'
      % (s.get_actor_label(), stand.x, stand.y, furn.x, furn.y, want, got, d,
         'OK' if abs(d) < 1.0 else '<-- 어긋남'))
    if abs(d) >= 1.0:
        fails.append('%s 방향' % s.get_actor_label())

L('')
if fails:
    E('=== 실패 %d건: %s ===' % (len(fails), fails))
else:
    L('=== 전부 통과 ===')
