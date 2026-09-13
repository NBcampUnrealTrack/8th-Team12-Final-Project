"""Eat 팩 애님을 손님 스켈레톤으로 리타겟한다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_eat_retarget.py" -unattended -nopause -nosplash

## 왜 리타겟이 필요한가

Eat 팩 애님은 **UE 마네퀸 리그(161본 · `pelvis`·`spine_01`)** 로 만들어졌고,
손님은 **Mixamo 리그(65본 · `mixamorig:*`)** 를 쓴다. 이름도 개수도 달라서
그대로는 재생이 안 된다 — 몽타주를 만들어도 스켈레톤 불일치로 거부된다.

## ⚠ 만든 에셋은 **반드시 저장한다**

커맨드릿은 종료할 때 저장 안 된 에셋을 그냥 버린다. 만들고 저장을 빠뜨리면
다음 실행에서 「없다」가 되는데, 원인이 안 보여 한참 헤맨다.

## ⚠ A포즈 / T포즈

마네퀸은 A포즈, Mixamo 는 T포즈다. 정렬 없이 리타겟하면 팔이 45도 틀어진다.
`auto_align_all_bones` 가 그걸 맞춘다.

⚠ **결과가 제대로 보이는지는 눈으로 봐야 한다.** 자동 정렬은 대개 맞지만
  손에 물건을 든 애님(버거·감자튀김)은 손 위치가 어긋나기 쉽다. 어긋나면
  `RTG_*` 를 열어 Edit Pose 로 맞춘다.

## ⚠⚠ 이 스크립트가 한 번 **조용히 빈 애님을 만들었다**

증상 — 에셋은 생기고, 스켈레톤도 맞고, 길이도 맞고, 트랙 65개도 다 있는데
**모든 본의 움직임이 0** 이었다. 몽타주도 정상으로 만들어지고 `Montage_Play`
도 성공을 보고한다. 화면에만 아무 일이 없다.

원인이 둘 겹쳐 있었다 —

**① 타깃 리그 `IK_Mixamo` 의 체인이 전부 1본짜리였다.** `Spine -> Spine`,
`LeftArm -> LeftArm` 처럼 **체인 이름을 본 이름 자리에 넣은** 실수다. Mixamo
에 마침 `Spine`·`LeftArm` 이라는 본이 실제로 있어서 오류가 안 났다.

**② `auto_map_chains` 가 안 붙었다.** 양쪽 체인 이름이 같은데도 매핑이 전부
`None` 이었다. 그래서 아래에서 **`set_source_chain` 으로 직접 박는다.**

⚠ 그리고 예전 되읽기는 **스켈레톤만** 봤다. 그게 통과해서 빈 애님이 그대로
  넘어갔다. 지금은 **본이 실제로 움직이는지**까지 잰다 (`amp`).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

PACK = '/Game/BurgerHouse/Anims/Eat_Animation_Pack'
RTDIR = PACK + '/Retarget'
OUTDIR = '/Game/BurgerHouse/Anims/Mixamo/Anims'
IK_MIXAMO = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/IK_Mixamo')
TGT_MESH = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing')

# 소스(마네퀸) 체인 — 이름은 아래 TGT_CHAINS 와 **같아야** 매핑이 1:1 로 붙는다
CHAINS = [
    ('Spine',    'spine_01',   'spine_05'),
    ('Head',     'neck_01',    'head'),
    ('LeftArm',  'upperarm_l', 'hand_l'),
    ('RightArm', 'upperarm_r', 'hand_r'),
    ('LeftLeg',  'thigh_l',    'ball_l'),
    ('RightLeg', 'thigh_r',    'ball_r'),
]

# 타깃(Mixamo) 체인 — **시작과 끝이 달라야 한다.**
#
# ⚠⚠ 여기가 예전에 `('Spine', 'Spine', 'Spine')` 처럼 들어가 있었다. Mixamo 에
#   `Spine`·`Head`·`LeftArm` 이라는 본이 **실제로 있어서** 오류 없이 통과했고,
#   1본짜리 체인이라 리타겟 결과가 사실상 안 움직였다.
TGT_CHAINS = [
    ('Spine',    'Spine',      'Spine2'),
    ('Head',     'Neck',       'Head'),
    ('LeftArm',  'LeftArm',    'LeftHand'),
    ('RightArm', 'RightArm',   'RightHand'),
    ('LeftLeg',  'LeftUpLeg',  'LeftToeBase'),
    ('RightLeg', 'RightUpLeg', 'RightToeBase'),
]

NAMES = ['Eating_a_Burger', 'Eating_a_Drumstick', 'Eating_Frantically', 'Eating_Fries']

# 골반 방향 보정(도). `fix_pelvis_op` 주석 참고 — 0 으로 두면 손님이 옆을 본다.
PELVIS_YAW_FIX = 90.0


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
    """`IK_Mixamo` 의 체인을 제대로 된 **구간**으로 고친다.

    ⚠ 이미 있는 체인을 지우고 다시 넣지 않고 **시작·끝만 갈아 끼운다.** 지웠다
      넣으면 이 리그를 참조하는 리타게터의 매핑이 같이 날아간다.
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
    for ch in c.get_retarget_chains():
        L('    타깃 체인 %-10s %-12s -> %-12s'
          % (ch.get_editor_property('chain_name'),
             ch.get_editor_property('start_bone').get_editor_property('bone_name'),
             ch.get_editor_property('end_bone').get_editor_property('bone_name')))


