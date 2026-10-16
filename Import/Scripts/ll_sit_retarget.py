"""마네퀸 `SitIdle_Male` 을 손님(Mixamo) 스켈레톤으로 리타겟한다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_sit_retarget.py" -unattended -nopause -nosplash

프리필(장식) 손님이 돌리는 `Sit_Idle` 풀을 먹는 애님 4종에서 이것 하나로
바꾸기 위한 재료다. 몽타주와 배선은 `ll_montages.py` 가 한다.

## 소스 — `Characters/ChairAnimation/Male/SitIdle_Male`

같은 이름이 **세 벌** 있다. 스켈레톤이 다르다.

| 경로 | 스켈레톤 | 골반 Z |
|---|---|---|
| `Characters/ChairAnimation/Male/SitIdle_Male` | **SK_Mannequin** | 57.9 |
| `MetaHumans/MHC_Character_Adam/Animation/SitIdle_Male` | metahuman_base_skel | 53.5 |
| `MetaHumans/MHC_Character_Adam/Cheon/Anim/SitIdle_Male` | Cheon_Skeleton | (root 0) |

마네퀸 것을 쓴다 — `RTG_MannyWalk` 에서 **이미 검증된 소스 리그**(`IK_MannyWalk`)를
그대로 재사용할 수 있고, 리타겟 경로가 하나로 모인다.

⚠ 골반 Z 57.9 는 먹는 애님(57.4)과 사실상 같다. 좌면 높이가 맞으므로 의자에
  그대로 앉는다.

## ⚠⚠ 걷기와 **반대로** 설정한다

| | 걷기 | 앉기(이것) | 왜 |
|---|---|---|---|
| `blend_to_source_translation` | 0.0 | **1.0** | 앉은 동작은 **좌면 높이가 곧 정답**이다. 0 으로 두면 타깃 레퍼런스 포즈(선 자세 ~101)를 기준으로 삼아 손님이 **의자 위에 서 버린다** |
| `rotation_offset_global.yaw` | 0 | **0** | 아래 ⚠⚠ 참고 — `ll_eat_retarget.py` 의 +90 을 그대로 가져오면 **과보정**이다 |
| `scale_horizontal` | 0.0 | **1.0** | 제자리 동작이라 죽일 전진이 없다. 0 으로 두면 좌면 **앞뒤 위치**까지 뭉갠다 |

⚠ `Root Motion` op 은 **양쪽 다 끈다** — Mixamo 는 `Hips` 가 루트라 이 op 이
  골반 이동을 빼내고 0 만 남긴다 (`ll_eat_retarget.py` §7).

## ⚠⚠ 골반 yaw 보정은 **+90 이 아니라 0 이다** — 먹는 애님과 다르다

`blend_to_source_translation = 1` 이니 먹는 애님처럼 `+90` 이 필요할 줄 알았는데,
넣고 재 보니 골반 yaw 가 **+82.6** 이었다 (기준: 화면 확인된 `Eating_a_Burger`
+2.1). 즉 보정이 통째로 과했다. 0 으로 두면 **−7.4** 로 기준과 10도 안이다.

왜 다른가 — 먹는 애님은 **팩마다 딸려온 제 스켈레톤**(`Eating_a_Burger_Skeleton`
161본)을 쓰고 그때 소스 리그를 급조했다. 여기는 `IK_MannyWalk` 를 재사용하는데
그 리그는 `auto_align_all_bones` + `run_op_initial_setup` 을 제대로 거쳐 회전
델타가 이미 잡혀 있다. **거기에 +90 을 더하니 두 번 돌아간 것이다.**

⚠ 다른 애님을 이 스크립트로 옮길 때도 **값을 물려받지 말고 재서 정할 것.**
  되읽기가 기준(`Eating_a_Burger`)과 나란히 찍어 주므로 비교하면 된다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SRC_ANIM = '/Game/Characters/ChairAnimation/Male/SitIdle_Male'
SRC_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'
RTDIR = '/Game/BurgerHouse/Anims/Mixamo/Retarget'
OUTDIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
OUT_NAME = 'SitIdle_Male'
NAME = 'MannySit'

# ⚠ 걷기가 만든 소스 리그를 그대로 쓴다 — 같은 SK_Mannequin 이다.
#   체인 구성은 `ll_walk_retarget.py` 의 CHAINS 와 같고 검증돼 있다.
SRC_RIG = '%s/IK_MannyWalk' % RTDIR

IK_MIXAMO = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/IK_Mixamo')
TGT_MESH = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing')

# 모듈 주석의 표 참고 — 걷기와 반대다
BLEND_TO_SOURCE = 1.0
PELVIS_YAW_FIX = 0.0        # ⚠ +90 은 과보정이다 — 모듈 주석의 ⚠⚠ 참고
SCALE_HORIZONTAL = 1.0

TGT_CHAINS = ['Spine', 'Head', 'LeftArm', 'RightArm', 'LeftLeg', 'RightLeg']

# 검산 기준
SIT_Z_MIN, SIT_Z_MAX = 45.0, 75.0     # 앉은 골반 높이 (먹는 애님 57.4 · 소스 57.9)
AMP_RATIO_MIN = 0.30                  # 소스 대비 이만큼은 움직여야 한다


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


def config_ops(rc, src_rig):
    """op 스택을 넣고 Pelvis Motion 을 **앉은 동작용**으로 맞춘다.

    ⚠⚠ op 이 0개면 리타겟은 타깃 레퍼런스 포즈를 그대로 뱉는다 — 에셋도 생기고
       스켈레톤·길이·트랙 수도 다 맞아서 어디서도 안 걸린다.
    """
    if rc.get_num_retarget_ops() == 0:
        rc.add_default_ops()
    n = rc.get_num_retarget_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)

    for i in range(n):
        if str(rc.get_op_name(i)) == 'Root Motion' and rc.get_retarget_op_enabled(i):
            rc.set_retarget_op_enabled(i, False)
            L('    Root Motion op 끔 (Mixamo 는 Hips 가 루트라 골반이 0 이 된다)')

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
                              unreal.Vector(BLEND_TO_SOURCE, BLEND_TO_SOURCE,
                                            BLEND_TO_SOURCE))
        s.set_editor_property('rotation_offset_global',
                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=PELVIS_YAW_FIX))
        s.set_editor_property('scale_horizontal', SCALE_HORIZONTAL)
        oc.set_settings(s)
        got = oc.get_settings()
        L('    Pelvis Motion · blend_to_source=%.1f · yaw %+.0f · scale_h=%.1f'
          % (got.get_editor_property('blend_to_source_translation'),
             got.get_editor_property('rotation_offset_global').yaw,
             got.get_editor_property('scale_horizontal')))
        if got.get_editor_property('blend_to_source_translation') < 0.99:
            E('    ⚠ 반영이 안 됐다 — 손님이 의자 위에 설 것이다')
        break
    return n


def map_chains(rc):
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    missing = [cn for cn in TGT_CHAINS
               if str(rc.get_source_chain(cn) or 'None') in ('None', '')]
    if missing:
        E('    ⚠ 매핑이 안 붙은 체인: %s' % ', '.join(missing))
    else:
        L('    체인 매핑 %d개 확인' % len(TGT_CHAINS))
    return not missing


def build(src_mesh):
    rig = EAL.load_asset(SRC_RIG)
    if not rig:
        E('  %s 가 없다 — ll_walk_retarget.py 를 먼저 돌릴 것' % SRC_RIG)
        return None
    c = unreal.IKRigController.get_controller(rig)
    L('    소스 리그 재사용: %s (체인 %d · root=%s)'
      % (rig.get_name(), len(c.get_retarget_chains()), c.get_retarget_root()))

    rtg = load_or_create('%s/RTG_%s' % (RTDIR, NAME), 'RTG_%s' % NAME,
                         unreal.IKRetargeter, unreal.IKRetargetFactory())
    if rtg is None:
        return None
    rc = unreal.IKRetargeterController.get_controller(rtg)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)

    # ⚠ 순서 — op 이 먼저다. 매핑이 op 안에 들어 있다.
    n_ops = config_ops(rc, rig)

    # ⚠ 마네퀸 A포즈 / Mixamo T포즈 정렬
    for who in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            rc.auto_align_all_bones(who)
        except Exception as ex:
            L('    auto_align 건너뜀: %s' % str(ex)[:70])
    map_chains(rc)

    # ⚠ 이 판본의 `run_op_initial_setup` 은 **op 하나씩** 받는다.
    done = 0
    for i in range(n_ops or 0):
        try:
            rc.run_op_initial_setup(i)
            done += 1
        except Exception as ex:
            E('    run_op_initial_setup(%d) 실패: %s' % (i, str(ex)[:80]))
    L('    run_op_initial_setup %d/%d' % (done, n_ops or 0))

    # ⚠⚠ 초기 설정이 Pelvis Motion 값을 되돌린다 — 뒤에서 한 번 더 박는다.
    config_ops(rc, rig)

    save(rtg)
    return rtg


def set_assets(i, anim):
    path = anim.get_path_name()
    cands = []
    try:
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        ad = reg.get_assets_by_package_name(anim.get_outermost().get_name())[0]
        a = unreal.Array(unreal.AssetData)
        a.append(ad)
        cands.append(('Array[AssetData]', a))
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
    # 이름이 그대로라 search/replace 는 비운다 — 폴더가 달라 안 겹친다
    i.set_editor_property('search', '')
    i.set_editor_property('replace', '')
    i.set_editor_property('use_source_path', False)
    i.set_editor_property('overwrite_existing_files', True)
    for tp in (OUTDIR, unreal.DirectoryPath(OUTDIR)):
        try:
            i.set_editor_property('target_path', tp)
            break
        except Exception:
            continue
    out = unreal.IKRetargetBatchOperation.run_batch_retarget(i)

    # ⚠⚠ 배치는 메모리에만 만든다. 저장 안 하면 커맨드릿 종료와 함께 사라진다.
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


def amplitude(seq, bones):
    """본들의 회전 변화폭 합(도). **빈 애님**을 잡는 유일한 검사다."""
    dur = seq.get_editor_property('sequence_length')
    total = 0.0
    for bone in bones:
        vals = []
        for i in range(17):
            t = min(dur * i / 16.0, dur - 1e-4)
            try:
                r = unreal.AnimationLibrary.get_bone_pose_for_time(
                    seq, bone, t, False).rotation.rotator()
            except Exception:
                return -1.0
            vals.append((r.pitch, r.yaw, r.roll))
        total += max(max(v[k] for v in vals) - min(v[k] for v in vals) for k in range(3))
    return total


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
L('')
L('--- 되읽기 ---')
out_path = '%s/%s' % (OUTDIR, OUT_NAME)
if not EAL.does_asset_exist(out_path):
    E('  %s 가 안 만들어졌다' % out_path)
else:
    src = EAL.load_asset(SRC_ANIM)
    a = EAL.load_asset(out_path)
    dur = a.get_editor_property('sequence_length')
    sk = a.get_editor_property('skeleton')

    # ⚠ 앉은 idle 은 원래 움직임이 작다. 절대값 기준을 두면 정상인데 실패로
    #   잡거나 그 반대가 된다. **소스 대비 비율**로 본다.
    src_amp = amplitude(src, ('pelvis', 'spine_01', 'upperarm_l', 'upperarm_r',
                              'thigh_l', 'thigh_r'))
    out_amp = amplitude(a, ('Hips', 'Spine', 'LeftArm', 'RightArm',
                            'LeftUpLeg', 'RightUpLeg'))
    ratio = out_amp / src_amp if src_amp > 0 else -1.0

    h0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', 0.0, False)
    hz, hyaw = h0.translation.z, h0.rotation.rotator().yaw

    # 비교군 — 이미 화면에서 확인된 앉은 애님
    ref = EAL.load_asset('%s/Eating_a_Burger' % OUTDIR)
    if ref:
        r0 = unreal.AnimationLibrary.get_bone_pose_for_time(ref, 'Hips', 0.0, False)
        L('  비교(Eating_a_Burger, 화면 확인됨): 골반Z %.1f · yaw %+.1f'
          % (r0.translation.z, r0.rotation.rotator().yaw))

    L('  %-16s %.3fs · 스켈레톤 %s' % (OUT_NAME, dur, sk.get_name() if sk else '(없음)'))
    L('  움직임 소스 %.0f도 -> 결과 %.0f도 (비율 %.2f) · 골반Z %.1f · yaw %+.1f'
      % (src_amp, out_amp, ratio, hz, hyaw))

    why = []
    if not sk or sk.get_name() != 'Hip_Hop_Dancing_Skeleton':
        why.append('스켈레톤이 손님 것이 아니다')
    if ratio < AMP_RATIO_MIN:
        why.append('빈 애님 (소스 대비 %.0f%%)' % (ratio * 100))
    if not (SIT_Z_MIN <= hz <= SIT_Z_MAX):
        why.append('골반 Z %.1f — 앉은 높이(%.0f~%.0f)가 아니다' % (hz, SIT_Z_MIN, SIT_Z_MAX))
    if abs(hyaw) > 40.0:
        why.append('골반이 %+.0f도 돌아감 — PELVIS_YAW_FIX 조정' % hyaw)

    if why:
        E('  ⚠ %s' % ' · '.join(why))
    else:
        L('  === OK — 앉은 높이로 움직이는 손님 idle ===')
        L('  다음: ll_montages.py 의 ACTIONS["Sit_Idle"] 을 이것으로 바꿔 돌린다')
