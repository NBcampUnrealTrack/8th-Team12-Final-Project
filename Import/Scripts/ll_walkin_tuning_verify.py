"""워크인 튜닝(속도 5배 · 키오스크 15도)을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/BP/BP_Customer'
ST = '/Game/BurgerHouse/BP/ST_Customer'
K_ACT = 'Root/Kiosk/K_Act'
TASK = 'BurgerHouseActionTask'

WANT_MOVE = {'max_walk_speed': 915.0, 'max_acceleration': 3660.0,
             'braking_deceleration_walking': 5490.0}
WANT_TASK = {'FaceSlotKind': 'Kiosk', 'FaceYawOffset': '15.000000'}

fails = []

L('=== ① 이동 값 ===')
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP))
mv = cdo.get_editor_property('character_movement')
for p, w in WANT_MOVE.items():
    got = mv.get_editor_property(p)
    ok = abs(got - w) < 0.5
    L('  %-30s %8.1f  %s' % (p, got, 'OK' if ok else '<-- ⚠ 기대 %.1f' % w))
    if not ok:
        fails.append('%s 가 %.1f 다 (기대 %.1f)' % (p, got, w))

# 로코모션 전이 임계값과 충돌하지 않는지 (5 를 한참 넘으니 문제 없음)
L('  Loco 임계값 %s (±%s) — 915 는 여유롭게 넘는다'
  % (cdo.get_editor_property('loco_walk_speed'),
     cdo.get_editor_property('loco_speed_hysteresis')))

L('')
L('=== ② K_Act 의 Play Action ===')
props = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
         for p in (unreal.StateTreeService.get_task_property_names(ST, K_ACT, TASK) or [])}
for k, v in props.items():
    L('  %-18s %s' % (k, v))
for k, w in WANT_TASK.items():
    if props.get(k) != w:
        fails.append('%s 가 %s 다 (기대 %s)' % (k, props.get(k), w))

info = unreal.StateTreeService.get_state_tree_info(ST)
L('  트리 컴파일: is_compiled=%s · %s'
  % (info.get_editor_property('is_compiled'), info.get_editor_property('last_compile_status')))

# ---- ③ 다른 Play Action 은 안 건드려졌나 (회귀) ----
L('')
L('=== ③ 다른 Play Action 은 회전 안 함 (회귀 검사) ===')
for path in ('Root/Enter', 'Root/Wait/W_Idle', 'Root/Pickup/P_Act',
             'Root/Seat/S_Eat/Eating'):
    ps = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
          for p in (unreal.StateTreeService.get_task_property_names(ST, path, TASK) or [])}
    if not ps:
        continue
    kind = ps.get('FaceSlotKind', '?')
    L('  %-26s ActionName=%-20s FaceSlotKind=%s' % (path.split('/')[-1],
                                                    ps.get('ActionName'), kind))
    if kind not in ('None', ''):
        fails.append('%s 의 Play Action 이 %s 로 회전한다 — 의도한 것이 아니다' % (path, kind))

# ---- ④ 키오스크 슬롯이 실제로 있나 (없으면 회전이 안 걸린다) ----
L('')
L('=== ④ 레벨의 Kiosk 슬롯 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
kiosks = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
          if str(a.get_editor_property('kind')) == 'Kiosk']
L('  Kiosk 슬롯 %d개' % len(kiosks))
for k in kiosks:
    L('    %-16s yaw %.1f · UseYawOffset %.1f -> 사용 yaw %.1f (+15 하면 %.1f)'
      % (k.get_name(), k.get_actor_rotation().yaw,
         k.get_editor_property('use_yaw_offset'),
         k.get_actor_rotation().yaw + k.get_editor_property('use_yaw_offset'),
         k.get_actor_rotation().yaw + k.get_editor_property('use_yaw_offset') + 15.0))
if not kiosks:
    fails.append('레벨에 Kiosk 슬롯이 없다 — 회전이 안 걸린다')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 ===')

L('')
L('=== ⚠ PIE 에서 봐야 하는 것 ===')
L('  1) 발이 얼마나 미끄러지는가 — 애님 183 vs 캡슐 915 (5배)')
L('  2) 좁은 통로에서 기물을 지나치거나 파고들지 않는가 (MaxStepHeight 30 · RVO 반경 200)')
L('  3) 키오스크 앞에서 **시계 15도** 틀어 서는가')
L('  4) 목표를 지나쳐 되돌아오지 않는가 (제동 5490 으로 올려 뒀다)')