def ensure_ops(rc, src_rig):
    """⚠⚠ **op 스택이 비어 있으면 리타겟은 아무것도 안 한다.**

    UE 5.6+ 의 IK Retargeter 는 「Pelvis Motion → FK Chains → Run IK Rig →
    Root Motion → Remap Curves」 **연산 목록**으로 돈다. 그런데
    `IKRetargetFactory` 로 만든 새 리타게터는 **op 이 0개**다.

    그 상태에서도 리그 지정·정렬·체인 매핑은 전부 성공하고, 배치 리타겟도
    에셋을 정상적으로 만들어 낸다 — **내용만 타깃의 레퍼런스 포즈**다.
    스켈레톤도 길이도 트랙 수도 맞아서 어디서도 안 걸린다.

    ⚠ `auto_map_chains` 도 op 이 없으면 조용히 아무것도 매핑하지 않는다.
      (매핑은 op 안에 들어 있기 때문이다.) 그래서 **op 을 먼저** 넣는다.
    """
    if rc.get_num_retarget_ops() == 0:
        rc.add_default_ops()
    n = rc.get_num_retarget_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)
    L('    op %d개: %s' % (n, ', '.join(str(rc.get_op_name(i)) for i in range(n))))
    fix_pelvis_op(rc, n)
    return n > 0


