"""워크인 리듬 · 도착 정밀도 · 알림음 볼륨을 **새 프로세스**에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리다 (§6).
⚠ 에디터가 켜져 있으면 저장이 전부 False 다 — 그 상태로 「적용됐다」고 믿지 말 것.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
S = unreal.StateTreeService
ST = '/Game/BurgerHouse/AI/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
fails = []


def props(state, task):
    return {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
            for p in (S.get_task_property_names(ST, state, task) or [])}


# ───────────── 스포너
L('=== ① 워크인 생성 리듬 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    d = sp.get_editor_property('first_spawn_delay')
    j = sp.get_editor_property('first_spawn_delay_jitter')
    iv = sp.get_editor_property('spawn_interval')
    ij = sp.get_editor_property('spawn_interval_jitter')
    w = [float(x) for x in sp.get_editor_property('spawn_burst_weights')]
    L('  첫 워크인   %.0f ±%.0f초  (%.0f ~ %.0f초)' % (d, j, d - j, d + j))
    L('  이후 주기   %.0f ±%.0f초  (%.0f ~ %.0f초)' % (iv, ij, iv - ij, iv + ij))
    L('  한 번에     가중치 %s' % w)

    if abs(d - 60.0) > 0.5 or abs(j - 30.0) > 0.5:
        fails.append('첫 지연이 %.1f ±%.1f 다 (기대 60 ±30)' % (d, j))
    if len(w) != 1 or abs(w[0] - 1.0) > 0.01:
        fails.append('SpawnBurstWeights 가 %s 다 — [1.0] 이라야 항상 1명' % w)
    else:
        L('  -> 항상 **1명**. 2명이 같이 들어오는 경우가 없어졌다')

    # 정원 대비 의미
    alive = sp.get_editor_property('fixed_desired_alive')
    L('  정원 %d · 프리필 5(영구) -> 워크인 자리 %d개' % (alive, alive - 5))
    L('     빈 자리 %d개를 채우는 데 최대 %.0f분' % (alive - 5, (alive - 5) * (iv + ij) / 60.0))

# ───────────── 트리
L('')
L('=== ② 키오스크 도착 · 회전 ===')
mv = props('Root/Kiosk/K_Move', 'StateTreeMoveToTask')
ac = props('Root/Kiosk/K_Act', 'BurgerHouseActionTask')

radius = float(mv.get('AcceptableRadius', -1))
incl = mv.get('bReachTestIncludesAgentRadius')
L('  AcceptableRadius              %s' % radius)
L('  bReachTestIncludesAgentRadius %s' % incl)
L('  FaceSlotKind                  %s' % ac.get('FaceSlotKind'))
L('  FaceYawOffset                 %s' % ac.get('FaceYawOffset'))

if incl not in ('false', 'False'):
    fails.append('bReachTestIncludesAgentRadius 가 %s 다 — 캡슐 반지름 34cm 가 그대로 더해진다' % incl)
if abs(float(ac.get('FaceYawOffset', -1)) - 0.0) > 0.01:
    fails.append('FaceYawOffset 이 %s 다 (기대 0)' % ac.get('FaceYawOffset'))
if ac.get('FaceSlotKind') != 'Kiosk':
    fails.append('FaceSlotKind 가 %s 다 — 키오스크를 등지는 버그가 돌아온다' % ac.get('FaceSlotKind'))

L('')
L('  -- 도착 오차 --')
L('     전: %0.0f + 에이전트 34 = 최대 **39cm**' % 5.0)
L('     후: %0.0f + 0            = 최대 **%0.0fcm**' % (radius, radius))
L('  ⚠ 캡슐이 키오스크에 더 가까워진다. 파고들면 AcceptableRadius 를 10~15 로.')

# ───────────── 볼륨
L('')
L('=== ③ 알림음 볼륨 ===')
snd = props('Root/Kiosk/K_Order', 'BurgerHousePlaySoundTask')
for k in ('ActorTag', 'Sound', 'VolumeMultiplier'):
    L('  %-18s %s' % (k, snd.get(k)))
if abs(float(snd.get('VolumeMultiplier', -1)) - 0.2) > 0.001:
    fails.append('VolumeMultiplier 가 %s 다 (기대 0.2)' % snd.get('VolumeMultiplier'))
if snd.get('Sound') in ('None', '', None):
    L('  ⚠ Sound 가 아직 비어 있다 — 붙이기 전까지는 볼륨과 무관하게 소리가 안 난다')

info = S.get_state_tree_info(ST)
L('')
L('  트리 컴파일 %s · %s' % (info.get_editor_property('is_compiled'),
                            info.get_editor_property('last_compile_status')))

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 통과 ===')

L('')
L('=== ⚠ PIE 에서 볼 것 ===')
L('  1) 레벨을 켜고 **30~90초 동안** 워크인이 안 들어온다 (앉은 5명만 있다)')
L('  2) 들어올 때 **항상 한 명**이다')
L('  3) 키오스크 앞에 더 바짝 선다 — 대신 메시를 파고들지 않는가')
L('  4) 키오스크를 **정면으로** 본다 (15도가 빠졌다)')
L('  5) 알림음이 작게 난다 (Sound 를 붙인 뒤)')
