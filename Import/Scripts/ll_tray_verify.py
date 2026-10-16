"""트레이 세트 · 캐리 애님 · 주문 알림음을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6). 에디터가 켜져 있으면 저장이 전부 False 다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

BP = '/Game/BurgerHouse/BP/BP_Customer'
ST = '/Game/BurgerHouse/BP/ST_Customer'
TAG = 'OrderReadyChime'
fails = []

cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP))

# ───────── ① 트레이 구성
L('=== ① 트레이에 얹힌 것 ===')
for prop in ('tray', 'burger_box', 'fries', 'receipt'):
    try:
        c = cdo.get_editor_property(prop)
    except Exception:
        fails.append('%s 컴포넌트가 없다 — 빌드 확인' % prop)
        continue
    m = c.get_editor_property('static_mesh') if c else None
    if not m:
        fails.append('%s 에 메시가 없다 — 안 보인다' % prop)
        L('  %-12s (메시 없음) <-- ⚠' % prop)
        continue
    s = c.get_editor_property('relative_scale3d')
    b = m.get_bounds().box_extent
    L('  %-12s %-16s loc %s  최종 %.1f × %.1f × %.1f cm'
      % (prop, m.get_name(), c.get_editor_property('relative_location'),
         b.x * 2 * s.x, b.y * 2 * s.y, b.z * 2 * s.z))
    # ⚠ `collision_enabled` 는 직접 노출이 없다 — `body_instance` 안에 있다.
    try:
        ce = c.get_editor_property('body_instance').get_editor_property('collision_enabled')
        if ce != unreal.CollisionEnabled.NO_COLLISION:
            fails.append('%s 의 콜리전이 %s 다 — 손님을 밀거나 내비를 막는다' % (prop, ce))
    except Exception:
        pass

L('')
L('  TraySocket      %s' % cdo.get_editor_property('tray_socket'))
L('  TrayHandOffset  %s' % cdo.get_editor_property('tray_hand_offset'))
if str(cdo.get_editor_property('tray_socket')) in ('RightHand', 'LeftHand'):
    fails.append('TraySocket 이 한 손이다 — 양손으로 들려면 몸통이라야 한다')

# ───────── ② 캐리 애님
L('')
L('=== ② 트레이를 든 동안의 애님 ===')
actions = cdo.get_editor_property('actions')
L('  LocoCarryWalkAction %s' % cdo.get_editor_property('loco_carry_walk_action'))
L('  LocoCarryIdleAction %s' % cdo.get_editor_property('loco_carry_idle_action'))
for key in (str(cdo.get_editor_property('loco_carry_walk_action')),
            str(cdo.get_editor_property('loco_carry_idle_action'))):
    act = actions.get(unreal.Name(key))
    if not act:
        fails.append('Actions 에 %s 가 없다 — 트레이를 들어도 팔이 그대로다' % key)
        continue
    for v in act.get_editor_property('variants'):
        m = v.get_editor_property('montage')
        tr = m.get_editor_property('slot_anim_tracks')[0]
        seg = tr.get_editor_property('anim_track').get_editor_property('anim_segments')[0]
        src = seg.get_editor_property('anim_reference')
        slot = str(tr.get_editor_property('slot_name'))
        rm = src.get_editor_property('enable_root_motion')
        L('  %-16s %-20s slot=%-9s <- %-12s rootmotion=%s'
          % (key, m.get_name(), slot, src.get_name(), rm))
        if slot != 'LocoSlot':
            fails.append('%s 가 %s 슬롯이다 — LocoSlot 이라야 로코모션을 덮는다' % (key, slot))
        if rm:
            fails.append('%s 의 시퀀스에 루트모션이 켜져 있다 — 캡슐 속도가 무시된다' % key)

# ───────── ③ 알림음
L('')
L('=== ③ 주문 알림음 ===')
unreal.EditorLoadingAndSavingUtils.load_map('/Game/BurgerHouse/Maps/L_BurgerHouse')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
tagged = unreal.GameplayStatics.get_all_actors_with_tag(world, unreal.Name(TAG))
L('  태그 «%s» 액터 %d개' % (TAG, len(tagged)))
for a in tagged:
    ac = a.get_editor_property('audio_component')
    L('    %-26s loc %s' % (a.get_actor_label(), a.get_actor_location()))
    L('      Sound %s · AutoActivate %s'
      % (ac.get_editor_property('sound'), ac.get_editor_property('auto_activate')))
    if not ac.get_editor_property('sound'):
        fails.append('%s 에 Sound 가 없다' % a.get_actor_label())
    if ac.get_editor_property('auto_activate'):
        fails.append('%s 의 AutoActivate 가 켜져 있다 — 계속 운다' % a.get_actor_label())
if not tagged:
    fails.append('태그 %s 액터가 없다' % TAG)

ps = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
      for p in (S.get_task_property_names(ST, 'Root/Pickup',
                                          'BurgerHousePlaySoundTask') or [])}
L('  Pickup ▸ Play Sound  ActorTag=%s · Volume=%s'
  % (ps.get('ActorTag'), ps.get('VolumeMultiplier')))
if ps.get('ActorTag') != TAG:
    fails.append('Pickup 의 ActorTag 가 %s 다' % ps.get('ActorTag'))

# ───────── ④ 들고 자리까지 가는 경로 (원래 있던 것)
L('')
L('=== ④ 들고 앉기까지의 경로 (이미 있던 것) ===')
info = S.get_state_tree_info(ST)
states = {str(s.get_editor_property('path')): s
          for s in info.get_editor_property('all_states')}
for p in ('Root/Pickup/P_Take', 'Root/Seat/S_Move', 'Root/Seat/S_Eat'):
    st = states.get(p)
    if not st:
        fails.append('%s 상태가 없다' % p)
        continue
    tks = [str(t.get_editor_property('struct_type')) for t in st.get_editor_property('tasks')]
    L('  %-22s %s' % (p.split('/')[-1], tks))
L('  -> P_Take 에서 Tray(Hold) → S_Move 로 이동 → S_Eat 에서 Sit At Seat')
L('  ⚠ 이 경로는 원래 있던 것이다. 이번에 바꾼 것은 **트레이에 무엇이 얹히나**와')
L('    **드는 동안 어떤 애님인가** 뿐이다.')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 배선 통과 ===')

L('')
L('=== ⚠ PIE 에서 봐야 하는 것 (숫자로 못 잡는다) ===')
L('  1) 트레이가 **양손 앞**에 오는가 — spine2 오프셋 (18, 0, -8) 은 추정이다')
L('  2) 버거·감자·영수증이 트레이 **위에** 놓이는가 — 배치 오프셋도 추정이다')
L('  3) 트레이를 들면 걸음이 `Carrying` 으로 바뀌는가')
L('  4) 자리에 앉을 때 트레이가 **테이블 위로** 옮겨지는가 (PlaceTrayAt)')
L('  5) 주문이 나올 때 차임이 울리는가')
L('  ⚠ 콘솔 `BurgerHouseReady 0` 으로 즉시 Ready 로 만들면 12초를 안 기다려도 된다')
