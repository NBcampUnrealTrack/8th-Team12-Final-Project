"""LL 프로토타입 FBX 임포트 — UE 에디터 파이썬 (Interchange 파이프라인 명시)

    UnrealEditor-Cmd.exe projectll.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_import.py"

## ⚠ `FbxImportUI` 는 쓰면 안 된다

UE 5.x 는 FBX 를 **Interchange** 로 처리한다. `AssetImportTask.options` 에
`FbxImportUI` 를 물리면 **조용히 무시되고** 기본 파이프라인이 돈다. 2026-08-28 에
실제로 그렇게 임포트됐고, 그 결과:

  - **콜리전 0개** — `UCX_*` 28개가 콜리전이 아니라 **렌더 지오메트리로 병합**됐다
    (`Table_A` 머티리얼 슬롯이 2 -> 3 으로 늘어난 게 증거였다)
  - `combine_meshes` / `auto_generate_collision` 같은 옵션이 전부 안 먹었다

그래서 **`InterchangeGenericAssetsPipeline` 을 직접 만들어
`InterchangePipelineStackOverride` 로 넘긴다.**

## `recompute_normals` 는 끄고 `recompute_tangents` 는 켠다

Interchange 기본값이 **True** 다. 그대로 두면 블렌더에서 섬 단위로 교정한
법선 24개와 스무딩 그룹이 **UE 에서 다시 계산되어 버려진다.**
`use_tspace=True` 로 탄젠트까지 구워 보냈으므로 둘 다 끈다.
"""
import unreal
import os

ROOT = r'C:/Project/Unreal/Import/FBX'
DEST_SHELL = '/Game/BurgerHouse/Shell'
DEST_PROPS = '/Game/BurgerHouse/Props'


def combine_enum():
    """모든 메쉬를 하나로 합치는 열거값. 이름이 버전마다 달라 방어적으로 찾는다."""
    E = unreal.InterchangeCombineStaticMeshesBehavior
    for n in ('COMBINE_ALL_MESHES_INTO_ONE', 'ALL_MESHES_INTO_ONE',
              'COMBINE_ALL', 'ALL'):
        if hasattr(E, n):
            return getattr(E, n)
    # 못 찾으면 DO_NOT_COMBINE(2) 이 아닌 첫 값
    for n in dir(E):
        if n.isupper() and 'DO_NOT' not in n:
            return getattr(E, n)
    return None


def make_pipeline():
    p = unreal.InterchangeGenericAssetsPipeline()

    mp = p.get_editor_property('mesh_pipeline')
    mp.set_editor_property('import_static_meshes', True)
    c = combine_enum()
    if c is not None:
        mp.set_editor_property('combine_static_meshes_behavior', c)
    # ⚠ 콜리전 — `UCX_<메쉬이름>_##` 이름 규약으로 붙인다
    mp.set_editor_property('collision', True)
    mp.set_editor_property('import_collision_according_to_mesh_name', True)
    # ⚠ **자동 생성은 끈다.** 켜면 UCX 위에 자동 박스가 덧씌워져 이중이 된다
    mp.set_editor_property('force_collision_primitive_generation', False)

    cm = p.get_editor_property('common_meshes_properties')
    # ⚠ 블렌더에서 구운 법선·탄젠트를 **그대로 쓴다** (위 설명 참조)
    # ⚠ **법선은 유지, 탄젠트는 UE 가 다시 만들게 한다.**
    #   법선을 재계산시키면 블렌더에서 섬 단위로 교정한 것과 스무딩 그룹이
    #   버려진다 -> False.
    #   ⚠ 그런데 **탄젠트까지 끄면 안 된다.** 이 씬은 UV 없는 오브젝트가
    #   **185개**(셸에만 104개)라 탄젠트를 만들 수가 없고, 그 상태에서
    #   마스터가 노멀맵을 샘플링하면 탄젠트 기저가 무너져 **화면 전체가
    #   대각선으로 번진다** (2026-08-28 실제 증상).
    #   UE 가 자기 삼각분할 기준으로 만들게 두는 것이 맞다.
    cm.set_editor_property('recompute_normals', False)
    cm.set_editor_property('recompute_tangents', True)
    # ⚠ **버텍스 컬러를 반드시 가져온다.** `Sofa_Unit` 의 색(`SofaCol`: 노랑/
    #   차콜/회색)이 버텍스 컬러에 들어 있어서, 무시하면 소파가 전부 노랗게
    #   나온다. 기본값이 REPLACE 라 지금은 맞지만 **바뀌면 조용히 망가지므로**
    #   명시해 둔다.
    cm.set_editor_property(
        'vertex_color_import_option',
        unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)

    ov = unreal.InterchangePipelineStackOverride()
    ov.add_pipeline(p)
    return ov, c


