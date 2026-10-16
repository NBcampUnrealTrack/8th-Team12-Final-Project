"""세 가지 문의를 실측한다. 안 고친다.

  ① 문 앞 대기가 어디서 오나
  ② 키오스크까지 왜 350 이 안 나오나
  ③ Ordering 회전의 중심이 어디인가
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/BP/ST_Customer'
BP = '/Game/BurgerHouse/BP/BP_Customer'


def task_props(path, task):
    return {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
            for p in (S.get_task_property_names(ST, path, task) or [])}


# ─────────────────────────────────────────── ①
L('=== ① 문 앞 대기 ===')
ent = task_props('Root/Enter', 'BurgerHouseActionTask')
for k in ('ActionName', 'FallbackSeconds', 'RandomExtraSeconds', 'RandomExtraRepeats', 'bLoop'):
    L('  Enter ▸ Play Action  %-20s %s' % (k, ent.get(k)))

cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP))
actions = cdo.get_editor_property('actions')
il = actions.get(unreal.Name(ent.get('ActionName', 'Idle_Look')))
L('')
L('  그 행동의 몽타주 길이 (= 상태에 머무는 시간)')
lens = []
if il:
    for v in il.get_editor_property('variants'):
        m = v.get_editor_property('montage')
        if m:
            d = m.get_editor_property('sequence_length')
            lens.append((m.get_name(), d, v.get_editor_property('weight')))
tot_w = sum(w for _, _, w in lens) or 1.0
for n, d, w in lens:
    L('    %-24s %6.2fs  w=%.1f' % (n, d, w))
if lens:
    exp = sum(d * w for _, d, w in lens) / tot_w
    extra = float(ent.get('RandomExtraSeconds', 0) or 0)
    L('    -> 기대값 %.1fs · 최대 %.1fs (+ RandomExtra 0~%.1fs)'
      % (exp + extra / 2, max(d for _, d, _ in lens) + extra, extra))

# 전이도 본다 — 무엇이 이 상태를 끝내나
info = S.get_state_tree_info(ST)
states = {str(s.get_editor_property('path')): s
          for s in info.get_editor_property('all_states')}
for p in ('Root/Enter', 'Root/P_Ordering'):
    st = states.get(p)
    if not st:
        continue
    trs = ['%s -> %s' % (t.get_editor_property('trigger'),
                         t.get_editor_property('target_state_name'))
           for t in st.get_editor_property('transitions')]
    tks = [str(t.get_editor_property('struct_type')) for t in st.get_editor_property('tasks')]
    L('  %-18s 태스크 %s · 전이 %s' % (p.split('/')[-1], tks, trs))
L('  ⚠ P_Ordering 은 OnTick 으로 즉시 나간다 — 「기다리지 않는 상태」의 본보기다')

# ─────────────────────────────────────────── ②
L('')
L('=== ② 키오스크까지의 이동 ===')
mp = task_props('Root/Kiosk/K_Move', 'StateTreeMoveToTask')
for k in sorted(mp):
    L('  K_Move ▸ Move To  %-26s %s' % (k, mp[k]))

mv = cdo.get_editor_property('character_movement')
L('')
L('  -- CharacterMovement (AI 이동에 영향 주는 것) --')
for p in ('max_walk_speed', 'max_acceleration', 'braking_deceleration_walking',
          'use_rvo_avoidance', 'avoidance_consideration_radius',
          'requested_move_use_acceleration', 'use_controller_desired_rotation',
          'orient_rotation_to_movement', 'rotation_rate',
          'ground_friction', 'braking_friction_factor', 'max_step_height',
          'min_analog_walk_speed'):
    try:
        L('  %-34s %s' % (p, mv.get_editor_property(p)))
    except Exception:
        L('  %-34s (없음)' % p)

L('')
L('  -- 내비 에이전트 (DefaultEngine.ini SupportedAgents) --')
L('  ⚠ 경로가 기물에 붙으면 코너마다 감속한다. AgentRadius 가 캡슐 반지름과')
L('    같으면(34) 가장자리를 타므로 특히 그렇다 (§9 「남은 것」).')

# ─────────────────────────────────────────── ③
L('')
L('=== ③ Ordering 회전의 중심 ===')
mesh = cdo.get_editor_property('mesh')
L('  Mesh 컴포넌트  relative_location %s' % mesh.get_editor_property('relative_location'))
L('                 relative_rotation %s' % mesh.get_editor_property('relative_rotation'))
cap = cdo.get_editor_property('capsule_component')
L('  캡슐           half_height %.1f · radius %.1f'
  % (cap.get_editor_property('capsule_half_height'), cap.get_editor_property('capsule_radius')))
L('  ⚠ 액터 원점은 **캡슐 중심**이다. 발밑은 그보다 %.0fcm 아래다.'
  % cap.get_editor_property('capsule_half_height'))
L('    하지만 yaw 회전은 **수직축** 둘레라, 원점이 위아래로 어디에 있든')
L('    X·Y 위치는 안 변한다 — 높이만으로는 「도는 것처럼」 보일 수 없다.')

L('')
L('  -- 그러면 무엇이 도나: 애님의 골반이 원점에서 **수평으로** 떨어져 있나 --')
for name in ('Reaching_Out', 'Idle', 'Walk_Fwd_Manny', 'SitIdle_Male'):
    a = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Anims/%s' % name)
    if not a:
        continue
    d = a.get_editor_property('sequence_length')
    worst = (0.0, 0.0, 0.0)
    for i in range(9):
        t = min(d * i / 8.0, d - 1e-4)
        p = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', t, False).translation
        r = (p.x ** 2 + p.y ** 2) ** 0.5
        if r > worst[2]:
            worst = (p.x, p.y, r)
    L('  %-18s 골반 수평 오프셋 최대 %6.1fcm  (x %7.1f · y %7.1f)'
      % (name, worst[2], worst[0], worst[1]))

L('')
L('  ⚠ 이 값이 크면 액터를 제자리에서 돌려도 **몸이 그 반지름으로 돈다.**')
L('    회전 중심을 「발밑」으로 옮긴다는 것은 곧 이 오프셋을 보정한다는 뜻이다.')