def fix_pelvis_op(rc, n):
    """골반이 **바닥에 붙는 것**을 막는다.

    ## ⚠ 증상 — 골반 Z 가 0 이 된다

    기본 설정(`blend_to_source_translation = 0`)에서 Pelvis Motion op 은 소스의
    골반 **상대 운동**을 타깃의 리타겟 포즈 골반 높이에 얹는다. 그 기준 높이가
    안 잡히면 결과가 **0** 이 되고, 캐릭터가 바닥에 주저앉은 그림이 된다.

    ⚠ 회전은 멀쩡해서 「애님은 나오는데 위치만 이상하다」로 보인다. 몸통·팔은
      제대로 움직인다.

    소스와 타깃이 둘 다 사람 크기라 **소스 위치를 그대로 따라가는 것**이 맞다.
    앉은 동작이면 골반이 좌면 높이(57~63)에 와야 한다.
    """
    # ⚠⚠ **Root Motion op 을 끈다.**
    #
    # Mixamo 스켈레톤은 `root` 본이 따로 없고 **`Hips` 자체가 루트**다. 그래서
    # Root Motion op 이 골반의 이동을 「루트 모션」으로 빼내 버리고 포즈 트랙에는
    # **0 만 남긴다.** 결과가 바닥에 주저앉은 애님이 된다.
    #
    # ⚠ 회전은 멀쩡하므로 움직임 검사(`amp`)로는 안 걸린다. 골반 Z 를 봐야 한다.
    #
    # 손님 애님은 전부 제자리 동작이라 루트 모션이 필요 없다.
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
            return
        s = oc.get_settings()
        s.set_editor_property('blend_to_source_translation', 1.0)
        s.set_editor_property('blend_to_source_translation_weights',
                              unreal.Vector(1.0, 1.0, 1.0))

        # ⚠⚠ **골반 방향 보정.**
        #
        # 소스 위치를 그대로 따라가게 하면 **방향도 소스 규약으로 들어온다.**
        # 마네퀸 골반 정면은 `+X`, Mixamo `Hips` 는 `+Y` 라 90도 어긋난다 —
        # 손님이 옆을 보고 앉는다.
        #
        # 실측 — 보정 없이 리타겟한 Hips yaw 가 **-88~-89.5**, 정상 Mixamo 애님은
        # **+5 근처**(`Sitting__1_` 5.3 · `Idle` 5.5). 그래서 `+90` 을 준다.
        #
        # ⚠ `run_op_initial_setup()` 으로는 안 잡힌다 (해 봤다).
        s.set_editor_property('rotation_offset_global',
                              unreal.Rotator(roll=0.0, pitch=0.0,
                                             yaw=PELVIS_YAW_FIX))
        oc.set_settings(s)

        got = oc.get_settings()
        L('    Pelvis Motion · blend_to_source_translation=%.1f · yaw 보정 %+.0f'
          % (got.get_editor_property('blend_to_source_translation'),
             got.get_editor_property('rotation_offset_global').yaw))
        if got.get_editor_property('blend_to_source_translation') < 0.99:
            E('    ⚠ 반영이 안 됐다 — 골반이 바닥에 붙을 것이다')
        return
    E('    Pelvis Motion op 이 없다')


def map_chains(rc):
    """체인 매핑을 붙이고 **붙었는지 확인한다.**

    ⚠ 이름이 양쪽 다 같아도 확인 없이 넘어가면 안 된다 — op 이 없던 시절에는
      전부 `None` 이었는데 아무 오류도 안 났다.
    """
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    missing = []
    for cn, _, _ in TGT_CHAINS:
        got = rc.get_source_chain(cn)
        if not got or str(got) in ('None', ''):
            missing.append(cn)
    if missing:
        E('    ⚠ 매핑이 안 붙은 체인: %s' % ', '.join(missing))
    else:
        L('    체인 매핑 %d개 확인' % len(TGT_CHAINS))
    return not missing


def build(name, mesh):
    """소스 IK 리그 + 리타게터를 만들고 **저장한다.** 여러 번 돌려도 안전하다."""
    rig = load_or_create('%s/IK_%s' % (RTDIR, name), 'IK_%s' % name,
                         unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    if rig is None:
        return None
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    have = {str(ch.get_editor_property('chain_name')) for ch in c.get_retarget_chains()}
    for cn, sb, eb in CHAINS:
        if cn not in have:
            c.add_retarget_chain(cn, sb, eb, 'None')
    c.set_retarget_root('pelvis')
    save(rig)
    L('    소스 리그: 체인 %d · root=%s' % (len(c.get_retarget_chains()), c.get_retarget_root()))

    rtg = load_or_create('%s/RTG_%s' % (RTDIR, name), 'RTG_%s' % name,
                         unreal.IKRetargeter, unreal.IKRetargetFactory())
    if rtg is None:
        return None
    rc = unreal.IKRetargeterController.get_controller(rtg)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, IK_MIXAMO)

    # ⚠ **순서가 중요하다** — op 이 먼저다. 체인 매핑이 op 안에 들어 있어서
    #    op 없이 auto_map_chains 를 부르면 조용히 아무것도 안 붙는다.
    ensure_ops(rc, rig)

    for who in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            rc.auto_align_all_bones(who)
        except Exception as ex:
            L('    auto_align 건너뜀: %s' % str(ex)[:70])
    map_chains(rc)

    # ⚠⚠ **op 을 넣고 매핑한 뒤에는 초기 설정을 돌려야 한다.**
    #
    # 안 돌리면 각 op 이 「소스·타깃 리타겟 포즈의 차이」를 못 잡는다. 그러면
    # Pelvis Motion 이 소스 골반 회전을 **델타가 아니라 그대로** 얹어서,
    # 마네퀸 규약(정면 +X)이 Mixamo(정면 +Y)에 그대로 들어온다 →
    # **손님이 90도 옆을 보고 앉는다.**
    #
    # ⚠ 회전량·골반 높이는 멀쩡하게 나오므로 그 둘만 재는 검사로는 안 걸린다.
    #   그래서 되읽기가 **Hips yaw** 도 본다.
    try:
        rc.run_op_initial_setup()
        L('    run_op_initial_setup OK')
    except Exception as ex:
        E('    run_op_initial_setup 실패: %s' % str(ex)[:90])

    save(rtg)
    return rtg


