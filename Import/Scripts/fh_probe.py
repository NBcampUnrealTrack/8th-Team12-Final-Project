"""FlowerHouse 임포트 검산 — **새 프로세스**로 돌릴 것 (같은 실행의 되읽기는 무의미)

메쉬 수 · 콜리전(볼록 요소) 수 · 슬롯/MI · 바운즈(축 확인) · 폴더의 잡동사니를 찍는다.
"""
import unreal

ROOT = '/Game/Levels/FlowerHouse/Scene'
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
kinds = {}
meshes = []
for a in ar.get_assets_by_path(ROOT, recursive=True):
    cls = str(a.asset_class_path.asset_name)
    kinds[cls] = kinds.get(cls, 0) + 1
    if cls == 'StaticMesh':
        meshes.append(str(a.package_name))
unreal.log_warning('[FHP] 에셋 종류: %s' % kinds)
n_col = n_nocol = 0
bad_mat = []
for p in sorted(meshes):
    sm = unreal.load_asset(p)
    bs = sm.get_editor_property('body_setup')
    g = bs.get_editor_property('agg_geom') if bs else None
    nc = len(g.get_editor_property('convex_elems')) if g else 0
    if nc:
        n_col += 1
    else:
        n_nocol += 1
    for s in sm.static_materials:
        mi = s.material_interface
        if mi is None or not mi.get_name().startswith('MI_'):
            bad_mat.append((p.split('/')[-1], str(s.material_slot_name)))
    if any(k in p for k in ('Table_Slab', 'FH_Pot_M', 'Showcase1_Body')) or len(meshes) <= 8:
        b = sm.get_bounding_box()
        unreal.log_warning('[FHP] %-40s 볼록 %d  tris %d  bounds min %s max %s' % (
            p.split('/')[-1], nc, sm.get_num_triangles(0),
            [round(v, 1) for v in (b.min.x, b.min.y, b.min.z)],
            [round(v, 1) for v in (b.max.x, b.max.y, b.max.z)]))
unreal.log_warning('[FHP] 메쉬 %d개 — 콜리전 있음 %d / 없음 %d / MI 아닌 슬롯 %d %s' % (
    len(meshes), n_col, n_nocol, len(bad_mat), bad_mat[:6]))
