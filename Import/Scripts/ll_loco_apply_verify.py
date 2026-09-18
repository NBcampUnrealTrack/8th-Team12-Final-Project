"""3단계 검산 — 몽타주 로코모션 배선을 새 프로세스에서 본다.

⚠ **런타임 동작은 여기서 못 본다.** 이 스크립트가 잡는 것은 「조용히 실패하는」
  배선 오류뿐이다 — 행동 이름 철자, 슬롯, 틱 스위치. 실제 걷는 그림은 PIE 로
  봐야 한다 (맨 아래 체크리스트).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP_PATH = '/Game/BurgerHouse/AI/BP_Customer'
MTG_DIR = '/Game/BurgerHouse/Anims/Montages'

fails = []
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP_PATH))

L('=== ① 새 프로퍼티 (C++ 빌드가 반영됐는가) ===')
WANT = {
    'montage_locomotion': True,
    'loco_idle_action': 'Loco_Idle',
    'loco_walk_action': 'Loco_Walk',
    'loco_sit_action': 'Loco_Sit',
    'loco_walk_speed': 5.0,
    'loco_speed_hysteresis': 1.0,
}
vals = {}
for p, want in WANT.items():
    try:
        v = cdo.get_editor_property(p)
        vals[p] = v
        ok = (str(v) == str(want)) if not isinstance(want, float) else abs(v - want) < 0.01
        L('  %-24s %-12s %s' % (p, v, 'OK' if ok else '<-- ⚠ 기대 %s' % want))
        if not ok:
            fails.append('%s 가 %s 다 (기대 %s)' % (p, v, want))
    except Exception as ex:
        E('  %-24s 없다 — 빌드가 안 반영됐다: %s' % (p, str(ex)[:60]))
        fails.append('%s 프로퍼티가 없다 (빌드 확인)' % p)

L('')
L('=== ② 틱 ===')
for p in ('can_ever_tick', 'tick_interval', 'start_with_tick_enabled'):
    try:
        t = cdo.get_editor_property('primary_actor_tick')
        L('  %-24s %s' % (p, t.get_editor_property(p)))
    except Exception as ex:
        E('  %-24s 못 읽음: %s' % (p, str(ex)[:60]))
L('  ⚠ 켜져 있어야 한다. 예전에는 꺼져 있었다 — 걷기 시작·정지는 이벤트가 없어')
L('    속도를 봐야만 알 수 있기 때문이다. bMontageLocomotion 이 꺼지면')
L('    BeginPlay 가 다시 끈다.')

# ---- ③ 행동 이름이 실제로 Actions 에 있고, 그 몽타주가 LocoSlot 인가 ----
L('')
L('=== ③ 행동 이름 -> 몽타주 -> 슬롯 (조용히 실패하는 지점) ===')
actions = cdo.get_editor_property('actions')
for key in ('loco_idle_action', 'loco_walk_action', 'loco_sit_action'):
    name = str(vals.get(key, ''))
    act = actions.get(unreal.Name(name))
    if not act:
        E('  %-12s -> Actions 에 «%s» 가 없다' % (key, name))
        fails.append('Actions 에 %s 가 없다 — PlayAction 이 못 찾는다' % name)
        continue
    variants = act.get_editor_property('variants')
    for v in variants:
        m = v.get_editor_property('montage')
        if not m:
            fails.append('%s 의 변형에 몽타주가 비어 있다' % name)
            continue
        tr = m.get_editor_property('slot_anim_tracks')[0]
        slot = str(tr.get_editor_property('slot_name'))
        nsec = m.get_num_sections()
        sec = str(m.get_section_name(0)) if nsec else '(없음)'
        bi = m.get_editor_property('blend_in').get_editor_property('blend_time')
        L('  %-12s -> %-16s slot=%-10s in=%.2f 섹션%d(%s)'
          % (name, m.get_name(), slot, bi, nsec, sec))
        if slot != 'LocoSlot':
            fails.append('%s 의 몽타주가 %s 슬롯이다 — LocoSlot 이라야 행동 몽타주와 안 싸운다'
                         % (name, slot))
        if nsec < 1:
            fails.append('%s 에 섹션이 없다 — 자기 자신으로 루프시킬 수 없다' % name)
        if abs(bi - 0.20) > 0.01:
            fails.append('%s 블렌드가 %.2f 다 (기대 0.20)' % (name, bi))

# ---- ④ 임계값이 상태 기계와 같은가 ----
L('')
L('=== ④ 임계값이 상태 기계 전이와 같은가 ===')
ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
    for t in unreal.AnimGraphService.get_state_transitions(ABP, m.machine_name) or []:
        if str(t.rule_variable) != 'Speed':
            continue
        L('  전이 %-14s %s (블렌드 %.3f)'
          % (' '.join(str(t.transition_name).split()),
             str(t.rule_summary), t.blend_duration))
L('  C++ LocoWalkSpeed = %s · 히스테리시스 ±%s' % (vals.get('loco_walk_speed'),
                                                  vals.get('loco_speed_hysteresis')))

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 배선 통과 ===')

L('')
L('=== ⚠ 여기서 못 보는 것 — PIE 체크리스트 ===')
L('  1) 워크인 손님이 **걷는가** (LocoSlot 몽타주가 상태 기계를 덮는가)')
L('  2) 걷다 서는 순간 **미끄러지거나 얼어붙지 않는가** (히스테리시스)')
L('  3) 앉을 때 한 프레임 걷는 그림이 **안 나오는가** (bSitting 우선 판정)')
L('  4) 앉은 손님의 **먹는 몽타주가 로코모션을 덮는가** (DefaultSlot > LocoSlot)')
L('  5) 손님이 사라질 때 로그에 경고가 안 뜨는가')
L('  ⚠ 이상하면 BP_Customer 의 bMontageLocomotion 을 끄면 즉시 예전 상태로 돌아간다.')
