"""FlowerHouse 임포트 — 텍스처 · 마스터 3종 · MI · FBX(Interchange) · 슬롯 배정

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script="C:/Project/Unreal/Import/Scripts/fh_import.py" -unattended -nopause -nosplash

원본은 `C:/Project/FlowerHouse/export_unreal.py` 가 만든다:
    Import/FBX/FlowerHouse/*.fbx        (에셋 320개, git 제외)
    Import/Textures/FlowerHouse/*       (텍스처, git 제외)
    Import/FH_materials.json            (재질 파라미터)
    Import/FH_placement.json            (배치 — fh_place.py 가 읽는다)

`ll_import.py` / `ll_material.py` 의 규약을 그대로 따른다 (BURGERHOUSE.md §5·§6):
  - FBX 는 **Interchange 파이프라인을 직접** 넘긴다 (`FbxImportUI` 는 조용히 무시된다)
  - 콜리전은 `UCX_<메쉬>_NN` 이름 매칭, 자동 생성은 끈다 (이중 콜리전 방지)
  - 법선은 블렌더 것 유지, 탄젠트는 UE 가 다시 만든다
  - 마스터는 지우지 않고 재사용한다 (MI 가 참조 중이면 delete 가 조용히 실패)
  - 에디터를 **끄고** 돌린다 (켜 두면 저장이 전부 False)

기존 `Content/Levels/FlowerHouse/` 의 팀 에셋(FlowerPot1~4, FlowerTable,
Blueprints)은 건드리지 않는다 — 전부 `Scene/` 아래로 들어간다.
"""
import unreal
import json
import os

IMPORT_DIR = r'C:/Project/Unreal/Import'
FBX_DIR = IMPORT_DIR + '/FBX/FlowerHouse'
TEX_SRC = IMPORT_DIR + '/Textures/FlowerHouse'
JSON = IMPORT_DIR + '/FH_materials.json'
ROOT = '/Game/Levels/FlowerHouse/Scene'
TEX_DEST = ROOT + '/Textures'
MAT_DEST = ROOT + '/Materials'
MESH_DEST = ROOT + '/Meshes'
MASTERS = {'opaque': MAT_DEST + '/M_FH_Master',
           'masked': MAT_DEST + '/M_FH_Master_Masked',
           'translucent': MAT_DEST + '/M_FH_Master_Trans'}
WHITE = '/Engine/EngineResources/WhiteSquareTexture'
WHITE_LIN = TEX_DEST + '/T_FH_White_Linear'
FLAT_N = TEX_DEST + '/T_FH_FlatNormal'
# 블렌더 발광(W/m²)과 UE 발광 배수는 단위가 다르다 — 절대 스케일만 여기서
EMISSIVE_GAIN = 1.5          # 6 blew the showcase LEDs out under Lumen (2026-09-30 shot)
# 실제 라이트로 바꾼 기구는 발광을 낮춘다 — 안 그러면 같은 빛을 두 번 센다
# (ll_material.LIGHT_CONVERTED 와 같은 이유). 0 이면 꺼진 것처럼 보이므로
# 「빛나 보이되 GI 기여는 작게」.
LIGHT_CONVERTED = {'M_Showcase_LED': 0.25, 'M_FH_LEDDiffuser': 0.3}

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log_warning('[FH] ' + msg)


def import_files(paths, dest):
    tasks = []
    for src in paths:
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', src)
        t.set_editor_property('destination_path', dest)
        t.set_editor_property('automated', True)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('save', True)
        tasks.append(t)
    if tasks:
        AT.import_asset_tasks(tasks)


