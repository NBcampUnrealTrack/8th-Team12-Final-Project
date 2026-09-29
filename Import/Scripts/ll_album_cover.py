"""Album 액터를 CD 팩처럼 — 앞면에 앨범 커버, 나머지는 케이스 플라스틱.

    UnrealEditor-Cmd.exe Febricula.uproject -run=pythonscript
        -script=".../Import/Scripts/ll_album_cover.py"

## 액터

`Album` = `StaticMeshActor_3`, `/Engine/BasicShapes/Cube`, 슬롯 0 은
`WorldGridMaterial` (기본 격자). 스케일 (0.188, 0.018, -0.211) → 18.8 × 1.8 ×
21.1 cm. **가장 얇은 축은 Y** 이므로 큰 면은 ±Y 다.

`LL_Light_PlayerStart` (300, -200, 160) 방향과 로컬 +Y 법선의 내적이 +0.32 라
**로컬 +Y 가 앞면**이다.

## ⚠ 스케일 Z 가 음수다 (-0.211) — 두 군데서 문제가 된다

**1. 두께 축 판정.** 그냥 정렬하면 -0.211 이 최솟값이라 Z 가 얇은 축으로
   잡힌다. **절댓값**으로 봐야 Y (0.018) 가 나온다.

**2. 법선이 뒤집힌다.** 행렬식이 음수라 렌더러가 컬링·법선을 뒤집는다. 그래서
   면 판정에 **법선을 쓰지 않는다** — 로컬 **위치**로 고른다 (`LocalPosition.y
   > 25` 가 앞면). 위치는 월드 변환 전 값이라 스케일 부호와 무관하다.
   같은 이유로 UV 도 큐브의 내장 UV 가 아니라 로컬 위치에서 만든다 — 엔진
   큐브의 UV 레이아웃을 몰라도 된다.

   다만 **보이는 방향**은 음수 스케일의 영향을 받는다 (거울상·상하반전).
   그건 아래에서 계산해 `FlipU` · `FlipV` 파라미터로 잡는다.

## 그래프 (M_AlbumCase)

    LocalPosition ─┬─ .x ─ (+50)/100 ───────── u ─┬─ lerp(u, 1-u, FlipU) ─┐
                   ├─ .z ─ (50-z)/100 ───────── v ─┴─ lerp(v, 1-v, FlipV) ─┤
                   └─ .y ─ (y-25)*1000 ─ clamp01 ─ frontMask               │
                                                        │      Cover(UVs) ─┘
    lerp(CaseColor, Cover.RGB, frontMask) ─────────────────> Base Color
    lerp(CaseRough, CoverRough, frontMask) ────────────────> Roughness

`(y-25)*1000 → clamp01` 은 계단 함수다. `If` 노드를 쓰지 않는 이유는 핀 이름
(`A > B` 등)이 버전마다 달라 파이썬 연결이 깨지기 쉬워서다.

## FlipU · FlipV 를 계산으로 정한다

이미지의 오른쪽 = 로컬 +X, 이미지의 위 = 로컬 +Z 로 UV 를 만들었다. 월드에서
그 방향들은 회전·스케일부호를 거쳐 이렇게 된다 —

    U = (로컬+X 의 월드방향) × sign(scale.x)
    V = (로컬+Z 의 월드방향) × sign(scale.z)
    N = (로컬+Y 의 월드방향)          ← 앞면 법선

바로 서고 거울상이 아니려면 둘 다 만족해야 한다:

    (a) V·월드up > 0            이미지의 위가 위를 향한다
    (b) (U × V)·N < 0           바깥(+N)에서 볼 때 u 가 오른쪽으로 간다

⚠⚠ (b) 의 부등호 방향에 주의 — **UE 는 왼손 좌표계다.** 카메라의 right 는
    `up × forward` 이고(단위 로테이터: (0,0,1)×(1,0,0) = (0,1,0) = right),
    바깥에서 보면 forward = -N 이므로 right = -(V × N) 이다. 오른손 좌표계
    감각으로 `> 0` 이라고 썼다가 좌우가 뒤집혔다 (2026-09-18).

(a) 로 `FlipV` 가 정해지고, 그 다음 (b) 로 `FlipU` 가 정해진다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SRC = r'C:/Project/Unreal/Import/Textures/album-cover.png'
TEX_DIR = '/Game/BurgerHouse/Textures'
MAT_DIR = '/Game/BurgerHouse/Materials'
TEX = TEX_DIR + '/T_AlbumCover'
MAT = MAT_DIR + '/M_AlbumCase'
MI = MAT_DIR + '/MI_AlbumCover'
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'
HALF = 50.0          # /Engine/BasicShapes/Cube 의 반치수


def wire(a, a_out, b, b_in):
    """연결이 실패하면 즉시 죽는다 — 핀 이름이 틀리면 조용히 False 만 돌아온다."""
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError('연결 실패: %s[%s] -> %s[%s]'
                           % (a.get_class().get_name(), a_out,
                              b.get_class().get_name(), b_in))


def prop(expr, out, p):
    if not MEL.connect_material_property(expr, out, p):
        raise RuntimeError('출력 연결 실패: %s -> %s' % (expr.get_class().get_name(), p))


# ── 1. 텍스처 임포트 ────────────────────────────────────────────────
L('=== 1. 텍스처 ===')
task = unreal.AssetImportTask()
task.filename = SRC
task.destination_path = TEX_DIR
task.destination_name = 'T_AlbumCover'
task.automated = True
task.replace_existing = True
task.save = True
TOOLS.import_asset_tasks([task])

tex = EAL.load_asset(TEX)
if not tex:
    raise RuntimeError('텍스처 임포트 실패: %s' % SRC)
tex.set_editor_property('srgb', True)
tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
EAL.save_asset(TEX, only_if_is_dirty=False)
L('  %s  (%dx%d)' % (TEX, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))


# ── 2. 마스터 머티리얼 ──────────────────────────────────────────────
L('')
L('=== 2. M_AlbumCase ===')
if EAL.does_asset_exist(MAT):
    EAL.delete_asset(MAT)          # 다시 돌릴 때 노드가 중복되지 않게
m = TOOLS.create_asset('M_AlbumCase', MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())


def node(cls, x, y, **kw):
    n = MEL.create_material_expression(m, cls, x, y)
    for k, v in kw.items():
        n.set_editor_property(k, v)
    return n


lp = node(unreal.MaterialExpressionLocalPosition, -1400, 0)

mask_x = node(unreal.MaterialExpressionComponentMask, -1200, -200, r=True, g=False, b=False, a=False)
mask_y = node(unreal.MaterialExpressionComponentMask, -1200, 0, r=False, g=True, b=False, a=False)
mask_z = node(unreal.MaterialExpressionComponentMask, -1200, 200, r=False, g=False, b=True, a=False)
for mk in (mask_x, mask_y, mask_z):
    # ⚠ ComponentMask · Clamp 의 첫 입력 핀 이름은 'Input' 이 아니라 **빈 문자열**이다.
    #   틀리면 예외 없이 False 만 돌아온다 — ll_album_pin_probe.py 로 확인했다.
    wire(lp, '', mk, '')

# u = (x + 50) / 100
u_add = node(unreal.MaterialExpressionAdd, -1000, -200, const_b=HALF)
wire(mask_x, '', u_add, 'A')
u = node(unreal.MaterialExpressionDivide, -850, -200, const_b=HALF * 2)
wire(u_add, '', u, 'A')

# v = (50 - z) / 100   — 로컬 +Z 가 이미지의 위(v=0)
v_sub = node(unreal.MaterialExpressionSubtract, -1000, 200, const_a=HALF)
wire(mask_z, '', v_sub, 'B')
v = node(unreal.MaterialExpressionDivide, -850, 200, const_b=HALF * 2)
wire(v_sub, '', v, 'A')

# 뒤집기
flip_u_p = node(unreal.MaterialExpressionScalarParameter, -850, -350,
                parameter_name='FlipU', default_value=0.0)
flip_v_p = node(unreal.MaterialExpressionScalarParameter, -850, 350,
                parameter_name='FlipV', default_value=0.0)
u_inv = node(unreal.MaterialExpressionSubtract, -700, -120, const_a=1.0)
wire(u, '', u_inv, 'B')
v_inv = node(unreal.MaterialExpressionSubtract, -700, 280, const_a=1.0)
wire(v, '', v_inv, 'B')
u_sel = node(unreal.MaterialExpressionLinearInterpolate, -550, -200)
wire(u, '', u_sel, 'A'); wire(u_inv, '', u_sel, 'B'); wire(flip_u_p, '', u_sel, 'Alpha')
v_sel = node(unreal.MaterialExpressionLinearInterpolate, -550, 200)
wire(v, '', v_sel, 'A'); wire(v_inv, '', v_sel, 'B'); wire(flip_v_p, '', v_sel, 'Alpha')

uv = node(unreal.MaterialExpressionAppendVector, -400, 0)
wire(u_sel, '', uv, 'A')
wire(v_sel, '', uv, 'B')

cover = node(unreal.MaterialExpressionTextureSampleParameter2D, -250, -250,
             parameter_name='Cover', texture=tex)
wire(uv, '', cover, 'UVs')

# 앞면 마스크: (y*FrontSign - 25) * 1000 을 0..1 로 자른다 = 계단
#
# ⚠ `FrontSign` 을 -1 로 바꾸면 앞면이 반대 면(-Y)으로 간다. 그때는 법선이
#   뒤집히므로 **`FlipU` 도 같이 토글**해야 거울상이 안 된다.
front_sign = node(unreal.MaterialExpressionScalarParameter, -1000, -80,
                  parameter_name='FrontSign', default_value=1.0)
y_signed = node(unreal.MaterialExpressionMultiply, -1000, 0)
wire(mask_y, '', y_signed, 'A')
wire(front_sign, '', y_signed, 'B')
f_sub = node(unreal.MaterialExpressionSubtract, -940, 0, const_b=HALF * 0.5)
wire(y_signed, '', f_sub, 'A')
f_mul = node(unreal.MaterialExpressionMultiply, -850, 0, const_b=1000.0)
wire(f_sub, '', f_mul, 'A')
front = node(unreal.MaterialExpressionClamp, -700, 0, min_default=0.0, max_default=1.0)
wire(f_mul, '', front, '')

# 색 · 거칠기
case_col = node(unreal.MaterialExpressionVectorParameter, -400, 250,
                parameter_name='CaseColor',
                default_value=unreal.LinearColor(0.015, 0.015, 0.018, 1.0))
bc = node(unreal.MaterialExpressionLinearInterpolate, 0, 0)
wire(case_col, '', bc, 'A'); wire(cover, 'RGB', bc, 'B'); wire(front, '', bc, 'Alpha')
prop(bc, '', unreal.MaterialProperty.MP_BASE_COLOR)

case_r = node(unreal.MaterialExpressionScalarParameter, -400, 450,
              parameter_name='CaseRough', default_value=0.32)
cover_r = node(unreal.MaterialExpressionScalarParameter, -400, 550,
               parameter_name='CoverRough', default_value=0.18)
rough = node(unreal.MaterialExpressionLinearInterpolate, 0, 250)
wire(case_r, '', rough, 'A'); wire(cover_r, '', rough, 'B'); wire(front, '', rough, 'Alpha')
prop(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)

spec = node(unreal.MaterialExpressionScalarParameter, -400, 650,
            parameter_name='Spec', default_value=0.65)
prop(spec, '', unreal.MaterialProperty.MP_SPECULAR)

MEL.recompile_material(m)
EAL.save_asset(MAT, only_if_is_dirty=False)
L('  노드 연결 전부 성공 · 컴파일 완료')


# ── 3. 액터를 찾아 방향을 계산한다 ──────────────────────────────────
L('')
L('=== 3. 앞면 방향 계산 ===')
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(LEVEL)

album = None
for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'Album':
        album = a
        break
if not album:
    raise RuntimeError('Album 액터가 없다')

t = album.get_actor_transform()
q, scl = t.rotation, t.scale3d


def sign(x):
    return 1.0 if x >= 0 else -1.0


def rot(vx, vy, vz):
    v = q.rotate_vector(unreal.Vector(vx, vy, vz))
    return (v.x, v.y, v.z)


def mul(v, s):
    return (v[0] * s, v[1] * s, v[2] * s)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


U = mul(rot(1, 0, 0), sign(scl.x))     # 이미지의 오른쪽
V = mul(rot(0, 0, 1), sign(scl.z))     # 이미지의 위
N = rot(0, 1, 0)                       # 앞면 법선

L('  U(이미지 오른쪽) = (%.3f, %.3f, %.3f)' % U)
L('  V(이미지 위)     = (%.3f, %.3f, %.3f)' % V)
L('  N(앞면 법선)     = (%.3f, %.3f, %.3f)' % N)

flip_v = 1.0 if V[2] < 0 else 0.0                       # (a) 이미지 위가 월드 위로
handed = dot(cross(U, V), N)
if flip_v:
    handed = -handed
flip_u = 1.0 if handed > 0 else 0.0                     # (b) 거울상 제거

L('  -> FlipV = %d  (V.z = %.3f)' % (flip_v, V[2]))
L('  -> FlipU = %d  ((U x V)·N = %+.3f, FlipV 반영 후 · 음수여야 정상)'
  % (flip_u, handed))


# ── 4. 머티리얼 인스턴스 ────────────────────────────────────────────
L('')
L('=== 4. MI_AlbumCover ===')
if EAL.does_asset_exist(MI):
    EAL.delete_asset(MI)
mi = TOOLS.create_asset('MI_AlbumCover', MAT_DIR, unreal.MaterialInstanceConstant,
                        unreal.MaterialInstanceConstantFactoryNew())
mi.set_editor_property('parent', m)
MEL.set_material_instance_texture_parameter_value(mi, 'Cover', tex)
MEL.set_material_instance_scalar_parameter_value(mi, 'FlipU', flip_u)
MEL.set_material_instance_scalar_parameter_value(mi, 'FlipV', flip_v)
MEL.set_material_instance_scalar_parameter_value(mi, 'FrontSign', 1.0)
MEL.update_material_instance(mi)
EAL.save_asset(MI, only_if_is_dirty=False)
L('  %s (부모 %s)' % (MI, m.get_name()))


# ── 5. 액터 컴포넌트에 꽂는다 ───────────────────────────────────────
L('')
L('=== 5. 액터에 배정 ===')
comp = album.get_component_by_class(unreal.StaticMeshComponent)
# ⚠ 메시 에셋(/Engine/BasicShapes/Cube)이 아니라 **컴포넌트 오버라이드**에 꽂는다.
#   에셋에 꽂으면 엔진 큐브를 쓰는 모든 액터가 앨범이 된다.
comp.set_material(0, mi)
L('  컴포넌트 슬롯0 = %s' % comp.get_material(0).get_path_name())

les.save_current_level()
L('  레벨 저장')
L('')
L('※ 검산은 새 프로세스에서 ll_album_verify.py 로 한다')
