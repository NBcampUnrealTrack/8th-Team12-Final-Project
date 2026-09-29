"""손님을 **언리얼 기본 마네퀸**으로 갈아 끼우기 — 1단계: 애님 전부 리타겟.

    에디터를 **끄고** 돌린다.

## 왜 `SKM_Manny_Simple` 인가

프로젝트를 훑어보니 **손님만 혼자 Mixamo** 였다 —

```
BP_Customer                                              Hip_Hop_Dancing (Mixamo)
BP_ThirdPersonCharacter · BP_Passerby · BP_SubNpc · …    SKM_Manny_Simple
BP_MainNpc_Sample                                        SKM_Quinn_Simple
```

`SKM_Manny_Simple` 과 `SKM_Quinn_Simple` 은 **같은 스켈레톤**
(`/Game/Characters/Mannequins/Meshes/SK_Mannequin`)을 쓰므로, 여기로 리타겟해
두면 나중에 퀸으로 바꾸는 것은 메시만 갈아 끼우면 된다.

⚠ 풀로 들어온 `/Game/Characters/NPCSequence/NPC_Idle/Mannequins/` 쪽에도
  `SKM_Manny`·`SK_Mannequin` 이 있지만 그것은 **별개의 스켈레톤**이다. 그쪽으로
  가면 프로젝트의 나머지 캐릭터와 애님을 공유 못 한다. 안 쓴다.

## ⚠⚠ 기존 리그를 건드리지 않는다

`/Game/Characters/Mannequins/Rigs/IK_Manne` 는 에픽이 준 리그이고 다른
리타게터들이 이미 걸려 있다. 체인을 고치면 **그쪽이 조용히 틀어진다.**
그래서 타깃 리그를 **새로 만든다**.

체인은 `ll_walk_retarget.py` 에서 검증된 6개 그대로다 (그때는 소스였고 지금은
타깃이다 — 방향만 뒤집혔다).

## ⚠ 루트모션

`ABP_Customer` 가 `RootMotionFromMontagesOnly` 이고 몽타주의 루트모션 플래그는
전부 꺼 둔 상태다 (§9-2). 전진은 `CharacterMovement` 가 한다.

- `Root Motion` op **끔** — 마네퀸에는 `root` 본이 있어서 켜 두면 소스에 없는
  루트 이동을 만들어 낸다.
- `scale_horizontal = 1` — **지금 동작을 그대로 옮긴다.** 0 으로 두면 제자리로
  만들어 버리는데, 그건 애님별로 판단할 일이지 일괄로 할 일이 아니다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()

SRC_RIG = '/Game/BurgerHouse/Anims/Mixamo/IK_Mixamo'
SRC_MESH = '/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing'
TGT_MESH = '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple'

RTDIR = '/Game/BurgerHouse/Anims/Manny/Retarget'
OUTDIR = '/Game/BurgerHouse/Anims/Manny'

# 타깃(마네퀸) 체인 — `ll_walk_retarget.py` 의 CHAINS 와 같다.
# ⚠ 이름이 소스(IK_Mixamo)와 **같아야** EXACT 자동 매핑이 1:1 로 붙는다.
TGT_CHAINS = [
    ('Spine',    'spine_01',   'spine_05'),
    ('Head',     'neck_01',    'head'),
    ('LeftArm',  'upperarm_l', 'hand_l'),
    ('RightArm', 'upperarm_r', 'hand_r'),
    ('LeftLeg',  'thigh_l',    'ball_l'),
    ('RightLeg', 'thigh_r',    'ball_r'),
]

BLEND_TO_SOURCE = 0.0     # 0 = 타깃(마네퀸) 비율을 따른다. 1 이면 Mixamo 골반 높이가 들어와 뜬다.
SCALE_HORIZONTAL = 1.0    # 지금 동작 그대로 (모듈 주석)


def save(obj):
    EAL.save_asset(obj.get_path_name().split('.')[0], only_if_is_dirty=False)


def load_or_create(path, name, folder, cls, factory):
    """⚠ 이미 있으면 `create_asset` 이 **None** 을 준다. 있으면 불러 쓴다."""
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    a = TOOLS.create_asset(name, folder, cls, factory)
    if a is None:
        E('    %s 생성 실패' % name)
    return a


# ───────────────────────── 1) 타깃 리그
L('R === 타깃 리그 IK_MannyTarget ===')
tgt_mesh = EAL.load_asset(TGT_MESH)
src_rig = EAL.load_asset(SRC_RIG)
if not tgt_mesh or not src_rig:
    E('R   메시(%s) 또는 소스 리그(%s) 가 없다' % (TGT_MESH, SRC_RIG))
    raise SystemExit

tgt_rig = load_or_create('%s/IK_MannyTarget' % RTDIR, 'IK_MannyTarget', RTDIR,
                         unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
c = unreal.IKRigController.get_controller(tgt_rig)
c.set_skeletal_mesh(tgt_mesh)
have = {str(ch.get_editor_property('chain_name')) for ch in c.get_retarget_chains()}
for cn, sb, eb in TGT_CHAINS:
    if cn not in have:
        c.add_retarget_chain(cn, sb, eb, 'None')
    else:
        c.set_retarget_chain_start_bone(cn, sb)
        c.set_retarget_chain_end_bone(cn, eb)
c.set_retarget_root('pelvis')
save(tgt_rig)
L('R   체인 %d개 · root=%s' % (len(c.get_retarget_chains()), c.get_retarget_root()))

# ───────────────────────── 2) 리타게터
L('R === RTG_MixamoToManny ===')
rtg = load_or_create('%s/RTG_MixamoToManny' % RTDIR, 'RTG_MixamoToManny', RTDIR,
                     unreal.IKRetargeter, unreal.IKRetargetFactory())
rc = unreal.IKRetargeterController.get_controller(rtg)
rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)


def config_ops():
    """op 스택 + Pelvis Motion.

    ⚠⚠ **op 이 0개면 리타겟은 타깃 레퍼런스 포즈를 그대로 뱉는다** — 에셋도 생기고
       스켈레톤·길이·트랙 수도 다 맞아서 어디서도 안 걸린다 (§7).
    """
    if rc.get_num_retarget_ops() == 0:
        rc.add_default_ops()
    n = rc.get_num_retarget_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    L('R     op %d개: %s' % (n, ', '.join(str(rc.get_op_name(i)) for i in range(n))))

    for i in range(n):
        if str(rc.get_op_name(i)) == 'Root Motion' and rc.get_retarget_op_enabled(i):
            rc.set_retarget_op_enabled(i, False)
            L('R     Root Motion op 끔 (모듈 주석)')

    for i in range(n):
        if str(rc.get_op_name(i)) != 'Pelvis Motion':
            continue
        oc = rc.get_op_controller(i)
        if not oc or not hasattr(oc, 'get_settings'):
            E('R     Pelvis Motion op 설정을 못 얻는다')
            return n
        s = oc.get_settings()
        s.set_editor_property('blend_to_source_translation', BLEND_TO_SOURCE)
        s.set_editor_property('blend_to_source_translation_weights',
                              unreal.Vector(BLEND_TO_SOURCE, BLEND_TO_SOURCE, BLEND_TO_SOURCE))
        s.set_editor_property('scale_horizontal', SCALE_HORIZONTAL)
        oc.set_settings(s)
        got = oc.get_settings()
        L('R     Pelvis Motion · blend_to_source=%.1f · scale_horizontal=%.1f'
          % (got.get_editor_property('blend_to_source_translation'),
             got.get_editor_property('scale_horizontal')))
        break
    return n


# ⚠ **순서** — op 이 먼저다. 매핑이 op 안에 들어 있어서 op 없이 auto_map_chains 를
#   부르면 조용히 아무것도 안 붙는다.
n_ops = config_ops()

# ⚠ Mixamo 는 T포즈, 마네퀸은 A포즈다. 정렬 없이 리타겟하면 팔이 45도 틀어진다.
for who in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
    try:
        rc.auto_align_all_bones(who)
    except Exception as ex:
        L('R     auto_align 건너뜀: %s' % str(ex)[:70])

rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
missing = [cn for cn, _, _ in TGT_CHAINS
           if str(rc.get_source_chain(cn) or 'None') in ('None', '')]
if missing:
    E('R     ⚠ 매핑이 안 붙은 체인: %s' % ', '.join(missing))
else:
    L('R     체인 매핑 %d개 확인' % len(TGT_CHAINS))

# ⚠ 이 판본의 `run_op_initial_setup` 은 **op 하나씩** 받는다 (`op_index` 필수).
done = 0
for i in range(n_ops or 0):
    try:
        rc.run_op_initial_setup(i)
        done += 1
    except Exception as ex:
        E('R     run_op_initial_setup(%d) 실패: %s' % (i, str(ex)[:80]))
L('R     run_op_initial_setup %d/%d' % (done, n_ops or 0))

# ⚠⚠ 초기 설정이 Pelvis Motion 값을 **되돌린다.** 뒤에서 한 번 더 박는다.
config_ops()
save(rtg)

# ───────────────────────── 3) 일괄 리타겟
L('R === 일괄 리타겟 ===')
TARGETS = []
for a in AR.get_assets_by_path('/Game/BurgerHouse/Anims/Mixamo', recursive=True):
    n = str(a.asset_class_path.asset_name)
    if n not in ('AnimSequence', 'AnimMontage', 'AnimBlueprint'):
        continue
    TARGETS.append(a)
for a in AR.get_assets_by_path('/Game/BurgerHouse/Anims/Montages', recursive=True):
    if str(a.asset_class_path.asset_name) == 'AnimMontage':
        TARGETS.append(a)
L('R   대상 %d개' % len(TARGETS))

arr = unreal.Array(unreal.AssetData)
for a in TARGETS:
    arr.append(a)

i = unreal.IKRetargetBatchOperationInputs()
i.set_editor_property('assets_to_retarget', arr)
i.set_editor_property('source_mesh', EAL.load_asset(SRC_MESH))
i.set_editor_property('target_mesh', tgt_mesh)
i.set_editor_property('ik_retarget_asset', rtg)
i.set_editor_property('use_source_path', False)
i.set_editor_property('overwrite_existing_files', True)
i.set_editor_property('include_referenced_assets', True)
for tp in (OUTDIR, unreal.DirectoryPath(OUTDIR)):
    try:
        i.set_editor_property('target_path', tp)
        break
    except Exception:
        continue

out = unreal.IKRetargetBatchOperation.run_batch_retarget(i)
L('R   결과 %d개' % len(out or []))

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
        E('R   저장 실패 %r: %s' % (o, str(ex)[:90]))
L('R   저장 %d개' % saved)

L('R === 만들어진 것 ===')
made = AR.get_assets_by_path(OUTDIR, recursive=True)
by = {}
for a in made:
    by.setdefault(str(a.asset_class_path.asset_name), []).append(str(a.asset_name))
for k in sorted(by):
    L('R   %-16s %d개' % (k, len(by[k])))
    L('R      %s' % ', '.join(sorted(by[k])))
