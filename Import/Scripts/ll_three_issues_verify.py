"""세 건을 **새 프로세스**에서 검산한다.

  ① 문 앞 대기 제거
  ② 이동 속도 관련 설정 (원인 후보를 숫자로 늘어놓는다 — 확정은 PIE)
  ③ 회전 직전 정지

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).
⚠ 에디터가 켜져 있으면 **저장이 전부 False** 가 된다 — 이번에 그걸로 한 번
  헛돌았다. 커맨드릿을 돌리기 전에 에디터를 닫을 것.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
S = unreal.StateTreeService
ST = '/Game/BurgerHouse/BP/ST_Customer'
BP = '/Game/BurgerHouse/BP/BP_Customer'

fails = []
info = S.get_state_tree_info(ST)
states = {str(s.get_editor_property('path')): s
          for s in info.get_editor_property('all_states')}

# ─────────────────────────── ①
L('=== ① 문 앞 대기 ===')
ent = states.get('Root/Enter')
tks = [str(t.get_editor_property('struct_type')) for t in ent.get_editor_property('tasks')]
trs = [(str(t.get_editor_property('trigger')), str(t.get_editor_property('target_state_name')))
       for t in ent.get_editor_property('transitions')]
L('  Enter  태스크 %s · 전이 %s' % (tks or '(없음)', trs))
if tks:
    fails.append('Enter 에 아직 태스크가 있다 %s — 몽타주 길이만큼 서 있는다' % tks)
if ('OnTick', 'Kiosk') not in trs:
    fails.append('Enter 의 전이가 OnTick -> Kiosk 가 아니다')

# Enter 를 가리키는 회수 경로가 살아 있는지 (지우면 안 되는 이유)
back = [p for p, s in states.items()
        for t in s.get_editor_property('transitions')
        if str(t.get_editor_property('target_state_name')) == 'Enter']
L('  Enter 로 되돌아오는 상태: %s' % [p.split('/')[-1] for p in back])
L('  -> 그래서 Enter 는 남겨 두고 내용만 비웠다')

# Idle_Look 은 다른 데서 계속 쓰여야 한다
users = [p.split('/')[-1] for p, s in states.items()
         for t in s.get_editor_property('tasks')
         if str(t.get_editor_property('struct_type')) == 'BurgerHouseActionTask']
L('  Play Action 이 남아 있는 상태: %s' % users)

# ─────────────────────────── ②
L('')
L('=== ② 이동 속도 — 350 이 안 나오는 이유 후보 ===')
cdo = unreal.get_default_object(unreal.load_object(None, '%s.BP_Customer_C' % BP))
mv = cdo.get_editor_property('character_movement')
speed = mv.get_editor_property('max_walk_speed')
accel = mv.get_editor_property('max_acceleration')
rvo = mv.get_editor_property('avoidance_consideration_radius')

L('  MaxWalkSpeed %.0f · MaxAccel %.0f · RVO %s (반경 %.0f)'
  % (speed, accel, mv.get_editor_property('use_rvo_avoidance'), rvo))
L('')
L('  a) 코너 감속 — 90도 코너에서 속도벡터를 돌리는 데 드는 시간')
L('     Δv = %.0f cm/s · MaxAccel %.0f -> **%.2f초** 동안 속도가 떨어진다'
  % (speed * 1.414, accel, speed * 1.414 / accel))
L('  b) RVO — 반경 %.0fcm 를 %.2f초에 지난다. 앉은 프리필 5명도 회피 대상이다'
  % (rvo, rvo / speed))
L('     홀이 6×11m 라 반경 2m 안에 늘 누군가 있다')
L('  c) 경로가 기물에 붙는다 — RecastNavMesh.AgentRadius 가 캡슐 반지름과 같으면')
L('     가장자리를 타서 코너가 늘어난다 (§9 「남은 것」)')
L('')
L('  ⚠ 셋 다 **평균 속도**를 깎는 것이지 MaxWalkSpeed 를 바꾸지 않는다.')
L('    실제로 얼마가 나오는지는 PIE 로 재야 한다 — 여기서는 못 본다.')

# ─────────────────────────── ③
L('')
L('=== ③ 회전 ===')
kact = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
        for p in (S.get_task_property_names(ST, 'Root/Kiosk/K_Act',
                                            'BurgerHouseActionTask') or [])}
L('  K_Act ▸ FaceSlotKind=%s · FaceYawOffset=%s'
  % (kact.get('FaceSlotKind'), kact.get('FaceYawOffset')))
if kact.get('FaceSlotKind') != 'Kiosk':
    fails.append('K_Act 의 FaceSlotKind 가 Kiosk 가 아니다')

L('  회전 전에 멈추는 코드가 들어갔다 (C++):')
L('    AIController::StopMovement()          경로 추종 중단')
L('    StopMovementImmediately()             속도 0')
L('    StopActiveMovement()                  **가속도** 0  <- orient 가 보는 값')
L('  ⚠ 가속도를 안 지우면 bOrientRotationToMovement 가 다음 프레임에')
L('    캐릭터를 속도 방향으로 되돌린다 — 그게 「회전이 어긋난다」의 정체였다.')

L('')
L('  -- 참고: 회전 중심은 이미 발밑 수직선 위다 --')
mesh = cdo.get_editor_property('mesh')
L('    Mesh relative_location %s  (X·Y 가 0)' % mesh.get_editor_property('relative_location'))
L('    yaw 는 수직축 둘레라 원점 높이와 무관하게 X·Y 가 안 변한다')

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 ===')

L('')
L('=== ⚠ PIE 에서 볼 것 ===')
L('  1) 손님이 문에 들어오자마자 **바로** 키오스크로 간다 (서 있지 않는다)')
L('  2) 걸어가는 동안 폰 보는 몽타주가 **안 나온다** (DefaultSlot 이 안 걸렸다)')
L('  3) 키오스크 앞에서 15도로 서고 **그 자세가 유지된다** (되돌아가지 않는다)')
L('  4) 실제 이동 속도 — 350 에 얼마나 가까운가')
