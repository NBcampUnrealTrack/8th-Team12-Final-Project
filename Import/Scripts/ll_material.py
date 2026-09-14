"""머티리얼 — 텍스처 임포트 + 마스터 1개 + 인스턴스 N개 + 메쉬에 배정

    UnrealEditor-Cmd.exe projectll.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_material.py"

## 구조

**마스터 `M_LL_Master` 하나에 인스턴스 75개.** 마스터를 여러 개로 나누지 않는
이유 — 텍스처가 없는 머티리얼은 파라미터를 **기본 백색 텍스처**로 두면 그만이라
스태틱 스위치가 필요 없다. 스위치를 쓰면 셰이더 퍼뮤테이션만 늘어난다.
(75개 중 텍스처를 쓰는 건 15개뿐이다.)

## 마스터 그래프

    TexCoord(0) × UVTiling(vec) ─┬─> ColorTex  ─┐
                                 ├─> RoughTex   │
                                 └─> NormalTex  │
    BaseColorTint(vec) ───────────────────────── × ─ × InstTint ─> Base Color
    RoughScalar × RoughTex.R ─────────────────────────────────> Roughness
    MetalScalar ──────────────────────────────────────────────> Metallic
    SpecScalar ───────────────────────────────────────────────> Specular
    lerp(평평(0,0,1), NormalTex, NormalStrength) ─────────────> Normal
    EmissiveColor(vec) × EmissiveStrength(scalar) ────────────> Emissive
    [반투명만] OpacityTex.A × Opacity ────────────────────────> Opacity
    [반투명만] Refraction ────────────────────────────────────> Refraction

`InstTint` = **`PerInstanceCustomData` 인덱스 2** (= 블렌더 `inst_tint`).
⚠ **기본값을 1.0 으로 둬야 한다.** HISM 이 아닌 액터(셸)에서는 이 노드가
  0 을 내므로, 기본값이 없으면 **셸이 새까맣게 된다.**

인덱스 0·1(`inst_uv_u/v`)은 UV 오프셋용인데, 지금은 나뭇결·비닐 변주에만 쓰이고
마스터에서 UV 에 더한다.

## ⚠ 옮겨지지 않는 것

- **절차 노드 6개** (`M_Steel` `M_Wallpaper` 등의 Noise) — 단색으로 나간다
- **Object 좌표 5개** — UE 는 UV 만 쓴다
둘 다 JSON 에 표시돼 있고, UE 에서 다시 만들 대상이다.
"""
import unreal
import json
import os

IMPORT_DIR = r'C:/Project/Unreal/Import'
JSON = IMPORT_DIR + '/LL_materials.json'
TEX_SRC = IMPORT_DIR + '/Textures'
TEX_DEST = '/Game/BurgerHouse/Textures'
MAT_DEST = '/Game/BurgerHouse/Materials'
# ⚠ **UE 의 블렌드 모드는 마스터 단위 속성이라 인스턴스로 못 바꾼다.**
#   그래서 종류별로 마스터를 나눈다. 하나로 묶으려면 스태틱 스위치가 필요한데
#   그건 셰이더 퍼뮤테이션을 늘린다.
# ⚠ **굴절도 마스터 단위 속성이다** — 블렌드 모드와 똑같다. 그래서 반투명을
#   둘로 가른다. 처음에 굴절을 공용 반투명 마스터에 걸었더니 **굴절이 필요
#   없는 데칼·타공판·그릴 6종까지 전부 왜곡 패스로 넘어가** 문·얼굴 데칼에
#   격자 무늬가 꼈다 (2026-08-28). 유리 4종만 굴절 마스터를 쓴다.
MASTERS = {
    'opaque':      MAT_DEST + '/M_LL_Master',
    'masked':      MAT_DEST + '/M_LL_Master_Masked',
    'translucent': MAT_DEST + '/M_LL_Master_Trans',
    'refract':     MAT_DEST + '/M_LL_Master_Refract',
}
TRANSLUCENT_KINDS = ('translucent', 'refract')
MASTER = MASTERS['opaque']

WHITE = '/Engine/EngineResources/WhiteSquareTexture'

# ⚠⚠ **`/Engine/EngineMaterials/DefaultNormal` 은 평평하지 않다.**
#   이름과 달리 **1024 × 1024 석재 타일 노멀맵**이다 (5×5 블록 격자 + 스투코
#   잔 요철). 그걸 `NormalTex` 기본값으로 물려 놨더니 **노멀맵이 없는 재질
#   66개가 전부 그 타일 요철을 뒤집어썼고**, 빨간 파티션·문에 석재 무늬가
#   찍혔다 — 2026-08-28 내내 쫓던 「격자 무늬」의 정체다.
#
#   ⚠ `blueprint_get_size_*` 가 갓 연 세션에서 **32×32**(스트리밍 밉)를
#     돌려줘서 「작고 평평하겠지」라는 오해를 굳혔다. 엔진 에셋은 이름도
#     크기 조회도 믿지 말고 **PNG 로 뽑아 눈으로 볼 것**.
#
#   -> 프로젝트에서 직접 만든 8×8 (128,128,255) 를 쓴다
#      (`Scripts/flatnormal_tex.py`).
FLAT_N = '/Game/BurgerHouse/Textures/T_FlatNormal'
# ⚠ **거칠기 슬롯의 기본 텍스처는 선형이어야 한다.** `WhiteSquareTexture` 는
#   sRGB 컬러라, 샘플러를 Linear Grayscale 로 두면 UE 가
#   `Sampler type is Linear Grayscale, should be Color` 로 컴파일을 거부한다.
#   엔진 에셋은 못 고치므로 **프로젝트로 복제해 sRGB 를 끈 판본**을 쓴다.
WHITE_LIN = TEX_DEST + '/T_White_Linear'

