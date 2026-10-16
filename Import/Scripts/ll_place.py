"""배치 — `LL_placement.json` 의 인스턴스 334개를 HISM 으로 스폰

    UnrealEditor-Cmd.exe projectll.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_place.py"

또는 에디터 Output Log 의 Python 콘솔에서:
    exec(open(r'C:/Project/Unreal/Import/Scripts/ll_place.py').read())

## ✅ `정면 축` 확정됨 (2026-08-28)

`Chair_A` 를 회전 0 으로 놓으니 블렌더 정면(**−Y**)이 UE 에서 **+Y** 를 향했다.
축 변환은 아래 `AXIS` **한 곳에만** 있다 — 다시 재려면 거기만 고치면 된다.

## 다시 돌려도 안전하다

레벨이 이미 있으면 로드해서 `HISM_*` / `LL_Shell` 액터를 지우고 다시 만든다.
손으로 추가한 액터(조명·플레이어 스타트 등)는 건드리지 않는다.

## 왜 HISM 인가

대전제다 — 메쉬는 공유하고 겉보기만 인스턴스별로 다르게 한다. 개별 액터로
스폰하면 드로콜 절감이 통째로 무의미해진다. 특히 `Tile_Floor` 182 /
`TrackHead` 36 / `Chair_A` 29 / `Table_A` 21.

`custom` 의 `inst_uv_u/v` `inst_tint` 는 **Per Instance Custom Data** 로 들어간다.
머티리얼에서 `PerInstanceCustomData` 노드로 받아 쓰면 "메쉬는 하나, 겉보기는
제각각" 이 완성된다. (float 3개 = 인덱스 0,1,2)
"""
import unreal
import json
import os

JSON = r'C:/Project/Unreal/Import/LL_placement.json'
PROPS = '/Game/BurgerHouse/Props/'
SHELL = '/Game/BurgerHouse/Shell/LL_Shell'

M = 100.0       # 블렌더 미터 -> UE 유닛(cm)

# ✅ **확정 (2026-08-28 왕복 테스트).**
#   `Chair_A` 를 회전 0 으로 놓으니 블렌더 정면(**−Y**)이 UE 에서 **+Y** 를 향했다.
#   즉 블렌더 (0,−1,0) -> UE (0,+1,0) 이고, 이는 **Y축만 뒤집는** 표준
#   우수좌표계(블렌더) -> 좌수좌표계(UE) 변환과 정확히 일치한다.
#
#       UE_X = +Blender_X      UE_Y = **−Blender_Y**      UE_Z = +Blender_Z
#
#   회전도 같은 규칙을 따른다 — Y 를 뒤집으면 **Z·Y 회전의 부호가 반대**가 된다.
#       Roll (UE X) = +rx      Pitch (UE Y) = −ry      Yaw (UE Z) = −rz
#   검산: 블렌더 Rz −90° 인 의자의 정면은 블렌더에서 −X 다. UE 에서 기본 정면이
#   +Y 이므로 Yaw +90° 를 주면 +Y -> −X. 일치한다.
#
#   ⚠ Pitch/Roll 부호는 **실측이 아니라 위 규칙에서 유도한 값**이다. 다만 배치
#     334개 중 X/Y 회전이 있는 것은 `Grout_E`/`Grout_W` **2개뿐**이고 둘 다
#     벽 뒤 백플레인이라 눈에 안 띈다. 나머지는 전부 Z 회전만 쓴다.
AXIS = {
    'loc': lambda x, y, z: unreal.Vector(x * M, -y * M, z * M),
    'rot': lambda rx, ry, rz: unreal.Rotator(pitch=-ry, yaw=-rz, roll=rx),
}


def load():
    with open(JSON, encoding='utf-8') as f:
        return json.load(f)


LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'

# ⚠ **그림자를 만들 게 없는데 VSM 마킹 비용만 내는 것들.**
#   `[VSM] Non-Nanite Marking Job Queue overflow` 경고의 주원인이다.
#   ⚠ 이건 **컴포넌트 설정**이라 HISM 을 새로 만들면 사라진다 — 그래서
#     `ll_nanite.py` 에 따로 두지 않고 **HISM 을 만드는 여기서** 같이 정한다.
#     (한 번 분리해 뒀다가 배치를 다시 돌리면서 통째로 잃었다.)
NO_SHADOW = {
    'Tile_Floor',       # 4v 평면이 바닥에 누워 있다 (182장!)
    'Grout_Unit',       # 벽·바닥 뒤 백플레인 — 영영 안 보인다
    'Scasi_T', 'Scasi_E',   # 벽에 붙은 얇은 레터링
    'Mirror_Shared_1',      # 벽면 초타원 판
}