def set_assets(i, anim):
    """`AssetsToRetarget` 가 어떤 배열 타입인지 판본마다 달라 하나씩 시도한다.

    ⚠ 그냥 `[anim]` 은 안 된다 — TSoftObjectPtr 배열이라 변환이 거부된다.
    """
    path = anim.get_path_name()
    cands = []
    # ⚠ 에디터의 배치 리타겟은 선택한 **AssetData** 로 돈다 — 이게 정답일 가능성이 높다
    try:
        reg = unreal.AssetRegistryHelpers.get_asset_registry()
        pkg = anim.get_outermost().get_name()          # /Game/.../Eating_a_Burger_Anim
        ad = reg.get_assets_by_package_name(pkg)[0]
        a = unreal.Array(unreal.AssetData); a.append(ad)
        cands.append(('Array[AssetData]', a))
        cands.append(('list[AssetData]', [ad]))
    except Exception as ex:
        L('    AssetData 준비 실패: %s' % str(ex)[:70])
    try:
        a = unreal.Array(unreal.SoftObjectPath); a.append(unreal.SoftObjectPath(path))
        cands.append(('Array[SoftObjectPath]', a))
    except Exception:
        pass
    try:
        a = unreal.Array(unreal.Object); a.append(anim)
        cands.append(('Array[Object]', a))
    except Exception:
        pass
    try:
        a = unreal.Array(unreal.AnimSequence); a.append(anim)
        cands.append(('Array[AnimSequence]', a))
    except Exception:
        pass
    cands.append(('list[str]', [path]))
    cands.append(('list[SoftObjectPath]', [unreal.SoftObjectPath(path)]))

    for label, v in cands:
        try:
            i.set_editor_property('assets_to_retarget', v)
            L('    assets_to_retarget <- %s' % label)
            return True
        except Exception:
            continue
    E('    assets_to_retarget — 어떤 형태도 안 먹는다')
    return False


def run(name, rtg, mesh, anim):
    i = unreal.IKRetargetBatchOperationInputs()
    if not set_assets(i, anim):
        return None
    i.set_editor_property('source_mesh', mesh)
    i.set_editor_property('target_mesh', TGT_MESH)
    i.set_editor_property('ik_retarget_asset', rtg)
    i.set_editor_property('search', '_Anim')
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

    # ⚠⚠ **반환된 에셋을 반드시 저장한다.** 배치는 메모리에만 만들고,
    #    커맨드릿이 끝나면 저장 안 된 것은 그냥 사라진다. 같은 실행 안에서
    #    되읽으면 「있다」고 나와서 성공한 줄 알기 쉽다.
    saved = 0
    for o in (out or []):
        try:
            # 반환은 **AssetData 구조체**다 — `package_name` 을 쓴다
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


L('=== 타깃 리그 손보기 (IK_Mixamo) ===')
fix_target_rig()
L('')