MEL = unreal.MaterialEditingLibrary

# ⚠ **블렌더와 UE 의 발광 단위는 다르다.** 블렌더 Emission Strength 는 W/m² 계열
#   이고 UE 는 색에 곱하는 무차원 배수(대략 nit)다. 1:1 로 옮기면 UE 에서
#   "빛나 보이지도 않는" 수준이 된다 — 실제로 그래서 매장이 어두웠다.
#   **상대 밝기는 블렌더가 정본**으로 두고(레일등 6.0 : 펜던트 5.0 : 코브 4.0),
#   절대 스케일만 여기 **한 곳**에서 곱한다. 밝기를 조절하려면 이 값만 바꾼다.
EMISSIVE_GAIN = 8.0

# ⚠⚠ **실제 라이트 액터로 전환한 기구는 발광을 낮춘다** (2026-08-29).
#   `ll_lights.py` 가 이 재질들 자리에 Spot/Point/Rect 를 심는다. 발광을 그대로
#   두면 **같은 빛을 두 번 센다** — Lumen 이 발광면에서 한 번, 라이트에서 또 한 번.
#   0 으로 죽이면 안 된다. 기구 자체가 **꺼진 것처럼** 보이기 때문이다 —
#   「렌즈는 빛나 보이되 GI 기여는 거의 없는」 세기로 남긴다.
#   화면·사인(`M_MenuFace` `M_KioskScreen` `M_ExitSign` …)은 여기 없다.
#   그것들은 **읽히는 게 목적**이라 발광 그대로 둔다.
LIGHT_CONVERTED = {
    'M_TrackLED': 0.12,      # 64  -> 7.7
    'M_SoffitLED': 0.12,     # 40  -> 4.8
    'M_PendantLED': 0.12,    # 40  -> 4.8
    'M_CoveLED': 0.14,       # 40  -> 5.6  (코브는 기구가 안 보이고 띠만 보인다)
    'M_KitchenLED': 0.12,    # 40  -> 4.8  (소핏 매입등과 같은 기구다)
}

# ⚠ **발광색도 같이 민다** (2026-08-29 3차 — 「너무 따뜻하다」).
#   `ll_lights.SPEC` 에서 라이트 색온도만 올리면 **절반만 바뀐다.** 코브 띠
#   (3.4m²)·레일 렌즈·펜던트 링이 여전히 주황으로 빛나 화면의 따뜻함을 붙든다.
#   블렌더 원본은 건드리지 않는다 — 거기선 발광이 유일한 광원이라 검증 렌더의
#   기준값이다 (`LIGHT_CONVERTED` 와 같은 이유). **UE 에서만** 갈아끼운다.
#
#       따뜻한 무리  (1.00, 0.82, 0.64) -> (1.00, 0.88, 0.76)   약 1/3 만 백색 쪽
#       중성 무리    (1.00, 0.94, 0.86) -> (1.00, 0.96, 0.91)
EMISSIVE_TINT = {
    'M_TrackLED': [1.00, 0.88, 0.76],
    'M_CoveLED': [1.00, 0.88, 0.76],
    'M_SoffitLED': [1.00, 0.96, 0.91],
    'M_PendantLED': [1.00, 0.96, 0.91],
}


