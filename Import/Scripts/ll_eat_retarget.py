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

# ⚠ IK_Mixamo 와 **같은 이름**이어야 자동 매핑이 붙는다
CHAINS = [
    ('Spine',    'spine_01',   'spine_05'),
    ('Head',     'neck_01',    'head'),
    ('LeftArm',  'upperarm_l', 'hand_l'),
    ('RightArm', 'upperarm_r', 'hand_r'),
    ('LeftLeg',  'thigh_l',    'ball_l'),
    ('RightLeg', 'thigh_r',    'ball_r'),
]

NAMES = ['Eating_a_Burger', 'Eating_a_Drumstick', 'Eating_Frantically', 'Eating_Fries']


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
    for who in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            rc.auto_align_all_bones(who)
        except Exception as ex:
            L('    auto_align 건너뜀: %s' % str(ex)[:70])
    rc.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
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
L('')
L('--- 되읽기 ---')
reg = unreal.AssetRegistryHelpers.get_asset_registry()
ok = 0
for d in reg.get_assets_by_path(OUTDIR, recursive=True):
    nm = str(d.asset_name)
    if 'Eating' not in nm or str(d.asset_class_path.asset_name) != 'AnimSequence':
        continue
    a = EAL.load_asset(str(d.package_name))
    sk = a.get_editor_property('skeleton')
    good = sk and sk.get_name() == 'Hip_Hop_Dancing_Skeleton'
    L('  %-30s skel=%-30s %5.2fs %s' % (
        nm, sk.get_name() if sk else '?', a.get_editor_property('sequence_length'),
        '<-- 손님용' if good else ''))
    ok += 1 if good else 0

L('')
if ok == len(NAMES):
    L('=== 완료 — 손님 스켈레톤 애님 %d개 ===' % ok)
    L('⚠ 자세가 맞는지는 **눈으로 봐야 한다.** 어긋나면 RTG_* 를 열어 Edit Pose 로 맞출 것.')
else:
    E('=== %d/%d 만 나왔다 — 위 로그를 볼 것 ===' % (ok, len(NAMES)))