for n in NAMES:
    L('=== %s ===' % n)
    mesh = EAL.load_asset('%s/%s' % (PACK, n))
    anim = EAL.load_asset('%s/%s_Anim' % (PACK, n))
    if not (mesh and anim):
        E('  메쉬/애님이 없다'); continue
    try:
        rtg = build(n, mesh)
        if rtg is None:
            continue
        L('    run_batch_retarget -> %r' % (run(n, rtg, mesh, anim),))
    except Exception as ex:
        E('  실패: %s' % str(ex)[:250])

# ---- 되읽기 ----
#
# ⚠⚠ **스켈레톤만 보면 안 된다.** 빈 애님도 스켈레톤·길이·트랙 수가 전부 맞아서
#    예전 검사를 그대로 통과했다. 본이 **실제로 움직이는지**를 재야 한다.
PROBE = ('Hips', 'Spine', 'RightArm', 'RightForeArm', 'RightHand', 'Head')
MIN_AMP = 5.0          # 이 정도도 안 움직이면 리타겟이 실패한 것이다


def amp(seq):
    """구간을 훑어 본들의 회전 변화폭 합(도)."""
    dur = seq.get_editor_property('sequence_length')
    total = 0.0
    for bone in PROBE:
        vals = []
        for i in range(9):
            t = min(dur * i / 8.0, dur - 0.001)
            try:
                r = unreal.AnimationLibrary.get_bone_pose_for_time(
                    seq, bone, t, False).rotation.rotator()
            except Exception:
                return -1.0
            vals.append((r.pitch, r.yaw, r.roll))
        total += max(max(v[k] for v in vals) - min(v[k] for v in vals) for k in range(3))
    return total


L('')
L('--- 되읽기 ---')
reg = unreal.AssetRegistryHelpers.get_asset_registry()
ok = 0
seen = 0
for d in reg.get_assets_by_path(OUTDIR, recursive=True):
    nm = str(d.asset_name)
    if 'Eating' not in nm or str(d.asset_class_path.asset_name) != 'AnimSequence':
        continue
    seen += 1
    a = EAL.load_asset(str(d.package_name))
    sk = a.get_editor_property('skeleton')
    skel_ok = bool(sk) and sk.get_name() == 'Hip_Hop_Dancing_Skeleton'
    moved = amp(a)

    # ⚠ 골반 높이도 본다. 0 이면 캐릭터가 **바닥에 주저앉는다** — 회전은
    #    멀쩡해서 움직임 검사만으로는 안 걸린다 (`fix_pelvis_op`).
    pose0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'Hips', 0.0, False)
    hips_z = pose0.translation.z

    # ⚠ 골반 **방향**도 본다. 마네퀸 규약(정면 +X)이 그대로 들어오면 yaw 가
    #    -90 쯤 되고 **손님이 옆을 보고 앉는다** — 회전량·높이로는 안 걸린다.
    #    정상 Mixamo 애님은 0 근처다 (`Sitting__1_` 5.3 · `Idle` 5.5).
    hips_yaw = pose0.rotation.rotator().yaw

    why = []
    if not skel_ok:
        why.append('스켈레톤')
    if moved < MIN_AMP:
        why.append('빈 애님')
    if hips_z < 30.0:
        why.append('골반이 바닥')
    if abs(hips_yaw) > 40.0:
        why.append('골반이 %+.0f도 돌아감' % hips_yaw)
    good = not why
    L('  %-26s %5.2fs  움직임 %6.0f도  골반Z %5.1f  yaw %+6.1f  %s' % (
        nm, a.get_editor_property('sequence_length'), moved, hips_z, hips_yaw,
        '' if good else '<-- ⚠ ' + ' · '.join(why)))
    ok += 1 if good else 0

L('')
if ok == len(NAMES):
    L('=== 완료 — 실제로 움직이는 손님 애님 %d개 ===' % ok)
    L('⚠ 자세가 맞는지는 **눈으로 봐야 한다.** 어긋나면 RTG_* 를 열어 Edit Pose 로 맞출 것.')
else:
    E('=== %d/%d 만 성공 (에셋 %d개 발견) — 체인 매핑 로그를 볼 것 ==='
      % (ok, len(NAMES), seen))