MATS = '/Game/BurgerHouse/Materials/'


def variant_suffix(key):
    """('M_ChairFrame', 'M_ChairFrame_Y') -> '_Y'"""
    out = ''
    for base, tgt in key:
        s = tgt[len(base):] if tgt.startswith(base) else '_' + tgt
        out += s if s.startswith('_') else '_' + s
    return out


def apply_override(comp, mesh, key, label):
    """인스턴스별 재질 교체를 **컴포넌트 단위**로 건다.

    ⚠ HISM 은 인스턴스마다 다른 재질을 못 준다 — 그래서 `ll_place` 는
      교체 조합마다 **HISM 을 따로** 만든다. 여기서는 그 컴포넌트에
      `set_material` 을 걸 뿐이다.

    ⚠ **슬롯을 이름으로 찾는다.** UE 의 슬롯 순서는 블렌더와 다르다.
    """
    names = [str(s.material_slot_name) for s in mesh.static_materials]
    for base, tgt in key:
        if base not in names:
            unreal.log_error('  %s: 슬롯 %s 가 메쉬에 없다 (있는 것 %s)'
                             % (label, base, names))
            continue
        mi = unreal.load_asset(MATS + 'MI_' + tgt)
        if mi is None:
            unreal.log_error('  %s: MI_%s 를 못 찾았다 — mat_export 가 '
                             'OBJECT 링크 재질을 걷었는지 볼 것' % (label, tgt))
            continue
        comp.set_material(names.index(base), mi)


def add_hism(eas, sds, label, mesh, asset):
    """액터 하나에 HISM 컴포넌트를 붙여 돌려준다.

    ⚠ **`Actor.add_component_by_class` 는 파이썬에 없다.** 대신
      `SubobjectDataSubsystem` 으로 붙인다 — 이게 지원되는 경로다.
    """
    a = eas.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    a.set_actor_label(label)
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    p = unreal.AddNewSubobjectParams()
    p.set_editor_property('parent_handle', root)
    p.set_editor_property(
        'new_class', unreal.HierarchicalInstancedStaticMeshComponent)
    h, fail = sds.add_new_subobject(p)
    if fail and str(fail):
        unreal.log_error('  %s 컴포넌트 추가 실패: %s' % (label, fail))
        return a, None
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
        sds.k2_find_subobject_data_from_handle(h))
    comp.set_static_mesh(mesh)
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    # ⚠ 3 = inst_uv_u / inst_uv_v / inst_tint. 머티리얼의
    #   `PerInstanceCustomData` 인덱스 0·1·2 와 짝이다
    comp.set_editor_property('num_custom_data_floats', 3)
    # ⚠ 에셋 이름으로 판정한다 — 라벨에는 변주 접미사(`_Y`)가 붙어 안 맞는다
    if asset in NO_SHADOW:
        comp.set_editor_property('cast_shadow', False)
    return a, comp


