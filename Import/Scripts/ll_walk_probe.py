"""Walk 애님 교체 전 **사실 확인**. 아무것도 안 고친다.

⚠ 추측으로 리타겟 설정을 정하면 §7 의 「빈 애님」이 반복된다. 먼저 잰다.

## 이 판본(UE 5.8)에서 **없는** API — 헛짚었던 것들

    AnimationLibrary.extract_root_motion        없다
    BlueprintEditorLibrary.get_all_graph_nodes  없다  -> AnimGraphService 를 쓴다
    skeleton.bone_tree[0]                       BoneNode **구조체**라 이름이 아니다
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

# 루트 후보 — 스켈레톤마다 다르다. 마네퀸은 root/pelvis, Mixamo 는 Hips.
ROOTS = ('root', 'Hips', 'pelvis', 'mixamorig:Hips')

CANDIDATES = [
    ('소스 후보',   '/Game/Characters/Mannequins/Anims/Unarmed/RetargetWalk/A_WalkFwd_Loop'),
    ('현재 손님',   '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking'),
    ('참고',        '/Game/BurgerHouse/Anims/Mixamo/Anims/Walking_While_Texting'),
    ('참고(앉기)',  '/Game/BurgerHouse/Anims/Mixamo/Anims/Idle'),
]


def track_names(seq):
    try:
        return [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(seq)]
    except Exception:
        return []


def pose(seq, bone, t):
    try:
        return unreal.AnimationLibrary.get_bone_pose_for_time(seq, bone, t, False)
    except Exception:
        return None


def probe(label, path):
    if not EAL.does_asset_exist(path):
        E('  %-12s %s  <-- 없다' % (label, path))
        return
    a = EAL.load_asset(path)
    sk = a.get_editor_property('skeleton')
    dur = a.get_editor_property('sequence_length')
    tracks = track_names(a)

    L('  %-12s %-26s  %s' % (label, path.split('/')[-1],
                             sk.get_name() if sk else '(스켈레톤 없음)'))
    L('      길이 %.3fs · 트랙 %d개 · EnableRootMotion=%s · RootLock=%s'
      % (dur, len(tracks), a.get_editor_property('enable_root_motion'),
         a.get_editor_property('root_motion_root_lock')))
    L('      트랙[0:4] %s' % tracks[:4])

    # 어느 본이 실제로 전진하는지 — 루트 후보를 전부 재 본다
    for bone in ROOTS:
        if tracks and bone not in tracks:
            continue
        p0, p1 = pose(a, bone, 0.0), pose(a, bone, max(dur - 0.001, 0.0))
        if not (p0 and p1):
            continue
        t0, t1 = p0.translation, p1.translation
        d = ((t1.x - t0.x) ** 2 + (t1.y - t0.y) ** 2) ** 0.5
        L('      %-16s 전진 %7.2fcm -> %7.2f cm/s   (Z %.1f -> %.1f · yaw %+.1f)'
          % (bone, d, d / dur if dur else 0.0, t0.z, t1.z,
             p0.rotation.rotator().yaw))
    L('')


L('=== 애님 실측 ===')
for lb, p in CANDIDATES:
    probe(lb, p)

# ---- ABP_Customer 의 상태 기계 ----
L('=== ABP_Customer 상태 기계 ===')
ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
abp = EAL.load_asset(ABP)
tgt = abp.get_editor_property('target_skeleton') if abp else None
L('  타깃 스켈레톤: %s' % (tgt.get_name() if tgt else '(없음)'))
try:
    for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
        L('  [%s] 상태 %s개' % (m.machine_name, m.state_count))
        for s in unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or []:
            L('      - %-20s (%s)' % (s.state_name, s.state_type))
except Exception as ex:
    E('  AnimGraphService 실패: %s' % str(ex)[:200])

# ---- BP_Customer 이동 값 ----
L('')
L('=== BP_Customer 이동 값 ===')
bp = EAL.load_asset('/Game/BurgerHouse/BP/BP_Customer')
if bp:
    try:
        cdo = unreal.get_default_object(bp.generated_class())
        mv = cdo.get_editor_property('character_movement')
        for p in ('max_walk_speed', 'max_acceleration', 'braking_deceleration_walking',
                  'orient_rotation_to_movement', 'rotation_rate', 'max_step_height'):
            try:
                L('  %-32s %s' % (p, mv.get_editor_property(p)))
            except Exception as ex:
                E('  %-32s 읽기 실패 %s' % (p, str(ex)[:50]))
    except Exception as ex:
        E('  CharacterMovement 를 못 읽는다: %s' % str(ex)[:150])

L('')
L('=== 프로브 끝 — 아무것도 안 고쳤다 ===')