def task(path, dest, options):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', path)
    t.set_editor_property('destination_path', dest)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)
    t.set_editor_property('options', options)
    return t


def main():
    options, c = make_pipeline()
    unreal.log_warning('  combine_static_meshes_behavior = %s' % c)
    unreal.log_warning('  recompute_normals=False (블렌더 법선 유지) / recompute_tangents=True (UE 가 생성)')

    # 일부만 다시 넣고 싶을 때 — 커맨드릿은 인자를 넘기기 까다로워 환경변수로 받는다
    #     set LL_IMPORT_ONLY=LL_Shell        (쉼표로 여러 개)
    only = [s.strip().lower() for s in
            os.environ.get('LL_IMPORT_ONLY', '').split(',') if s.strip()]

    def want(path):
        return not only or os.path.splitext(
            os.path.basename(path))[0].lower() in only

    tasks = []
    # ⚠ 셸은 **둘**이다 — 불투명 본체와 반투명 부재.
    #   Nanite 가 반투명을 못 써서, 슬롯 하나 때문에 6.3 × 13.8m 메쉬 전체가
    #   non-Nanite 가 되고 VSM 마킹 큐가 넘쳤다. 나눠서 본체만 Nanite 를 쓴다.
    for fn in ('LL_Shell.fbx', 'LL_Shell_Trans.fbx'):
        p = os.path.join(ROOT, fn)
        if os.path.exists(p) and want(p):
            tasks.append(task(p, DEST_SHELL, options))
    adir = os.path.join(ROOT, 'Assets')
    for f in sorted(os.listdir(adir)):
        if f.lower().endswith('.fbx') and want(f):
            tasks.append(task(os.path.join(adir, f), DEST_PROPS, options))

    if only:
        unreal.log_warning('  LL_IMPORT_ONLY=%s -> %d개만 넣는다'
                           % (','.join(only), len(tasks)))
    unreal.log_warning('=== LL 임포트 — FBX %d개 ===' % len(tasks))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    unreal.log_warning('=== 임포트 끝 ===')


main()


# ---------------------------------------------------------------------------
# ⚠ 셸은 **렌더 메쉬를 그대로 콜리전으로** 쓴다.
#   오브젝트 122개를 하나로 합치면 `UCX_LL_Shell_##` 매칭이 깨져 볼록 껍질
#   1개로 뭉개지고, 그러면 **방 전체가 막힌 덩어리**가 되어 못 들어간다.
#   건축 셸은 complex-as-simple 이 정석이다 (정적이라 물리 시뮬도 필요 없다).
def set_shell_complex_collision():
    # ⚠ **반투명 쪽도 콜리전이 필요하다.** 유리문·파티션 문은 실제로 막혀
    #   있어야 한다 — 안 그러면 NPC 와 플레이어가 유리를 통과한다.
    for name in ('LL_Shell', 'LL_Shell_Trans'):
        path = '/Game/BurgerHouse/Shell/' + name
        m = unreal.load_asset(path)
        if m is None:
            unreal.log_warning('  %s 없음 — 건너뜀' % name)
            continue
        bs = m.get_editor_property('body_setup')
        bs.set_editor_property(
            'collision_trace_flag',
            unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        # 잘못 생긴 볼록 껍질 1개를 비운다
        g = bs.get_editor_property('agg_geom')
        g.set_editor_property('convex_elems', [])
        bs.set_editor_property('agg_geom', g)
        unreal.EditorAssetLibrary.save_asset(path)
        unreal.log_warning('  %s -> CTF_USE_COMPLEX_AS_SIMPLE / 볼록껍질 비움'
                           % name)


set_shell_complex_collision()