# ---------------------------------------------------------------- 텍스처
def import_textures(data):
    """재질이 쓰는 텍스처만 들인다. 노멀·거칠기·알파는 sRGB 를 끈다.

    ⚠ 알파(오파시티) 맵은 **컬러 텍스처와 다른 샘플러**(MASKS)로 읽어야 해서
      같은 이미지라도 `_Op` 접미사 사본을 따로 만든다 — 샘플러 타입과 압축이
      짝이 안 맞으면 마스터가 컴파일을 거부하고 회색 체커로 떨어진다.
    """
    kinds = {}
    for m in data['materials'].values():
        for kind, t in (m.get('textures') or {}).items():
            kinds.setdefault(t['image'], set()).add(kind)
    srcs = [os.path.join(TEX_SRC, n) for n in sorted(kinds)]
    todo = [s for s in srcs if os.path.exists(s) and (
        os.environ.get('FH_TEX_REIMPORT') or not EAL.does_asset_exist(
            '%s/%s' % (TEX_DEST, os.path.splitext(os.path.basename(s))[0])))]
    missing = [s for s in srcs if not os.path.exists(s)]
    for s in missing:
        unreal.log_error('[FH] 텍스처 없음: %s' % s)
    import_files(todo, TEX_DEST)
    flat = os.path.join(TEX_SRC, 'T_FH_FlatNormal.png')
    if os.path.exists(flat) and not EAL.does_asset_exist(FLAT_N):
        import_files([flat], TEX_DEST)
    n_set = 0
    for name, ks in sorted(kinds.items()):
        p = '%s/%s' % (TEX_DEST, os.path.splitext(name)[0])
        tex = unreal.load_asset(p)
        if tex is None:
            continue
        if 'normal' in ks:
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('compression_settings',
                                    unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif ks & {'rough', 'metal'} and 'color' not in ks:
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('compression_settings',
                                    unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_asset(p)
        n_set += 1
        if 'alpha' in ks:
            op = p + '_Op'
            if not EAL.does_asset_exist(op):
                EAL.duplicate_asset(p, op)
            ot = unreal.load_asset(op)
            if ot:
                ot.set_editor_property('srgb', False)
                ot.set_editor_property('compression_settings',
                                       unreal.TextureCompressionSettings.TC_MASKS)
                EAL.save_asset(op)
    ft = unreal.load_asset(FLAT_N)
    if ft:
        ft.set_editor_property('srgb', False)
        ft.set_editor_property('compression_settings',
                               unreal.TextureCompressionSettings.TC_NORMALMAP)
        EAL.save_asset(FLAT_N)
    if not EAL.does_asset_exist(WHITE_LIN):
        EAL.duplicate_asset(WHITE, WHITE_LIN)
    wl = unreal.load_asset(WHITE_LIN)
    if wl:
        wl.set_editor_property('srgb', False)
        wl.set_editor_property('compression_settings',
                               unreal.TextureCompressionSettings.TC_MASKS)
        EAL.save_asset(WHITE_LIN)
    log('텍스처 %d종 (새로 들임 %d, 없음 %d)' % (len(kinds), len(todo), len(missing)))


# ---------------------------------------------------------------- 마스터
def build_master(kind):
    path = MASTERS[kind]
    name = path.split('/')[-1]
    mat = unreal.load_asset(path)
    if mat is None:
        mat = AT.create_asset(name, MAT_DEST, unreal.Material, unreal.MaterialFactoryNew())
    else:
        MEL.delete_all_material_expressions(mat)
    if mat is None:
        raise RuntimeError('마스터를 못 만든다: %s' % path)
    if kind == 'masked':
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('two_sided', True)
    elif kind == 'translucent':
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property(
            'translucency_lighting_mode',
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def sp(n, v, x, y, g='', lo=0.0, hi=1.0):
        e = expr(unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property('parameter_name', n)
        e.set_editor_property('default_value', v)
        e.set_editor_property('group', g)
        e.set_editor_property('slider_min', lo)
        e.set_editor_property('slider_max', hi)
        return e

    def vp(n, r, g_, b, x, y, g=''):
        e = expr(unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property('parameter_name', n)
        e.set_editor_property('default_value', unreal.LinearColor(r, g_, b, 1))
        e.set_editor_property('group', g)
        return e

    def rgb(src, x, y):
        # ⚠ VectorParameter 는 float4 — 마스크로 float3 로 맞춘다 (ll_material)
        m = expr(unreal.MaterialExpressionComponentMask, x, y)
        for c, on in (('r', True), ('g', True), ('b', True), ('a', False)):
            m.set_editor_property(c, on)
        MEL.connect_material_expressions(src, '', m, '')
        return m

    def tp(n, path, x, y, sampler=None):
        e = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        e.set_editor_property('parameter_name', n)
        e.set_editor_property('texture', unreal.load_asset(path))
        if sampler:
            e.set_editor_property('sampler_type', sampler)
        return e

    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1500, 0)
    tile = sp('UVTiling', 1.0, -1500, 150, 'UV', 0.01, 20.0)
    uvm = expr(unreal.MaterialExpressionMultiply, -1250, 40)
    MEL.connect_material_expressions(uv, '', uvm, 'A')
    MEL.connect_material_expressions(tile, '', uvm, 'B')

    ctex = tp('ColorTex', WHITE, -800, -200)
    MEL.connect_material_expressions(uvm, '', ctex, 'UVs')
    tint = rgb(vp('BaseColorTint', 1, 1, 1, -800, 100, 'Base'), -640, 100)
    m1 = expr(unreal.MaterialExpressionMultiply, -500, -50)
    MEL.connect_material_expressions(ctex, 'RGB', m1, 'A')
    MEL.connect_material_expressions(tint, '', m1, 'B')
    # 버텍스 컬러 — 절차 꽃(프리지아 꽃잎 틴트 등)의 색 변주가 여기 있다.
    # 기본값 0 스위치로 막아 둔다 (없는 메쉬에서 무엇을 내는지에 기대지 않는다)
    vc = expr(unreal.MaterialExpressionVertexColor, -800, 480)
    one = expr(unreal.MaterialExpressionConstant3Vector, -800, 560)
    one.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
    uvc = sp('UseVertexColor', 0.0, -800, 620, 'Base')
    vl = expr(unreal.MaterialExpressionLinearInterpolate, -500, 520)
    MEL.connect_material_expressions(one, '', vl, 'A')
    MEL.connect_material_expressions(vc, 'RGB', vl, 'B')
    MEL.connect_material_expressions(uvc, '', vl, 'Alpha')
    m3 = expr(unreal.MaterialExpressionMultiply, -120, 40)
    MEL.connect_material_expressions(m1, '', m3, 'A')
    MEL.connect_material_expressions(vl, '', m3, 'B')
    MEL.connect_material_property(m3, '', unreal.MaterialProperty.MP_BASE_COLOR)

    rtex = tp('RoughTex', WHITE_LIN, -800, 700, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    MEL.connect_material_expressions(uvm, '', rtex, 'UVs')
    rs = sp('Roughness', 0.5, -800, 900, 'Surface')
    rm = expr(unreal.MaterialExpressionMultiply, -500, 760)
    MEL.connect_material_expressions(rtex, 'R', rm, 'A')
    MEL.connect_material_expressions(rs, '', rm, 'B')
    MEL.connect_material_property(rm, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(sp('Metallic', 0.0, -500, 980, 'Surface'), '',
                                  unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(sp('Specular', 0.5, -500, 1060, 'Surface'), '',
                                  unreal.MaterialProperty.MP_SPECULAR)

    ntex = tp('NormalTex', FLAT_N if EAL.does_asset_exist(FLAT_N) else
              '/Engine/EngineMaterials/DefaultNormal', -800, 1160,
              unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_expressions(uvm, '', ntex, 'UVs')
    flat = expr(unreal.MaterialExpressionConstant3Vector, -800, 1320)
    flat.set_editor_property('constant', unreal.LinearColor(0, 0, 1, 1))
    ns = sp('NormalStrength', 1.0, -800, 1380, 'Surface', 0.0, 4.0)
    nl = expr(unreal.MaterialExpressionLinearInterpolate, -500, 1260)
    MEL.connect_material_expressions(flat, '', nl, 'A')
    MEL.connect_material_expressions(ntex, 'RGB', nl, 'B')
    MEL.connect_material_expressions(ns, '', nl, 'Alpha')
    MEL.connect_material_property(nl, '', unreal.MaterialProperty.MP_NORMAL)

    ec = rgb(vp('EmissiveColor', 0, 0, 0, -800, 1460, 'Emissive'), -640, 1460)
    es = sp('EmissiveStrength', 0.0, -800, 1550, 'Emissive', 0.0, 50.0)
    em = expr(unreal.MaterialExpressionMultiply, -500, 1500)
    MEL.connect_material_expressions(ec, '', em, 'A')
    MEL.connect_material_expressions(es, '', em, 'B')
    em2 = expr(unreal.MaterialExpressionMultiply, -360, 1500)
    MEL.connect_material_expressions(em, '', em2, 'A')
    MEL.connect_material_expressions(ctex, 'RGB', em2, 'B')
    MEL.connect_material_property(em2, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if kind in ('masked', 'translucent'):
        # 알파 채널 선택 — 폴리헤이븐 알파맵은 흑백 PNG 라 R 에 있다
        atex = tp('OpacityTex', WHITE_LIN, -800, 1700, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        MEL.connect_material_expressions(uvm, '', atex, 'UVs')
        fa = sp('OpacityFromAlpha', 0.0, -800, 1900, 'Surface')
        ol = expr(unreal.MaterialExpressionLinearInterpolate, -500, 1760)
        MEL.connect_material_expressions(atex, 'R', ol, 'A')
        MEL.connect_material_expressions(atex, 'A', ol, 'B')
        MEL.connect_material_expressions(fa, '', ol, 'Alpha')
        if kind == 'masked':
            MEL.connect_material_property(ol, '', unreal.MaterialProperty.MP_OPACITY_MASK)
        else:
            op = sp('Opacity', 1.0, -800, 1980, 'Surface')
            om = expr(unreal.MaterialExpressionMultiply, -360, 1800)
            MEL.connect_material_expressions(ol, '', om, 'A')
            MEL.connect_material_expressions(op, '', om, 'B')
            MEL.connect_material_property(om, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    EAL.save_asset(path)
    log('마스터 [%s] %s' % (kind, path))
    return mat


# ---------------------------------------------------------------- 인스턴스
def build_instances(data, masters):
    made = {}
    for name, m in sorted(data['materials'].items()):
        mi_name = 'MI_' + name[2:] if name.startswith('M_') else 'MI_' + name
        path = '%s/%s' % (MAT_DEST, mi_name)
        mi = unreal.load_asset(path)
        if mi is None:
            mi = AT.create_asset(mi_name, MAT_DEST, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            unreal.log_error('[FH] MI 생성 실패: %s' % name)
            continue
        kind = m.get('kind', 'opaque')
        MEL.set_material_instance_parent(mi, masters[kind])
        tx = m.get('textures') or {}
        c = tx.get('color')
        bc = [1.0, 1.0, 1.0] if (c and c.get('direct')) else (m.get('base_color') or [1, 1, 1])
        MEL.set_material_instance_vector_parameter_value(
            mi, 'BaseColorTint', unreal.LinearColor(bc[0], bc[1], bc[2], 1))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'Roughness', 1.0 if (tx.get('rough') or {}).get('direct') else m.get('roughness', 0.5))
        MEL.set_material_instance_scalar_parameter_value(mi, 'Metallic', m.get('metallic', 0.0))
        MEL.set_material_instance_scalar_parameter_value(mi, 'Specular', m.get('specular', 0.5))
        MEL.set_material_instance_scalar_parameter_value(mi, 'NormalStrength', m.get('normal_strength', 1.0))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'UseVertexColor', 1.0 if m.get('vertex_color') else 0.0)
        ec = m.get('emissive_color') or [0, 0, 0]
        MEL.set_material_instance_vector_parameter_value(
            mi, 'EmissiveColor', unreal.LinearColor(ec[0], ec[1], ec[2], 1))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'EmissiveStrength', m.get('emissive_strength', 0.0) * EMISSIVE_GAIN
            * LIGHT_CONVERTED.get(name, 1.0))
        if kind == 'translucent':
            MEL.set_material_instance_scalar_parameter_value(mi, 'Opacity', m.get('opacity', 0.3))
        scale = None
        for tkind, param in (('color', 'ColorTex'), ('rough', 'RoughTex'),
                             ('normal', 'NormalTex'), ('alpha', 'OpacityTex')):
            t = tx.get(tkind)
            if not t:
                continue
            p = '%s/%s' % (TEX_DEST, os.path.splitext(t['image'])[0])
            if tkind == 'alpha':
                p += '_Op'
                MEL.set_material_instance_scalar_parameter_value(
                    mi, 'OpacityFromAlpha', 1.0 if t.get('channel') == 'A' else 0.0)
            tex = unreal.load_asset(p)
            if tex:
                MEL.set_material_instance_texture_parameter_value(mi, param, tex)
            if t.get('scale') and scale is None:
                scale = t['scale']
        if scale:
            MEL.set_material_instance_scalar_parameter_value(mi, 'UVTiling', float(scale[0]))
        # 두께 없는 면(꽃잎·잎·종이·포스터)은 양면 — MI 오버라이드로 켠다
        # ⚠ 끄는 쪽도 명시한다 — MI 는 재사용되므로, 한 번 켜진 오버라이드가
        #   남는다 (책 속지 MI 가 양면으로 남아 표지와 z-fighting 했다)
        if kind != 'masked':
            try:
                two = bool(m.get('two_sided'))
                o = mi.get_editor_property('base_property_overrides')
                o.set_editor_property('override_two_sided', two)
                o.set_editor_property('two_sided', two)
                mi.set_editor_property('base_property_overrides', o)
            except Exception as e:
                log('양면 오버라이드 실패 %s: %s' % (name, e))
        EAL.save_asset(path)
        made[name] = mi
    log('머티리얼 인스턴스 %d개' % len(made))
    return made


# ---------------------------------------------------------------- FBX
def combine_enum():
    E = unreal.InterchangeCombineStaticMeshesBehavior
    for n in ('COMBINE_ALL_MESHES_INTO_ONE', 'ALL_MESHES_INTO_ONE', 'COMBINE_ALL', 'ALL'):
        if hasattr(E, n):
            return getattr(E, n)
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
    mp.set_editor_property('collision', True)
    mp.set_editor_property('import_collision_according_to_mesh_name', True)
    mp.set_editor_property('force_collision_primitive_generation', False)
    cm = p.get_editor_property('common_meshes_properties')
    cm.set_editor_property('recompute_normals', False)
    cm.set_editor_property('recompute_tangents', True)
    cm.set_editor_property('vertex_color_import_option',
                           unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    # 재질·텍스처는 JSON 쪽이 정본 — FBX 가 만드는 빈 껍데기는 들이지 않는다
    try:
        matp = p.get_editor_property('material_pipeline')
        for prop, val in (('material_import', getattr(unreal.InterchangeMaterialImportOption,
                                                      'DO_NOT_IMPORT', None)),
                          ('import_materials', False)):
            if val is None:
                continue
            try:
                matp.set_editor_property(prop, val)
            except Exception:
                pass
        tp = matp.get_editor_property('texture_pipeline')
        tp.set_editor_property('import_textures', False)
    except Exception as e:
        log('재질 임포트 끄기 실패(무해): %s' % e)
    ov = unreal.InterchangePipelineStackOverride()
    ov.add_pipeline(p)
    return ov


def import_meshes():
    files = sorted(f for f in os.listdir(FBX_DIR) if f.lower().endswith('.fbx'))
    only = [s.strip().lower() for s in os.environ.get('FH_IMPORT_ONLY', '').split(',') if s.strip()]
    if only:
        files = [f for f in files if os.path.splitext(f)[0].lower() in only]
    opts = make_pipeline()
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', os.path.join(FBX_DIR, f))
        t.set_editor_property('destination_path', MESH_DEST)
        t.set_editor_property('automated', True)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('save', True)
        t.set_editor_property('options', opts)
        tasks.append(t)
    log('FBX %d개 임포트 시작' % len(tasks))
    AT.import_asset_tasks(tasks)
    log('FBX 임포트 끝')


# ---------------------------------------------------------------- 배정
def assign(made):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    n_mesh = n_slot = 0
    n_nonan = [0]
    missing = set()
    for a in ar.get_assets_by_path(MESH_DEST, recursive=True):
        obj = unreal.load_asset(str(a.package_name))
        if not isinstance(obj, unreal.StaticMesh):
            continue
        changed = False
        trans = False
        for i, s in enumerate(obj.static_materials):
            nm = str(s.material_slot_name)
            mi = made.get(nm) or made.get(nm.rsplit('_', 1)[0])
            if mi is None:
                missing.add(nm)
                continue
            obj.set_material(i, mi)
            changed = True
            n_slot += 1
            par = mi.get_editor_property('parent')
            if par and par.get_path_name().split('.')[0] == MASTERS['translucent']:
                trans = True
        # ⚠ 이 프로젝트는 임포트 메쉬에 **Nanite 가 기본으로 켜진다.** 반투명
        #   슬롯이 있으면 Nanite 대신 저폴리 폴백으로 그려지므로 그 메쉬만 끈다
        #   (유리·병·앰플). 나머지는 Nanite 그대로 — 잎의 마스크도 지원된다.
        ns = obj.get_editor_property('nanite_settings')
        if trans and ns.get_editor_property('enabled'):
            ns.set_editor_property('enabled', False)
            obj.set_editor_property('nanite_settings', ns)
            changed = True
            n_nonan[0] += 1
        if changed:
            EAL.save_asset(str(a.package_name))
            n_mesh += 1
    log('메쉬 %d개 / 슬롯 %d개 배정 / 못 찾은 슬롯 이름 %d / 반투명이라 Nanite 끔 %d' % (n_mesh, n_slot, len(missing), n_nonan[0]))
    if missing:
        log('⚠ 이름이 안 맞는 슬롯: %s' % sorted(missing)[:20])


def main():
    with open(JSON, encoding='utf-8') as f:
        data = json.load(f)
    log('=== FlowerHouse 임포트 — 재질 %d개 ===' % len(data['materials']))
    import_textures(data)
    masters = {k: build_master(k) for k in MASTERS}
    made = build_instances(data, masters)
    if not os.environ.get('FH_SKIP_MESHES'):
        import_meshes()
    assign(made)
    log('=== 끝 ===')


main()