def main():
    d = load()
    inst = d['instances']
    unreal.log_warning('=== 배치 — 인스턴스 %d개 / 에셋 %d종 ==='
                       % (len(inst), len(d['assets'])))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    if not (les and eas and sds):
        unreal.log_error('필요한 서브시스템이 없다: les=%s eas=%s sds=%s'
                         % (les, eas, sds))
        return

    # ⚠ **새 레벨을 만들어 거기에 넣는다.** 안 그러면 `/Temp/Untitled` 라는
    #   휘발성 월드에 스폰되고 저장되지 않는다 (테스트 때 실제로 그랬다).
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        les.load_level(LEVEL)
        # 다시 돌릴 때를 위해 기존 배치물을 지운다
        old = [a for a in eas.get_all_level_actors()
               if a.get_actor_label().startswith(('HISM_', 'LL_Shell'))]
        for a in old:
            eas.destroy_actor(a)
        unreal.log_warning('  기존 레벨 로드 / 배치물 %d개 제거' % len(old))
    else:
        les.new_level(LEVEL)
        unreal.log_warning('  새 레벨 생성: %s' % LEVEL)

    # ---- 셸 ----
    # ⚠ **셸은 둘이다** — 불투명 본체(Nanite)와 반투명 부재(non-Nanite).
    #   나눈 이유는 Nanite 가 반투명을 지원하지 않아서다. 둘 다 원점에 놓는다
    #   (블렌더 월드 좌표 그대로 구워져 있다).
    for path, label in ((SHELL, 'LL_Shell'),
                        (SHELL + '_Trans', 'LL_Shell_Trans')):
        sm = unreal.load_asset(path)
        if not sm:
            unreal.log_warning('  %s 없음 — 건너뜀' % label)
            continue
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor,
                                       unreal.Vector(0, 0, 0))
        a.set_actor_label(label)
        a.static_mesh_component.set_static_mesh(sm)
        a.set_actor_scale3d(unreal.Vector(1, 1, 1))
        unreal.log_warning('  %s 배치 (%d정점 / 슬롯 %d)'
                           % (label, sm.get_num_vertices(0),
                              len(sm.static_materials)))

    # ---- 에셋 × 재질교체조합 별 HISM ----
    # ⚠ **HISM 은 인스턴스별 재질을 못 준다.** 의자 프레임 3색처럼 같은 메쉬를
    #   다른 재질로 쓰려면 조합마다 컴포넌트를 나눠야 한다. 드로콜은 조합 수만큼
    #   늘지만(의자 1 -> 3) 메쉬는 그대로 하나다.
    by_asset = {}
    for it in inst:
        key = tuple(sorted((it.get('mat_override') or {}).items()))
        by_asset.setdefault((it['asset'], key), []).append(it)

    total = 0
    for asset, key in sorted(by_asset, key=lambda k: (k[0], variant_suffix(k[1]))):
        mesh = unreal.load_asset(PROPS + asset)
        if mesh is None:
            unreal.log_error('  %-18s 에셋 없음 — 건너뜀' % asset)
            continue
        items = by_asset[(asset, key)]
        label = 'HISM_' + asset + variant_suffix(key)
        _, hism = add_hism(eas, sds, label, mesh, asset)
        if hism is None:
            continue
        if key:
            apply_override(hism, mesh, key, label)
        for it in items:
            lx, ly, lz = it['loc']
            rx, ry, rz = it['rot_deg']
            sx, sy, sz = it['scale']
            i = hism.add_instance(unreal.Transform(
                AXIS['loc'](lx, ly, lz), AXIS['rot'](rx, ry, rz),
                unreal.Vector(sx, sy, sz)))
            c = dict(it.get('custom') or {})
            # ⚠ **`inst_cell` 을 UV 오프셋으로 바꿔 넣는다.**
            #   바닥 타일 텍스처는 1200mm 안에 600mm 칸이 **2×2 = 4개**이고,
            #   `inst_cell`(0~3)이 타일마다 어느 칸을 볼지 정한다. 이걸 안 넘기면
            #   **182장이 전부 같은 칸**을 보게 되어, 회전을 줘도 티가 안 난다
            #   (사용자가 "회전이 들어간건지 모르겠다"고 한 이유).
            #   타일링 후 UV 로 한 칸 = 0.5 이므로 오프셋도 0.5 단위다.
            if 'inst_cell' in c:
                cell = int(c['inst_cell'])
                c['inst_uv_u'] = (cell % 2) * 0.5
                c['inst_uv_v'] = (cell // 2) * 0.5
            for k, nm in enumerate(('inst_uv_u', 'inst_uv_v', 'inst_tint')):
                # ⚠ 마지막 값만 `mark_dirty=True` — 매번 True 면 인스턴스마다
                #   렌더 상태를 다시 만들어 334개에서 눈에 띄게 느려진다.
                #   `mark_render_state_dirty()` 는 파이썬에 노출돼 있지 않다
                hism.set_custom_data_value(i, k, float(c.get(nm, 0.0)), k == 2)
        unreal.log_warning('  %-22s HISM %3d개%s%s'
                           % (label.replace('HISM_', ''),
                              hism.get_instance_count(),
                              '  (그림자 끔)' if asset in NO_SHADOW else '',
                              ('  재질 ' + ', '.join('%s->%s' % kv for kv in key))
                              if key else ''))
        total += hism.get_instance_count()

    les.save_current_level()
    unreal.log_warning('=== 인스턴스 %d개 배치 / 레벨 저장: %s ==='
                       % (total, LEVEL))


main()
