"""Walk 애님 교체가 **디스크에 실제로 써졌는지** 새 프로세스에서 검산한다.

⚠ 같은 실행 안의 되읽기는 메모리를 보는 것이라 의미가 없다 (§6). 이 스크립트는
  반드시 `ll_walk_apply.py` 와 **다른 프로세스**로 돌린다.

보는 것 —
  ① `ABP_Customer` 의 Walk 상태가 `Walk_Fwd_Manny` 를 물고 있나
  ② `BP_Customer` 의 MaxWalkSpeed 가 183 인가
  ③ 새 애님이 여전히 제자리이고 비어 있지 않나
  ④ 기존 `Walking` 을 아직 참조하는 곳이 남았나
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
BP = '/Game/BurgerHouse/AI/BP_Customer'
NEW = '/Game/BurgerHouse/Anims/Mixamo/Anims/Walk_Fwd_Manny'
OLD = '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking'

fails = []

# ---- ① Walk 상태의 애님 ----
L('=== ① ABP_Customer 의 Walk 상태 ===')
L('  AnimGraphService 의 상태 조회 메서드: %s'
  % [m for m in dir(unreal.AnimGraphService)
     if 'state' in m and not m.startswith('_')])
for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
    mn = str(m.machine_name).strip()
    for s in unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or []:
        sn = str(s.state_name).strip()
        extra = []
        for prop in ('animation_asset', 'animation', 'sequence', 'asset_path', 'state_type'):
            try:
                v = getattr(s, prop)
                if v:
                    extra.append('%s=%s' % (prop, v))
            except Exception:
                continue
        L('  [%s] %-24s %s' % (mn, sn, ' · '.join(extra)))

# 상태 조회로 애님을 못 읽을 수도 있다 -> **참조 그래프**로 확인한다.
# ⚠ 이쪽이 더 믿을 만하다. ABP 가 애님을 참조하면 패키지 의존성에 남는다.
L('')
L('  -- ABP 의 애님 의존성 --')
deps = EAL.find_package_referencers_for_asset(NEW, False) or []
L('  %s 를 참조하는 패키지: %s' % (NEW.split('/')[-1], [str(d) for d in deps]))
if any('ABP_Customer' in str(d) for d in deps):
    L('  OK — ABP_Customer 가 새 애님을 참조한다')
else:
    fails.append('ABP_Customer 가 Walk_Fwd_Manny 를 참조하지 않는다')

old_deps = EAL.find_package_referencers_for_asset(OLD, False) or []
L('  %s 를 아직 참조하는 패키지: %s' % (OLD.split('/')[-1], [str(d) for d in old_deps]))
if any('ABP_Customer' in str(d) for d in old_deps):
    fails.append('ABP_Customer 가 아직 옛 Walking 을 참조한다 — 교체가 안 됐다')

# ---- ② 이동 속도 ----
L('')
L('=== ② BP_Customer 이동 값 ===')
bp = EAL.load_asset(BP)
mv = unreal.get_default_object(bp.generated_class()).get_editor_property('character_movement')
want = {'max_walk_speed': 183.0, 'max_acceleration': 732.0,
        'braking_deceleration_walking': 1098.0}
for p, w in want.items():
    got = mv.get_editor_property(p)
    tag = 'OK' if abs(got - w) < 0.5 else '<-- ⚠ 기대 %.1f' % w
    L('  %-30s %8.1f  %s' % (p, got, tag))
    if abs(got - w) >= 0.5:
        fails.append('%s 가 %.1f 다 (기대 %.1f)' % (p, got, w))

# ---- ③ 새 애님 ----
L('')
L('=== ③ Walk_Fwd_Manny ===')
a = EAL.load_asset(NEW)
dur = a.get_editor_property('sequence_length')
sk = a.get_editor_property('skeleton')
h0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', 0.0, False)
h1 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', dur - 1e-4, False)
drift = ((h1.translation.x - h0.translation.x) ** 2
         + (h1.translation.y - h0.translation.y) ** 2) ** 0.5
moved = 0.0
for bone in ('Hips', 'Spine', 'LeftUpLeg', 'LeftLeg', 'RightUpLeg', 'RightLeg'):
    vals = []
    for i in range(13):
        r = unreal.AnimationLibrary.get_bone_pose_for_time(
            a, bone, min(dur * i / 12.0, dur - 1e-4), False).rotation.rotator()
        vals.append((r.pitch, r.yaw, r.roll))
    moved += max(max(v[k] for v in vals) - min(v[k] for v in vals) for k in range(3))

L('  길이 %.3fs · 스켈레톤 %s' % (dur, sk.get_name() if sk else '(없음)'))
L('  움직임 %.0f도 · 골반Z %.1f · 전진 %.2fcm' % (moved, h0.translation.z, drift))
if not sk or sk.get_name() != 'Hip_Hop_Dancing_Skeleton':
    fails.append('새 애님의 스켈레톤이 손님 것이 아니다')
if moved < 5.0:
    fails.append('새 애님이 비어 있다 (움직임 %.0f도)' % moved)
if drift > 5.0:
    fails.append('새 애님이 제자리가 아니다 (%.1fcm 전진)' % drift)
if h0.translation.z < 30.0:
    fails.append('새 애님의 골반이 바닥에 붙었다 (Z %.1f)' % h0.translation.z)

# ---- ④ 애님 길이 대 캡슐 속도 ----
L('')
L('=== ④ 발 미끄러짐 검산 ===')
L('  애님 보행 속도  183.0 cm/s   (리타겟 전 골반 전진 195.24cm / %.3fs)' % dur)
L('  MaxWalkSpeed    %.1f cm/s' % mv.get_editor_property('max_walk_speed'))
gap = abs(mv.get_editor_property('max_walk_speed') - 183.0) / 183.0 * 100.0
L('  차이 %.1f%%   (0 에 가까울수록 발이 안 미끄러진다)' % gap)

L('')
if fails:
    E('=== 실패 %d건 ===' % len(fails))
    for f in fails:
        E('  - %s' % f)
else:
    L('=== 전부 통과 ===')
    L('⚠ 남은 것은 **눈으로** 봐야 한다 — 팔 각도·발 접지. 어긋나면')
    L('  RTG_MannyWalk 를 열어 Edit Pose 로 맞출 것.')
