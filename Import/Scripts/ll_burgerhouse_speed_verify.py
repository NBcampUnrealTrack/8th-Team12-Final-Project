"""버거집 350 고정을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).
⚠ 플레이어 쪽의 **실제 적용**(BeginPlay)은 여기서 못 본다 — PIE 에서 봐야 한다.
  여기서는 「그 값이 제자리에 있는가」만 본다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

CUST = '/Game/BurgerHouse/AI/BP_Customer'
PLAYER = '/Game/MetaHumans/MHC_Character_Adam/Cheon/BP_CHeon'
LEVEL = 'L_BurgerHouse'
TARGET = 350.0
fails = []

pc = unreal.get_default_object(unreal.load_object(None, '%s.BP_CHeon_C' % PLAYER))
cc = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % CUST))
cmv = cc.get_editor_property('character_movement')

L('=== 버거집에서의 속도 ===')
over = pc.get_editor_property('level_walk_speed_overrides')
p_here = over.get(unreal.Name(LEVEL))
n_here = cmv.get_editor_property('max_walk_speed')
L('  플레이어  %s' % (('%.1f' % p_here) if p_here is not None else '(오버라이드 없음)'))
L('  손님      %.1f' % n_here)
if p_here is None or abs(p_here - TARGET) > 0.01:
    fails.append('플레이어 오버라이드가 %s 다 (기대 %.1f)' % (p_here, TARGET))
if abs(n_here - TARGET) > 0.01:
    fails.append('손님 속도가 %.1f 다 (기대 %.1f)' % (n_here, TARGET))
if p_here is not None and abs(p_here - n_here) > 0.01:
    fails.append('둘이 안 맞는다 — 플레이어 %.1f · 손님 %.1f' % (p_here, n_here))

L('')
L('=== 다른 레벨은 안 건드려졌나 (회귀) ===')
L('  플레이어 WalkSpeed(기본)  %.1f  <- 다른 레벨은 이 값' % pc.get_editor_property('walk_speed'))
L('  플레이어 SprintSpeed      %.1f' % pc.get_editor_property('sprint_speed'))
L('  오버라이드 목록           %s' % {str(k): over[k] for k in over.keys()})
if abs(pc.get_editor_property('walk_speed') - 500.0) > 0.01:
    fails.append('WalkSpeed 기본값이 500 이 아니다 — 모든 레벨이 영향받는다')
if len(over) != 1:
    fails.append('오버라이드가 %d개다 — 버거집 하나만 의도했다' % len(over))

L('')
L('=== 스프린트 차단 (같이 걸려 있어야 350 이 「고정」이다) ===')
blocked = [str(x) for x in pc.get_editor_property('sprint_blocked_levels')]
L('  SprintBlockedLevels %s' % blocked)
if LEVEL not in blocked:
    fails.append('%s 가 스프린트 차단 목록에 없다 — 눌러서 1000 이 되면 「고정」이 아니다' % LEVEL)

L('')
L('=== 손님 가감속 (속도 ×4 / ×6) ===')
for p, mul in (('max_acceleration', 4.0), ('braking_deceleration_walking', 6.0)):
    got = cmv.get_editor_property(p)
    want = TARGET * mul
    ok = abs(got - want) < 0.5
    L('  %-30s %8.1f  %s' % (p, got, 'OK' if ok else '<-- ⚠ 기대 %.1f' % want))
    if not ok:
        fails.append('%s 가 %.1f 다 (기대 %.1f)' % (p, got, want))

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 ===')

L('')
L('=== ⚠ PIE 에서 확인할 것 ===')
L('  1) 버거집에서 플레이어가 **350 으로** 걷는다 (BeginPlay 가 붙였는가)')
L('  2) 버거집에서 스프린트를 눌렀다 떼도 **350 으로 돌아온다**')
L('     — EndSprint 가 WalkSpeed(500) 가 아니라 GetEffectiveWalkSpeed() 를 쓴다')
L('  3) 다른 레벨에서는 걷기 500 · 스프린트 1000 그대로')
L('  4) 손님과 플레이어가 나란히 걸을 때 속도가 같다')
L('  5) 손님 발 미끄러짐 — 애님 183 vs 캡슐 350 (1.91배, 지금까지 중 가장 작다)')