# ---------------------------------------------------------------- 텍스처
def import_textures(data):
    """참조 이미지를 임포트한다. **노멀·거칠기는 sRGB 를 꺼야 한다.**"""
    imgs = {}
    for m in data['materials'].values():
        for kind, t in (m.get('textures') or {}).items():
            imgs[t['image']] = kind
    tasks = []
    for name, kind in sorted(imgs.items()):
        src = os.path.join(TEX_SRC, name)
        if not os.path.exists(src):
            unreal.log_error('  텍스처 없음: %s' % name)
            continue
        # ⚠ **이미 있으면 건너뛴다 — 그래서 PNG 를 다시 구워도 UE 는 옛 판을
        #   그대로 쓴다.** 텍스처를 고쳤으면 `LL_TEX_REIMPORT=1` 로 돌릴 것.
        #   (물결유리 노멀을 고치고도 UE 에 반영이 안 돼 한 판 헛돌았다.)
        if (not os.environ.get('LL_TEX_REIMPORT')
                and unreal.EditorAssetLibrary.does_asset_exist(
                    '%s/%s' % (TEX_DEST, os.path.splitext(name)[0]))):
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', src)
        t.set_editor_property('destination_path', TEX_DEST)
        t.set_editor_property('automated', True)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('save', True)
        tasks.append(t)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    # ⚠ 임포트 뒤에 압축·sRGB 를 고쳐야 한다. 노멀맵을 sRGB 로 두면 감마가
    #   한 번 더 걸려 요철이 뭉개지고, 거칠기도 값이 어긋난다.
    fixed = 0
    for name, kind in sorted(imgs.items()):
        p = '%s/%s' % (TEX_DEST, os.path.splitext(name)[0])
        tex = unreal.load_asset(p)
        if tex is None:
            continue
        if kind == 'normal':
            tex.set_editor_property('srgb', False)
            tex.set_editor_property(
                'compression_settings',
                unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif kind in ('rough', 'metal'):
            tex.set_editor_property('srgb', False)
            tex.set_editor_property(
                'compression_settings',
                unreal.TextureCompressionSettings.TC_MASKS)
        unreal.EditorAssetLibrary.save_asset(p)
        fixed += 1
    unreal.log_warning('  텍스처 %d개 (sRGB/압축 설정 %d개)' % (len(imgs), fixed))
    return imgs


def ensure_flat_normal():
    """평평한 노멀 기본 텍스처를 보장한다.

    ⚠ **어느 재질도 이걸 참조하지 않으므로 `import_textures` 가 안 가져온다.**
      마스터의 기본값으로만 쓰이기 때문이다 — 따로 넣어 줘야 한다.
      (`T_White_Linear` 를 따로 만드는 것과 같은 이유.)
    """
    if not unreal.EditorAssetLibrary.does_asset_exist(FLAT_N):
        src = os.path.join(TEX_SRC, 'T_FlatNormal.png')
        if not os.path.exists(src):
            unreal.log_error('  T_FlatNormal.png 이 없다 — '
                             'Scripts/flatnormal_tex.py 를 먼저 돌릴 것')
            return '/Engine/EngineMaterials/DefaultNormal'
        t = unreal.AssetImportTask()
        t.set_editor_property('filename', src)
        t.set_editor_property('destination_path', TEX_DEST)
        t.set_editor_property('automated', True)
        t.set_editor_property('replace_existing', True)
        t.set_editor_property('save', True)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(FLAT_N)
    if tex is None:
        unreal.log_error('  T_FlatNormal 임포트 실패')
        return '/Engine/EngineMaterials/DefaultNormal'
    # ⚠ sRGB 를 켜 두면 128 이 0.5 로 복호화되지 않아 **평평하지 않게** 된다
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('compression_settings',
                            unreal.TextureCompressionSettings.TC_NORMALMAP)
    unreal.EditorAssetLibrary.save_asset(FLAT_N)
    return FLAT_N


def ensure_linear_white():
    if not unreal.EditorAssetLibrary.does_asset_exist(WHITE_LIN):
        unreal.EditorAssetLibrary.duplicate_asset(WHITE, WHITE_LIN)
    t = unreal.load_asset(WHITE_LIN)
    if t:
        t.set_editor_property('srgb', False)
        t.set_editor_property('compression_settings',
                              unreal.TextureCompressionSettings.TC_MASKS)
        unreal.EditorAssetLibrary.save_asset(WHITE_LIN)
    return WHITE_LIN if t else WHITE


def set_refraction_mode(mat, on=True):
    """마스터의 굴절 모드를 정한다 — `on` 이면 `PixelNormalOffset`, 아니면 끔.

    ⚠ **끄는 쪽도 명시해야 한다.** 굴절이 켜진 마스터는 그 자식 MI 가
      `Refraction = 1.0`(굴절 없음)이어도 **왜곡 패스로 넘어간다.** 그
      패스가 장면색을 다시 샘플링하면서 얇은 데칼에 격자 무늬가 낀다.

    ⚠ **속성 이름과 열거자 철자가 UE 판본마다 다르다.** UE 5.8 에서 확인한
      정답은 속성 `refraction_method` · 열거자 `RefractionMode.RM_PIXEL_NORMAL_OFFSET`
      이다 (`refraction_mode` 는 없고, 접두사 `RM_` 을 빼면 못 찾는다).
      다른 판본을 대비해 후보를 늘어놓고 **안 되면 조용히 넘어간다** — 굴절은
      없어도 나머지가 다 서야 하기 때문이다 (굴절 하나 때문에 머티리얼
      75개가 회색 체커로 떨어지면 손해가 훨씬 크다).
    """
    val = None
    e = getattr(unreal, 'RefractionMode', None)
    want = (('RM_PIXEL_NORMAL_OFFSET', 'REFRACTION_MODE_PIXEL_NORMAL_OFFSET',
             'PIXEL_NORMAL_OFFSET') if on
            else ('RM_NONE', 'REFRACTION_MODE_NONE', 'NONE'))
    if e is not None:
        for nm in want:
            val = getattr(e, nm, None)
            if val is not None:
                break
    if val is None:
        unreal.log_warning('  ⚠ 굴절 모드 열거자를 못 찾았다 — 기본 모드로 둔다')
        return False
    for prop in ('refraction_method', 'refraction_mode'):
        try:
            mat.set_editor_property(prop, val)
            unreal.log_warning('  굴절 모드: %s = %s'
                               % (prop, 'PixelNormalOffset' if on else 'None'))
            return True
        except Exception:
            continue
    unreal.log_warning('  ⚠ 굴절 모드 속성을 못 찾았다 — 기본 모드로 둔다')
    return False


# ---------------------------------------------------------------- 마스터
def build_master(kind='opaque'):
    # ⚠ **마스터를 지우고 다시 만들면 안 된다.** MI 75개가 참조하고 있어서
    #   `delete_asset` 이 조용히 실패하고, 그러면 `create_asset` 이 **None** 을
    #   돌려준다. 그 None 에 표현식을 만들면 전부 None 이 되어
    #   `AttributeError: 'NoneType' object has no attribute 'set_editor_property'`
    #   로 죽는다 (2026-08-28 실제로 그랬다).
    #   -> 있으면 **재사용하고 표현식만 비운다.** 참조가 안 끊긴다.
    path = MASTERS[kind]
    name = path.split('/')[-1]
    mat = unreal.load_asset(path)
    if mat is None:
        at = unreal.AssetToolsHelpers.get_asset_tools()
        mat = at.create_asset(name, MAT_DEST, unreal.Material,
                              unreal.MaterialFactoryNew())
    else:
        MEL.delete_all_material_expressions(mat)
    if mat is None:
        raise RuntimeError('마스터를 만들지도 열지도 못했다: %s' % path)
    if kind == 'masked':
        mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    elif kind in TRANSLUCENT_KINDS:
        mat.set_editor_property('blend_mode',
                                unreal.BlendMode.BLEND_TRANSLUCENT)
        # 표면광 계산을 켜야 유리가 유리처럼 보인다 (기본은 볼륨 방식)
        mat.set_editor_property(
            'translucency_lighting_mode',
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        # ⚠ 굴절은 **굴절 마스터에만**. 평범한 반투명은 명시적으로 꺼 둔다 —
        #   켜져 있으면 데칼이 왜곡 패스로 넘어가 격자 무늬가 낀다
        set_refraction_mode(mat, kind == 'refract')

    def expr(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def sp(name, val, x, y, group='', lo=0.0, hi=1.0):
        e = expr(unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('default_value', val)
        e.set_editor_property('group', group)
        e.set_editor_property('slider_min', lo)
        e.set_editor_property('slider_max', hi)
        return e

    def vp(name, r, g, b, x, y, group=''):
        e = expr(unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('default_value', unreal.LinearColor(r, g, b, 1))
        e.set_editor_property('group', group)
        return e

    def mask(src, x, y, r=True, g=True, b=False, a=False):
        """⚠ **`VectorParameter` 는 float4 를 낸다.** 그걸 `TextureCoordinate`
        (float2)나 텍스처 RGB(float3)와 그대로 곱하면 UE 가
        `Arithmetic between types float2 and float4 are undefined` 로
        **컴파일에 실패하고, 실패한 머티리얼은 회색 체커(WorldGridMaterial)로
        폴백한다.** 2026-08-28 에 실제로 씬 전체가 그렇게 나왔다.
        채널 마스크로 폭을 맞춘다.
        """
        m = expr(unreal.MaterialExpressionComponentMask, x, y)
        m.set_editor_property('r', r)
        m.set_editor_property('g', g)
        m.set_editor_property('b', b)
        m.set_editor_property('a', a)
        MEL.connect_material_expressions(src, '', m, '')
        return m

    def tp(name, path, x, y, sampler=None):
        e = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        e.set_editor_property('parameter_name', name)
        e.set_editor_property('texture', unreal.load_asset(path))
        if sampler:
            e.set_editor_property('sampler_type', sampler)
        return e

    # ---- UV ----
    uv = expr(unreal.MaterialExpressionTextureCoordinate, -1500, 0)
    # ⚠ **스칼라다.** `VectorParameter` 로 두면 float4 가 나와
    #   `Arithmetic between types float2 and float4 are undefined` 로 죽는다.
    #   마스크로 float2 를 뽑아도 되지만, 이 씬의 타일링은 **전부 등방**이라
    #   (2.5/2.5, 0.833/0.833, 1.818/1.818 …) 스칼라 하나면 충분하다.
    tile = sp('UVTiling', 1.0, -1500, 150, 'UV', 0.01, 10.0)
    uvmul = expr(unreal.MaterialExpressionMultiply, -1250, 40)
    MEL.connect_material_expressions(uv, '', uvmul, 'A')
    MEL.connect_material_expressions(tile, '', uvmul, 'B')

    # ⚠ 인스턴스별 UV 오프셋 (`inst_uv_u/v` = 커스텀데이터 0·1)
    cd0 = expr(unreal.MaterialExpressionPerInstanceCustomData, -1500, 320)
    cd0.set_editor_property('data_index', 0)
    cd0.set_editor_property('const_default_value', 0.0)
    cd1 = expr(unreal.MaterialExpressionPerInstanceCustomData, -1500, 400)
    cd1.set_editor_property('data_index', 1)
    cd1.set_editor_property('const_default_value', 0.0)
    off = expr(unreal.MaterialExpressionAppendVector, -1300, 360)
    MEL.connect_material_expressions(cd0, '', off, 'A')
    MEL.connect_material_expressions(cd1, '', off, 'B')
    uvadd = expr(unreal.MaterialExpressionAdd, -1050, 100)
    MEL.connect_material_expressions(uvmul, '', uvadd, 'A')
    MEL.connect_material_expressions(off, '', uvadd, 'B')

    # ---- 색 ----
    ctex = tp('ColorTex', WHITE, -800, -200)
    MEL.connect_material_expressions(uvadd, '', ctex, 'UVs')
    tint = vp('BaseColorTint', 1, 1, 1, -800, 100, 'Base')
    tint_rgb = mask(tint, -640, 100, True, True, True, False)
    m1 = expr(unreal.MaterialExpressionMultiply, -500, -50)
    MEL.connect_material_expressions(ctex, 'RGB', m1, 'A')
    MEL.connect_material_expressions(tint_rgb, '', m1, 'B')

    # ⚠ 인스턴스 명도 변주 — **기본값 1.0** 이 아니면 셸이 새까매진다
    # ⚠ 속성 이름이 `const_default_value` 다 — `default_value` 는 없다
    cd2 = expr(unreal.MaterialExpressionPerInstanceCustomData, -800, 260)
    cd2.set_editor_property('data_index', 2)
    cd2.set_editor_property('const_default_value', 1.0)
    m2 = expr(unreal.MaterialExpressionMultiply, -250, -20)
    MEL.connect_material_expressions(m1, '', m2, 'A')
    MEL.connect_material_expressions(cd2, '', m2, 'B')

    # ⚠ **버텍스 컬러** — `M_SofaVinyl` 의 진짜 색이 여기 들어 있다
    #   (`SofaCol`: 노랑 / 차콜 / 회색 3색). 안 읽으면 차콜·회색 부분이
    #   통째로 사라지고 소파가 전부 노랗게 나온다.
    #   ⚠ 마스터는 75개가 공유하므로 **기본값 0 인 스위치로 막아 둔다** —
    #     버텍스 컬러가 없는 메쉬에서 이 노드가 무엇을 내는지에 기대지 않는다.
    vc = expr(unreal.MaterialExpressionVertexColor, -800, 480)
    one = expr(unreal.MaterialExpressionConstant3Vector, -800, 560)
    one.set_editor_property('constant', unreal.LinearColor(1, 1, 1, 1))
    uvc = sp('UseVertexColor', 0.0, -800, 620, 'Base')
    vlerp = expr(unreal.MaterialExpressionLinearInterpolate, -500, 520)
    MEL.connect_material_expressions(one, '', vlerp, 'A')
    MEL.connect_material_expressions(vc, 'RGB', vlerp, 'B')
    MEL.connect_material_expressions(uvc, '', vlerp, 'Alpha')
    m3 = expr(unreal.MaterialExpressionMultiply, -120, 40)
    MEL.connect_material_expressions(m2, '', m3, 'A')
    MEL.connect_material_expressions(vlerp, '', m3, 'B')
    MEL.connect_material_property(m3, '', unreal.MaterialProperty.MP_BASE_COLOR)

    # ---- 거칠기 / 금속 / 스페큘러 ----
    # ⚠ **샘플러 타입은 텍스처의 압축 설정과 짝이어야 한다.** 안 맞으면
    #   `Sampler type is X, should be Y for <텍스처>` 로 컴파일이 거부된다.
    #   여기 물리는 것은 기본값 `T_White_Linear` 도, 실제 거칠기맵도 전부
    #   **TC_MASKS** 다 (`import_textures` 가 그렇게 설정한다) -> `MASKS`.
    #   세 슬롯의 짝: ColorTex=TC_Default/COLOR · RoughTex=TC_MASKS/MASKS ·
    #                 NormalTex=TC_NORMALMAP/NORMAL
    rtex = tp('RoughTex', ensure_linear_white(), -800, 420,
              unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    MEL.connect_material_expressions(uvadd, '', rtex, 'UVs')
    rs = sp('Roughness', 0.5, -800, 700, 'Surface')
    rm = expr(unreal.MaterialExpressionMultiply, -500, 500)
    MEL.connect_material_expressions(rtex, 'R', rm, 'A')
    MEL.connect_material_expressions(rs, '', rm, 'B')
    MEL.connect_material_property(rm, '', unreal.MaterialProperty.MP_ROUGHNESS)

    ms = sp('Metallic', 0.0, -500, 780, 'Surface')
    MEL.connect_material_property(ms, '', unreal.MaterialProperty.MP_METALLIC)
    ss = sp('Specular', 0.5, -500, 860, 'Surface')
    MEL.connect_material_property(ss, '', unreal.MaterialProperty.MP_SPECULAR)

    # ---- 노멀 ----
    ntex = tp('NormalTex', ensure_flat_normal(), -800, 960,
              unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_expressions(uvadd, '', ntex, 'UVs')
    # ⚠ **블렌더 `Normal Map` 노드의 Strength 를 받는다.** 같은 노멀맵을 세기만
    #   달리 쓰는 경우가 있다 — 물결유리가 그렇다 (`M_BinGlass` 1.0 은 뿌옇게,
    #   `M_Glass` 0.35 는 결만 남겨 밖이 보이게). 이걸 안 받으면 전면 유리까지
    #   다 불투명해진다. 평평한 노멀(0,0,1)과 lerp 하는 게 표준 방식이다.
    flat = expr(unreal.MaterialExpressionConstant3Vector, -800, 1120)
    flat.set_editor_property('constant', unreal.LinearColor(0, 0, 1, 1))
    nstr = sp('NormalStrength', 1.0, -800, 1180, 'Surface', 0.0, 4.0)
    nlerp = expr(unreal.MaterialExpressionLinearInterpolate, -500, 1060)
    MEL.connect_material_expressions(flat, '', nlerp, 'A')
    MEL.connect_material_expressions(ntex, 'RGB', nlerp, 'B')
    MEL.connect_material_expressions(nstr, '', nlerp, 'Alpha')

    normal_out = nlerp

    # ---- 리브드(플루티드) 유리 — 텍스처 없이 셰이더에서 만든다 ----
    # ⚠ **무늬유리를 노멀맵으로 하면 타일링이 보인다.** 2048² 를 300mm 마다
    #   반복시켰더니 5.1m 전면 유리에서 17 × 11 번 반복돼 **눈이 그 주기를
    #   격자로 읽었다.** 이음매를 없애도(순환 차분) 모티프 반복은 남는다.
    #   리브드 유리는 무늬가 **원래 주기적**이라 그 문제 자체가 없다.
    #
    #       h(u)  = (깊이/2)·cos(2π·u/피치)
    #       dh/du = −(깊이·π/피치)·sin(2π·u/피치)
    #       접선공간 법선 = normalize(dh/du, 0, 1)
    #
    # ⚠ **UV 가 월드 미터라서 성립한다** (`glass_uv.py` 월드 박스 투영).
    #   피치를 미터로 주면 5.1m 유리든 0.42m 문 패널이든 골 간격이 같다.
    # ⚠ UE `Sine` 은 `sin(2π·x/Period)` 고 Period 기본값이 1 이다 —
    #   그래서 `u/피치` 를 그대로 넣으면 된다.
    if kind == 'refract':
        pitch = sp('FlutePitch', 0.025, -800, 620, 'Flute', 0.005, 0.2)
        depth = sp('FluteDepth', 0.004, -800, 700, 'Flute', 0.0, 0.05)
        amt = sp('FluteAmount', 0.0, -800, 780, 'Flute')
        umask = mask(uvadd, -1000, 560, True, False, False, False)
        div = expr(unreal.MaterialExpressionDivide, -640, 560)
        MEL.connect_material_expressions(umask, '', div, 'A')
        MEL.connect_material_expressions(pitch, '', div, 'B')
        sine = expr(unreal.MaterialExpressionSine, -500, 560)
        MEL.connect_material_expressions(div, '', sine, '')
        pi = expr(unreal.MaterialExpressionConstant, -800, 860)
        pi.set_editor_property('r', 3.1415927)
        dpi = expr(unreal.MaterialExpressionMultiply, -640, 820)
        MEL.connect_material_expressions(depth, '', dpi, 'A')
        MEL.connect_material_expressions(pi, '', dpi, 'B')
        amp = expr(unreal.MaterialExpressionDivide, -500, 820)
        MEL.connect_material_expressions(dpi, '', amp, 'A')
        MEL.connect_material_expressions(pitch, '', amp, 'B')
        nx = expr(unreal.MaterialExpressionMultiply, -360, 640)
        MEL.connect_material_expressions(sine, '', nx, 'A')
        MEL.connect_material_expressions(amp, '', nx, 'B')
        zero = expr(unreal.MaterialExpressionConstant, -360, 720)
        zero.set_editor_property('r', 0.0)
        one1 = expr(unreal.MaterialExpressionConstant, -360, 780)
        one1.set_editor_property('r', 1.0)
        xy = expr(unreal.MaterialExpressionAppendVector, -220, 660)
        MEL.connect_material_expressions(nx, '', xy, 'A')
        MEL.connect_material_expressions(zero, '', xy, 'B')
        xyz = expr(unreal.MaterialExpressionAppendVector, -120, 700)
        MEL.connect_material_expressions(xy, '', xyz, 'A')
        MEL.connect_material_expressions(one1, '', xyz, 'B')
        fnorm = expr(unreal.MaterialExpressionNormalize, -20, 700)
        MEL.connect_material_expressions(xyz, '', fnorm, '')
        flerp = expr(unreal.MaterialExpressionLinearInterpolate, 100, 900)
        MEL.connect_material_expressions(nlerp, '', flerp, 'A')
        MEL.connect_material_expressions(fnorm, '', flerp, 'B')
        MEL.connect_material_expressions(amt, '', flerp, 'Alpha')
        normal_out = flerp

    MEL.connect_material_property(normal_out, '',
                                  unreal.MaterialProperty.MP_NORMAL)

    # ---- 발광 ----
    # ⚠⚠ **색 텍스처를 곱한다.** 안 그러면 발광이 면 전체를 **균일한 색**으로
    #   덮어써서 그림이 사라진다. 실제로 피난구유도등이 그랬다 —
    #   `ColorTex` 에 `T_ExitSign` 이 제대로 물려 있는데도 흰색 발광 2.8 이
    #   덮어써서 「데칼이 적용 안 됐다」로 보였다 (`UE_TROUBLESHOOTING.md` #55).
    #   텍스처가 없는 재질은 `ColorTex` 기본값이 흰색이라 **아무 영향이 없다.**
    ec = vp('EmissiveColor', 0, 0, 0, -800, 1240, 'Emissive')
    es = sp('EmissiveStrength', 0.0, -800, 1330, 'Emissive', 0.0, 20.0)
    ec_rgb = mask(ec, -640, 1240, True, True, True, False)
    em = expr(unreal.MaterialExpressionMultiply, -500, 1280)
    MEL.connect_material_expressions(ec_rgb, '', em, 'A')
    MEL.connect_material_expressions(es, '', em, 'B')
    em2 = expr(unreal.MaterialExpressionMultiply, -360, 1280)
    MEL.connect_material_expressions(em, '', em2, 'A')
    MEL.connect_material_expressions(ctex, 'RGB', em2, 'B')
    MEL.connect_material_property(em2, '',
                                  unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # ---- 불투명도 ----
    if kind == 'masked':
        # 알파 텍스처로 구멍을 뚫는다 (`M_PerfPanel` 타공판 등)
        atex = tp('OpacityTex', WHITE, -800, 1520)
        MEL.connect_material_expressions(uvadd, '', atex, 'UVs')
        MEL.connect_material_property(
            atex, 'A', unreal.MaterialProperty.MP_OPACITY_MASK)
    elif kind in TRANSLUCENT_KINDS:
        # ⚠ **스칼라만으로는 부족하다.** 유리는 스칼라 하나면 되지만, 데칼은
        #   알파 **텍스처**로 모양이 정해진다. 둘을 곱해 한 마스터가 양쪽을
        #   다 받게 한다 — 텍스처가 없으면 기본 백색이라 스칼라만 남는다.
        atex = tp('OpacityTex', WHITE, -800, 1520)
        MEL.connect_material_expressions(uvadd, '', atex, 'UVs')
        op = sp('Opacity', 1.0, -800, 1800, 'Surface')
        omul = expr(unreal.MaterialExpressionMultiply, -500, 1600)
        MEL.connect_material_expressions(atex, 'A', omul, 'A')
        MEL.connect_material_expressions(op, '', omul, 'B')
        MEL.connect_material_property(
            omul, '', unreal.MaterialProperty.MP_OPACITY)

        # ---- 굴절 — 무늬유리가 무늬유리로 보이는 이유 ----
        # ⚠ **노멀맵만으로는 뒤가 안 흩어진다.** 노멀은 반사·하이라이트만
        #   바꾼다. 레퍼런스(`유리.webp`)처럼 **뒤가 잘게 깨져 보이려면**
        #   굴절이 있어야 한다.
        #   `PixelNormalOffset` 모드를 쓴다 — 픽셀 노멀로 배경을 밀어내는
        #   방식이라 **얇은 판유리에 정확히 맞고** 굴절 광선 추적보다 싸다.
        #   (`IndexOfRefraction` 은 두께가 있는 덩어리용이다.)
        # ⚠ **굴절 마스터에만 연결한다.** 평범한 반투명 마스터에 달면
        #   데칼까지 왜곡 패스로 넘어간다 (위 `MASTERS` 주석 참조).
        if kind == 'refract':
            rf = sp('Refraction', 1.0, -800, 1900, 'Surface', 1.0, 2.0)
            MEL.connect_material_property(
                rf, '', unreal.MaterialProperty.MP_REFRACTION)

    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    unreal.log_warning('  마스터 [%s] %s' % (kind, path))
    return mat


# ---------------------------------------------------------------- 인스턴스
def build_instances(data, masters):
    at = unreal.AssetToolsHelpers.get_asset_tools()
    made = {}
    for name, m in sorted(data['materials'].items()):
        path = '%s/MI_%s' % (MAT_DEST, name)
        # ⚠ **지우고 다시 만들면 안 된다.** 스태틱메쉬 23개가 이 MI 들을
        #   참조하고 있어서 `delete_asset` 이 조용히 실패하고, 그러면
        #   `create_asset` 이 **None** 을 돌려준다. 그 None 이 `made` 에 들어가
        #   배정 단계에서 **91개 전부 "못 찾음"** 이 된다 (2026-08-28 실제 발생).
        #   마스터에서 겪은 것과 같은 함정이다 — **있으면 재사용한다.**
        mi = unreal.load_asset(path)
        if mi is None:
            mi = at.create_asset('MI_' + name, MAT_DEST,
                                 unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            unreal.log_error('  MI 생성 실패: %s' % name)
            continue
        kind = m.get('kind', 'opaque')
        MEL.set_material_instance_parent(mi, masters[kind])

        tx0 = m.get('textures') or {}
        # ⚠ **텍스처가 물린 채널은 스칼라/벡터를 중립값으로 둔다.**
        #   블렌더는 이미지가 연결되면 `default_value` 를 무시하지만, UE 마스터는
        #   `텍스처 × 틴트` 로 곱하므로 그 값을 그대로 넣으면 이중 적용된다.
        #   실제로 텍스처 머티리얼 15개가 Base Color 0.8 로 나가 **20% 어두웠다.**
        #   ⚠ **직결(`direct`)일 때만** 중립화한다. 중간에 Mix 가 끼면 블렌더도
        #     그 색을 쓰므로 살려야 한다 — `M_SofaVinyl` 이 그 경우다
        #     (가죽 컬러맵을 명암 변조로만 쓰고 색은 따로 준다).
        _c = tx0.get('color')
        bc = ([1.0, 1.0, 1.0] if (_c and _c.get('direct'))
              else (m.get('base_color') or [1, 1, 1]))
        MEL.set_material_instance_vector_parameter_value(
            mi, 'BaseColorTint', unreal.LinearColor(bc[0], bc[1], bc[2], 1))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'Roughness',
            1.0 if (tx0.get('rough') or {}).get('direct')
            else m.get('roughness', 0.5))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'Metallic', m.get('metallic', 0.0))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'Specular', m.get('specular', 0.5))
        ec = EMISSIVE_TINT.get(name) or m.get('emissive_color') or [0, 0, 0]
        MEL.set_material_instance_vector_parameter_value(
            mi, 'EmissiveColor', unreal.LinearColor(ec[0], ec[1], ec[2], 1))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'EmissiveStrength',
            m.get('emissive_strength', 0.0) * EMISSIVE_GAIN
            * LIGHT_CONVERTED.get(name, 1.0))

        # ⚠ 투과는 **불투명도로 뒤집어** 넣는다. 블렌더 transmission 1.0(완전
        #   투명)이 UE 에서는 opacity ~0 이다. 0 으로 두면 아예 사라지므로
        #   0.94 만 반영해 얇게 남긴다.
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'NormalStrength', m.get('normal_strength', 1.0))
        MEL.set_material_instance_scalar_parameter_value(
            mi, 'UseVertexColor', 1.0 if m.get('vertex_color') else 0.0)

        if kind in TRANSLUCENT_KINDS:
            # 투과가 있으면 그 값으로, 알파 텍스처만 있으면 1.0(텍스처가 결정)
            trans = m.get('transmission', 0.0)
            MEL.set_material_instance_scalar_parameter_value(
                mi, 'Opacity',
                1.0 - trans * 0.94 if trans > 0.01 else 1.0)
            # ⚠ **투과가 있는 것에만 굴절을 준다.** 데칼·타공판·그릴도
            #   반투명 마스터를 쓰는데, 거기에 굴절이 걸리면 **글자와 로고가
            #   배경째 밀려 흔들린다.** 1.0 = 굴절 없음.
            MEL.set_material_instance_scalar_parameter_value(
                mi, 'Refraction',
                m.get('ior', 1.5) if trans > 0.01 else 1.0)

            # ⚠ 리브드 유리는 **파라미터만** 넘어온다 (텍스처가 없다).
            #   `flute_pitch` 가 있는 재질만 켠다 — 나머지는 기본 0 이라
            #   그래프가 붙어 있어도 아무 영향이 없다.
            fp = m.get('flute_pitch')
            if fp:
                MEL.set_material_instance_scalar_parameter_value(
                    mi, 'FlutePitch', float(fp))
                MEL.set_material_instance_scalar_parameter_value(
                    mi, 'FluteDepth', float(m.get('flute_depth', 0.004)))
                MEL.set_material_instance_scalar_parameter_value(
                    mi, 'FluteAmount', 1.0)
        tx = tx0
        scale = None
        for tkind, param in (('color', 'ColorTex'), ('rough', 'RoughTex'),
                             ('normal', 'NormalTex'), ('alpha', 'OpacityTex')):
            t = tx.get(tkind)
            if not t:
                continue
            p = '%s/%s' % (TEX_DEST, os.path.splitext(t['image'])[0])
            tex = unreal.load_asset(p)
            if tex:
                MEL.set_material_instance_texture_parameter_value(
                    mi, param, tex)
            if t.get('scale') and scale is None:
                scale = t['scale']
        if scale:
            # 등방이므로 X 성분 하나만 쓴다 (위 마스터 주석 참조)
            MEL.set_material_instance_scalar_parameter_value(
                mi, 'UVTiling', float(scale[0]))

        unreal.EditorAssetLibrary.save_asset(path)
        made[name] = mi
    unreal.log_warning('  머티리얼 인스턴스 %d개' % len(made))
    return made


# ---------------------------------------------------------------- 배정
def assign(made):
    """스태틱메쉬의 슬롯 이름으로 MI 를 물린다.

    ⚠ 슬롯 이름은 FBX 에서 온 **블렌더 머티리얼 이름**이다. 임포트 때 이름이
      바뀌었으면(중복 등) 여기서 못 찾는다 — 못 찾은 것은 반드시 보고한다.
    """
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    n_mesh = n_slot = n_miss = 0
    missing = set()
    for base in ('/Game/BurgerHouse/Shell', '/Game/BurgerHouse/Props'):
        for a in ar.get_assets_by_path(base, recursive=True):
            obj = unreal.load_asset(str(a.package_name))
            if not isinstance(obj, unreal.StaticMesh):
                continue
            sm = obj.static_materials
            changed = False
            for i, s in enumerate(sm):
                nm = str(s.material_slot_name)
                mi = made.get(nm)
                if mi is None:
                    # `M_Foo_1` 처럼 뒤에 숫자가 붙는 경우가 있다
                    mi = made.get(nm.rsplit('_', 1)[0])
                if mi is None:
                    missing.add(nm)
                    n_miss += 1
                    continue
                obj.set_material(i, mi)
                changed = True
                n_slot += 1
            if changed:
                unreal.EditorAssetLibrary.save_asset(str(a.package_name))
                n_mesh += 1
    unreal.log_warning('  메쉬 %d개 / 슬롯 %d개 배정 / 못 찾음 %d개'
                       % (n_mesh, n_slot, n_miss))
    if missing:
        unreal.log_warning('  ⚠ 이름이 안 맞는 슬롯: %s'
                           % sorted(missing)[:12])


def nuke():
    """머티리얼 자산을 **완전히** 지운다.

    ⚠ 순서가 전부다. 참조가 남아 있으면 `delete_asset` 이 **조용히 실패**하고
      `create_asset` 이 None 을 돌려준다 — 그러면 그 뒤가 전부 무너진다.
        ① 스태틱메쉬 슬롯을 비워 **MI 참조**를 끊고
        ② MI 를 지워 **마스터 참조**를 끊고
        ③ 마스터를 지운다
      재사용(표현식만 비우기)으로는 **옛 파라미터 이름이 캐시에 남는다** —
      실제로 `UVTiling` 이 스칼라·벡터 양쪽에 잡혀 있었다. 같은 이름 다른 타입은
      그 자체가 컴파일 에러다.
    """
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.search_all_assets(True)
    # ① 메쉬 슬롯 비우기
    n = 0
    for base in ('/Game/BurgerHouse/Shell', '/Game/BurgerHouse/Props'):
        for a in ar.get_assets_by_path(base, recursive=True):
            m = unreal.load_asset(str(a.package_name))
            if not isinstance(m, unreal.StaticMesh):
                continue
            for i in range(len(m.static_materials)):
                m.set_material(i, None)
                n += 1
            unreal.EditorAssetLibrary.save_asset(str(a.package_name))
    unreal.log_warning('  슬롯 %d개 비움 (MI 참조 해제)' % n)
    # ② MI 삭제  ③ 마스터 삭제
    d = 0
    for a in list(ar.get_assets_by_path(MAT_DEST, recursive=True)):
        pth = str(a.package_name)
        if pth.split('/')[-1].startswith('MI_'):
            if unreal.EditorAssetLibrary.delete_asset(pth):
                d += 1
    ok = []
    for mp in MASTERS.values():
        if unreal.EditorAssetLibrary.does_asset_exist(mp):
            ok.append(unreal.EditorAssetLibrary.delete_asset(mp))
    unreal.log_warning('  MI %d개 삭제 / 마스터 삭제 %s' % (d, ok))


def main():
    with open(JSON, encoding='utf-8') as f:
        data = json.load(f)
    nuke()
    unreal.log_warning('=== 머티리얼 — %d개 ===' % len(data['materials']))
    import_textures(data)
    masters = {k: build_master(k) for k in MASTERS}
    made = build_instances(data, masters)
    assign(made)
    n_p = sum(1 for m in data['materials'].values() if m.get('procedural'))
    n_o = sum(1 for m in data['materials'].values() if m.get('object_coord'))
    unreal.log_warning('=== 완료 ===')
    unreal.log_warning('  ⚠ 절차 노드 %d개 / Object 좌표 %d개는 단색으로 갔다 —'
                       ' UE 에서 다시 만들 대상' % (n_p, n_o))


main()
