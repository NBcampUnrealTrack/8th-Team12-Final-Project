"""마네퀸 `A_WalkFwd_Loop` 를 손님(Mixamo) 스켈레톤으로 리타겟한다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_walk_retarget.py" -unattended -nopause -nosplash

`ll_eat_retarget.py` 와 같은 뼈대다. 먹는 애님과 **다른 점만** 아래에 적는다.

## 전진을 어디에 싣느냐 — 먹는 애님에는 없던 문제

`A_WalkFwd_Loop` 는 **`root` 본에 179.39cm** 를 싣고 간다 (1.067s -> 168.18 cm/s).
`pelvis` 는 제자리다. `EnableRootMotion=True · ForceRootLock=True` 라, 전진은
캡슐이 가져가고 화면에는 제자리걸음이 나온다.

Mixamo 스켈레톤에는 **`root` 본이 없다** — `hips` 자체가 루트다. 그래서 리타겟
결과에 전진을 실으면 그게 `hips` 로 들어가는데, `ABP_Customer` 의 루트모션
모드가 `RootMotionFromMontagesOnly` 라 **아무도 그걸 캡슐로 뽑아가지 않는다.**
메시만 앞으로 흘러가고 루프마다 제자리로 튄다.

-> **제자리로 만든다** (`Root Motion` op 끔). 전진은 CharacterMovement 가 하고,
   `MaxWalkSpeed` 를 애님 보폭에 맞춘다 (`ll_walk_stride.py` 로 잰다).

⚠⚠ 기존 `Walking` 은 이 규약을 **안 지키고 있다** — 골반에 279.64cm 가 실려
  있는데 `ForceRootLock` 도 꺼져 있다. 즉 지금 손님들은 걸을 때 메시가 캡슐보다
  앞서 나갔다가 2.87초마다 되돌아온다. 교체하면 그것도 같이 사라진다.

## 조정 가능한 두 값 — 재고 나서 정한다

`BLEND_TO_SOURCE` 가 0 이면 타깃의 레퍼런스 포즈 높이를 기준으로 골반을 움직이고,
1 이면 소스의 절대 위치를 그대로 가져온다. 먹는 애님은 1 이 필요했지만(좌면
높이를 그대로 써야 했다) 걷기는 **0 이 맞다** — 마네퀸 골반이 93.6, 손님은
101 이라 1 로 두면 손님이 7.5cm 가라앉는다.

⚠ 문서 §7 은 0 일 때 골반이 **0 이 되는** 경우를 적어 놨다. 그래서 되읽기가
  골반 Z 를 보고, 30 밑이면 실패로 잡는다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SRC_ANIM = '/Game/Characters/Mannequins/Anims/Unarmed/RetargetWalk/A_WalkFwd_Loop'
SRC_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
RTDIR = '/Game/BurgerHouse/Anims/Mixamo/Retarget'
OUTDIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
OUT_NAME = 'Walk_Fwd_Manny'
NAME = 'MannyWalk'

IK_MIXAMO = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/IK_Mixamo')
TGT_MESH = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing')

# ⚠ 걷기는 0 이 맞다 (모듈 주석 참고). 되읽기에서 골반 Z 가 30 밑이면 1 로 바꿀 것.
#   실측: 0 으로 두니 골반 Z 101.8 (손님 본래 높이 101.1 과 일치) — 맞다.
BLEND_TO_SOURCE = 0.0

# ⚠ 실측: 0 으로 두어도 hips yaw 가 +0.0 이다. 먹는 애님과 달리 보정이 필요 없다
#   (그쪽은 `blend_to_source_translation=1` 이라 소스 회전 규약이 같이 들어왔었다).
PELVIS_YAW_FIX = 0.0

# ⚠⚠ **제자리로 만드는 손잡이가 이것이다.**
#
# `Root Motion` op 을 꺼도 골반은 전진한다 — 소스 `root` 의 이동이 `pelvis` 의
# **글로벌 트랜스폼**에 이미 섞여 있고, Pelvis Motion op 이 그걸 타깃 `hips` 로
# 옮기기 때문이다. 실측으로 195.24cm 가 실려 나왔다.
#
# `scale_horizontal=0` 은 골반의 **수평 성분만** 없앤다. 상하 바운스
# (`scale_vertical=1`)와 다리 회전은 그대로라 걷는 그림은 유지된다.
#
# ⚠ 대신 좌우 흔들림(sway)도 같이 사라진다. Mixamo 의 「In Place」 내보내기와
#   같은 결과이고, 손님이 쓰는 다른 애님(`Idle` 전진 0.00cm)과 규약이 맞는다.
SCALE_HORIZONTAL = 0.0

# ⚠ 지우기 전에 **재 둘 것** — 이 값이 `MaxWalkSpeed` 의 근거다.
#   수평을 죽이기 전 실측 195.24cm / 1.067s = 183.0 cm/s (손님 체격에 맞게
#   스케일된 보행 속도). 마네퀸 원본은 168.18 cm/s, 체격비 101.8/93.6 = 1.088.
MEASURED_SPEED = 183.0

# 소스(마네퀸) 체인 — 이름이 TGT_CHAINS 와 **같아야** EXACT 매핑이 1:1 로 붙는다
CHAINS = [
    ('Spine',    'spine_01',   'spine_05'),
    ('Head',     'neck_01',    'head'),
    ('LeftArm',  'upperarm_l', 'hand_l'),
    ('RightArm', 'upperarm_r', 'hand_r'),
    ('LeftLeg',  'thigh_l',    'ball_l'),
    ('RightLeg', 'thigh_r',    'ball_r'),
]

# 타깃(Mixamo) — `ll_eat_retarget.py` 에서 검증된 값. 시작과 끝이 **달라야** 한다.
TGT_CHAINS = [
    ('Spine',    'Spine',      'Spine2'),
    ('Head',     'Neck',       'Head'),
    ('LeftArm',  'LeftArm',    'LeftHand'),
    ('RightArm', 'RightArm',   'RightHand'),
    ('LeftLeg',  'LeftUpLeg',  'LeftToeBase'),
    ('RightLeg', 'RightUpLeg', 'RightToeBase'),
]


def save(obj):
    EAL.save_asset(obj.get_path_name().split('.')[0], only_if_is_dirty=False)


def load_or_create(path, name, cls, factory):
    """⚠ 이미 있으면 `create_asset` 이 **None** 을 준다. 있으면 불러 쓴다."""
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    a = TOOLS.create_asset(name, RTDIR, cls, factory)
    if a is None:
        E('    %s 생성 실패' % name)
    return a


def fix_target_rig():
    """`IK_Mixamo` 체인을 제대로 된 **구간**으로 맞춘다 (있으면 시작·끝만 갈아 끼운다).

    ⚠ 지웠다 다시 넣으면 이 리그를 참조하는 **다른 리타게터의 매핑이 날아간다**
      — 먹는 애님 4종이 거기에 걸려 있다.
    """
    c = unreal.IKRigController.get_controller(IK_MIXAMO)
    have = {str(ch.get_editor_property('chain_name')) for ch in c.get_retarget_chains()}
    for cn, sb, eb in TGT_CHAINS:
        if cn not in have:
            c.add_retarget_chain(cn, sb, eb, 'None')
            continue
        c.set_retarget_chain_start_bone(cn, sb)
        c.set_retarget_chain_end_bone(cn, eb)
    c.set_retarget_root('Hips')
    save(IK_MIXAMO)
    L('  타깃 체인 %d개 · root=%s' % (len(c.get_retarget_chains()), c.get_retarget_root()))


def config_ops(rc, src_rig):
    """op 스택을 넣고 Pelvis Motion 을 설정한다.

    ⚠⚠ **op 이 0개면 리타겟은 타깃 레퍼런스 포즈를 그대로 뱉는다** — 에셋도
       생기고 스켈레톤·길이·트랙 수도 다 맞아서 어디서도 안 걸린다 (§7).
    """
    if rc.get_num_retarget_ops() == 0:
        rc.add_default_ops()
    n = rc.get_num_retarget_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)
    L('    op %d개: %s' % (n, ', '.join(str(rc.get_op_name(i)) for i in range(n))))

    # ⚠⚠ Root Motion op 을 끈다 — 모듈 주석의 「전진을 어디에 싣느냐」 참고.
    #    Mixamo 는 Hips 가 루트라 이 op 이 골반 이동을 빼내고 0 만 남긴다.
    for i in range(n):
        if str(rc.get_op_name(i)) == 'Root Motion' and rc.get_retarget_op_enabled(i):
            rc.set_retarget_op_enabled(i, False)
            L('    Root Motion op 끔 — 제자리 애님으로 만든다')

    for i in range(n):
        if str(rc.get_op_name(i)) != 'Pelvis Motion':
            continue
        oc = rc.get_op_controller(i)
        if not oc or not hasattr(oc, 'get_settings'):
            E('    Pelvis Motion op 설정을 못 얻는다')
            return n
        s = oc.get_settings()
        s.set_editor_property('blend_to_source_translation', BLEND_TO_SOURCE)
        s.set_editor_property('blend_to_source_translation_weights',
                              unreal.Vector(BLEND_TO_SOURCE, BLEND_TO_SOURCE, BLEND_TO_SOURCE))
        s.set_editor_property('rotation_offset_global',
                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=PELVIS_YAW_FIX))
        # ⚠⚠ 제자리로 만드는 손잡이 — 모듈 상단 SCALE_HORIZONTAL 주석 참고
        s.set_editor_property('scale_horizontal', SCALE_HORIZONTAL)
        oc.set_settings(s)
        got = oc.get_settings()
        L('    Pelvis Motion · blend_to_source=%.1f · yaw %+.0f · scale_horizontal=%.1f'
          % (got.get_editor_property('blend_to_source_translation'),
             got.get_editor_property('rotation_offset_global').yaw,
             got.get_editor_property('scale_horizontal')))
        if got.get_editor_property('scale_horizontal') != SCALE_HORIZONTAL:
            E('    ⚠ scale_horizontal 반영이 안 됐다 — 골반이 전진할 것이다')
        break
    return n


def map_chains(rc):
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    missing = [cn for cn, _, _ in TGT_CHAINS
               if str(rc.get_source_chain(cn) or 'None') in ('None', '')]
    if missing:
        E('    ⚠ 매핑이 안 붙은 체인: %s' % ', '.join(missing))
    else:
        L('    체인 매핑 %d개 확인' % len(TGT_CHAINS))
    return not missing


def build(src_mesh):
    rig = load_or_create('%s/IK_%s' % (RTDIR, NAME), 'IK_%s' % NAME,
                         unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    if rig is None:
        return None
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(src_mesh)
    have = {str(ch.get_editor_property('chain_name')) for ch in c.get_retarget_chains()}
    for cn, sb, eb in CHAINS:
        if cn not in have:
            c.add_retarget_chain(cn, sb, eb, 'None')
    c.set_retarget_root('pelvis')
    save(rig)
    L('    소스 리그: 체인 %d · root=%s' % (len(c.get_retarget_chains()), c.get_retarget_root()))

    rtg = load_or_create('%s/RTG_%s' % (RTDIR, NAME), 'RTG_%s' % NAME,
                         unreal.IKRetargeter, unreal.IKRetargetFactory())
    if rtg is None:
        return None
    rc = unreal.IKRetargeterController.get_controller(rtg)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)

    # ⚠ **순서** — op 이 먼저다. 매핑이 op 안에 들어 있어서 op 없이 auto_map_chains
    #   를 부르면 조용히 아무것도 안 붙는다.
    n_ops = config_ops(rc, rig)

    # ⚠ 마네퀸은 A포즈, Mixamo 는 T포즈다. 정렬 없이 리타겟하면 팔이 45도 틀어진다.
    for who in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            rc.auto_align_all_bones(who)
        except Exception as ex:
            L('    auto_align 건너뜀: %s' % str(ex)[:70])
    map_chains(rc)

    # ⚠ 이 판본의 `run_op_initial_setup` 은 **op 하나씩** 받는다 (`op_index` 필수).
    #   인자 없이 부르면 TypeError 로 조용히 건너뛰어져, 각 op 이 「소스·타깃
    #   리타겟 포즈의 차이」를 못 잡은 채로 돈다.
    done = 0
    for i in range(n_ops or 0):
        try:
            rc.run_op_initial_setup(i)
            done += 1
        except Exception as ex:
            E('    run_op_initial_setup(%d) 실패: %s' % (i, str(ex)[:80]))
    L('    run_op_initial_setup %d/%d' % (done, n_ops or 0))

    # ⚠⚠ 초기 설정이 Pelvis Motion 값을 **되돌릴 수 있다.** 그래서 뒤에서 한 번 더
    #   박고 되읽어 확인한다. (앞에서만 넣으면 scale_horizontal 이 1 로 돌아가
    #   골반이 다시 전진하는데, 로그에는 0 으로 찍혀 있어 오독하기 쉽다.)
    config_ops(rc, rig)

    save(rtg)
    return rtg


def set_assets(i, anim):
    """`assets_to_retarget` 의 배열 타입이 판본마다 달라 하나씩 시도한다."""
    path = anim.get_path_name()
    cands = []
    try:
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        ad = reg.get_assets_by_package_name(anim.get_outermost().get_name())[0]
        a = unreal.Array(unreal.AssetData)
        a.append(ad)
        cands.append(('Array[AssetData]', a))
        cands.append(('list[AssetData]', [ad]))
    except Exception as ex:
        L('    AssetData 준비 실패: %s' % str(ex)[:70])
    try:
        a = unreal.Array(unreal.SoftObjectPath)
        a.append(unreal.SoftObjectPath(path))
        cands.append(('Array[SoftObjectPath]', a))
    except Exception:
        pass
    cands.append(('list[str]', [path]))
    for label, v in cands:
        try:
            i.set_editor_property('assets_to_retarget', v)
            L('    assets_to_retarget <- %s' % label)
            return True
        except Exception:
            continue
    E('    assets_to_retarget — 어떤 형태도 안 먹는다')
    return False


def run(rtg, src_mesh, anim):
    i = unreal.IKRetargetBatchOperationInputs()
    if not set_assets(i, anim):
        return None
    i.set_editor_property('source_mesh', src_mesh)
    i.set_editor_property('target_mesh', TGT_MESH)
    i.set_editor_property('ik_retarget_asset', rtg)
    i.set_editor_property('search', 'A_WalkFwd_Loop')
    i.set_editor_property('replace', OUT_NAME)
    i.set_editor_property('use_source_path', False)
    i.set_editor_property('overwrite_existing_files', True)
    for tp in (OUTDIR, unreal.DirectoryPath(OUTDIR)):
        try:
            i.set_editor_property('target_path', tp)
            break
        except Exception:
            continue
    out = unreal.IKRetargetBatchOperation.run_batch_retarget(i)

    # ⚠⚠ 배치는 메모리에만 만든다. **저장 안 하면 커맨드릿 종료와 함께 사라진다.**
    saved = 0
    for o in (out or []):
        try:
            if isinstance(o, str):
                p = o
            elif hasattr(o, 'get_editor_property'):
                p = str(o.get_editor_property('package_name'))
            else:
                p = o.get_path_name()
            EAL.save_asset(str(p).split('.')[0], only_if_is_dirty=False)
            saved += 1
        except Exception as ex:
            E('    저장 실패 %r: %s' % (o, str(ex)[:90]))
    L('    저장 %d개' % saved)
    return out


L('=== 타깃 리그 (IK_Mixamo) ===')
fix_target_rig()
L('')
L('=== RTG_%s 만들기 ===' % NAME)
src_mesh = EAL.load_asset(SRC_MESH)
anim = EAL.load_asset(SRC_ANIM)
if not (src_mesh and anim):
    E('  소스 메시/애님이 없다 — %s / %s' % (SRC_MESH, SRC_ANIM))
else:
    rtg = build(src_mesh)
    if rtg:
        L('    run_batch_retarget -> %r' % (run(rtg, src_mesh, anim),))

# ---- 되읽기 ----
#
# ⚠⚠ **스켈레톤만 보면 안 된다.** 빈 애님도 스켈레톤·길이·트랙 수가 전부 맞는다.
PROBE = ('Hips', 'Spine', 'LeftUpLeg', 'LeftLeg', 'RightUpLeg', 'RightLeg')
MIN_AMP = 5.0

L('')
L('--- 되읽기 ---')
out_path = '%s/%s' % (OUTDIR, OUT_NAME)
if not EAL.does_asset_exist(out_path):
    E('  %s 가 안 만들어졌다' % out_path)
else:
    a = EAL.load_asset(out_path)
    dur = a.get_editor_property('sequence_length')
    sk = a.get_editor_property('skeleton')

    moved = 0.0
    for bone in PROBE:
        vals = []
        for i in range(13):
            t = min(dur * i / 12.0, dur - 1e-4)
            p = unreal.AnimationLibrary.get_bone_pose_for_time(a, bone, t, False)
            r = p.rotation.rotator()
            vals.append((r.pitch, r.yaw, r.roll))
        moved += max(max(v[k] for v in vals) - min(v[k] for v in vals) for k in range(3))

    h0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', 0.0, False)
    h1 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', dur - 1e-4, False)
    drift = ((h1.translation.x - h0.translation.x) ** 2
             + (h1.translation.y - h0.translation.y) ** 2) ** 0.5
    hz = h0.translation.z
    hyaw = h0.rotation.rotator().yaw

    # ## ⚠⚠ 루트모션 플래그 — **리타게터가 소스에서 복사해 온다**
    #
    # 소스 `A_WalkFwd_Loop` 는 `EnableRootMotion=True · ForceRootLock=True` 다
    # (마네퀸은 `root` 본에 전진이 실려 있다). 그게 그대로 따라오면 —
    #
    #   `ABP_Customer` 가 `RootMotionFromMontagesOnly` 라 몽타주 루트모션을 뽑고
    #   -> CharacterMovement 가 **MaxWalkSpeed 를 무시하고** 루트모션으로 움직인다
    #   -> 그런데 이 애님은 제자리라 루트모션이 0 -> **거의 안 움직인다**
    #
    # ⚠ 전진량 0 은 「제자리로 잘 구워졌다」로 읽히지만, 플래그가 켜져 있으면
    #   **정반대 효과**가 난다. 그래서 둘을 같이 봐야 한다.
    #
    # ⚠ `ForceRootLock` 도 끈다 — Mixamo 는 `Hips` 가 루트라 고정하면 상하
    #   바운스와 골반 높이가 죽는다.
    rm = a.get_editor_property('enable_root_motion')
    lk = a.get_editor_property('force_root_lock')

    why = []
    if rm or lk:
        why.append('루트모션 플래그가 켜져 있다 (EnableRootMotion=%s · ForceRootLock=%s) '
                   '— 소스에서 복사된 것이다. 둘 다 False 라야 한다' % (rm, lk))
    if not sk or sk.get_name() != 'Hip_Hop_Dancing_Skeleton':
        why.append('스켈레톤')
    if moved < MIN_AMP:
        why.append('빈 애님')
    if hz < 30.0:
        why.append('골반이 바닥 (BLEND_TO_SOURCE 를 1 로)')
    if abs(hyaw) > 40.0:
        why.append('골반이 %+.0f도 돌아감 (PELVIS_YAW_FIX 조정)' % hyaw)
    if drift > 5.0:
        why.append('제자리가 아니다 — 골반이 %.1fcm 전진' % drift)

    L('  %-18s %.3fs · 스켈레톤 %s' % (OUT_NAME, dur, sk.get_name() if sk else '(없음)'))
    L('  움직임 %6.0f도 · 골반Z %5.1f · yaw %+6.1f · 전진 %5.2fcm' % (moved, hz, hyaw, drift))
    L('  EnableRootMotion=%s · ForceRootLock=%s  (둘 다 False 라야 한다)' % (rm, lk))
    L('  비교(기존 Walking): 골반Z 101.1 · 전진 279.64cm (= 제자리가 아니다)')
    if why:
        E('  ⚠ %s' % ' · '.join(why))
    else:
        L('  === OK — 제자리로 움직이는 손님 걷기 애님 ===')
        L('  ⚠ 자세는 **눈으로 봐야 한다.** 어긋나면 RTG_%s 를 열어 Edit Pose 로 맞출 것.' % NAME)
