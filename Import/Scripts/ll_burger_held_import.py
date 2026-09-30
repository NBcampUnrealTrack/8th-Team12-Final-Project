"""`Eating_a_Burger` 모션에 쓸 버거 메시를 들여온다.

    에디터를 **끄고** 돌린다.

원본: `Downloads/burger/A_detailed_3D_model_o_1207173805_refine.fbx` (+ 같은 이름 `.png`)

## ⚠ 텍스처가 **따로** 온다

FBX 옆에 `.png` 가 같이 있지만 머티리얼 연결이 보장되지 않는다 — §9-8 에서 음식
텍스처가 통째로 안 따라온 적이 있다 (폴더가 갈려 있었다). 여기서는 텍스처를
**따로 들여와** 직접 머티리얼을 만들어 물린다.

⚠⚠ 임포터가 만드는 것은 `MaterialInstanceConstant` 라
  `MaterialEditingLibrary.create_material_expression` 이 **거부한다.** 그래서
  `Material` 을 새로 만든다 (§9-8 과 같은 이유).

## ⚠ 이름

`Props/Burger` 가 이미 있다 (셸에 쓰이는 진열용). 손에 드는 것과 구별하려고
**`Food_BurgerHeld`** 로 들여온다.

## ⚠ 콜리전은 안 만든다

손에 드는 장식이라 콜리전을 켜면 손님 캡슐을 밀거나 내비를 막는다 (§9-6).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary

SRC = 'C:/Users/jesse/Downloads/burger/A_detailed_3D_model_o_1207173805_refine'
DEST = '/Game/BurgerHouse/Props/Food'
TEXDIR = '/Game/BurgerHouse/Props/Food/Textures'
NAME = 'Food_BurgerHeld'
TEX = 'T_BurgerHeld_BC'
MAT = '/Game/BurgerHouse/Materials/M_BurgerHeld'

fails = []


def no_combine():
    En = unreal.InterchangeCombineStaticMeshesBehavior
    for n in ('DO_NOT_COMBINE', 'DONT_COMBINE', 'NONE'):
        if hasattr(En, n):
            return getattr(En, n)
    return None


# ───────────────────────── 메시
L('B === 메시 ===')
p = unreal.InterchangeGenericAssetsPipeline()
mp = p.get_editor_property('mesh_pipeline')
mp.set_editor_property('import_static_meshes', True)
c = no_combine()
if c is not None:
    mp.set_editor_property('combine_static_meshes_behavior', c)
# ⚠ 콜리전은 안 만든다 (모듈 주석)
mp.set_editor_property('collision', False)
mp.set_editor_property('force_collision_primitive_generation', False)

t = unreal.AssetImportTask()
t.set_editor_property('filename', SRC + '.fbx')
t.set_editor_property('destination_path', DEST)
t.set_editor_property('automated', True)
t.set_editor_property('replace_existing', True)
t.set_editor_property('save', True)
t.set_editor_property('options', p)
TOOLS.import_asset_tasks([t])

# ⚠ 이름이 원본 그대로 들어온다. 규약에 맞춰 바꾼다.
made = [a for a in EAL.list_assets(DEST, recursive=False, include_folder=False)
        if 'A_detailed' in a]
L('B   원본 이름으로 들어온 것 %s' % [m.split('/')[-1] for m in made])
mesh_path = '%s/%s' % (DEST, NAME)
for a in made:
    o = EAL.load_asset(a)
    if not isinstance(o, unreal.StaticMesh):
        continue
    if EAL.does_asset_exist(mesh_path):
        EAL.delete_asset(mesh_path)
    L('B   rename -> %s  %s' % (NAME, EAL.rename_asset(a.split('.')[0], mesh_path)))
    break

if not EAL.does_asset_exist(mesh_path):
    E('B   메시가 안 들어왔다')
    raise SystemExit

# ───────────────────────── 텍스처
L('B')
L('B === 텍스처 ===')
tt = unreal.AssetImportTask()
tt.set_editor_property('filename', SRC + '.png')
tt.set_editor_property('destination_path', TEXDIR)
tt.set_editor_property('destination_name', TEX)
tt.set_editor_property('automated', True)
tt.set_editor_property('replace_existing', True)
tt.set_editor_property('save', True)
TOOLS.import_asset_tasks([tt])
tex_path = '%s/%s' % (TEXDIR, TEX)
L('B   %s -> %s' % (TEX, EAL.does_asset_exist(tex_path)))
if not EAL.does_asset_exist(tex_path):
    fails.append('텍스처')

# ───────────────────────── 머티리얼
L('B')
L('B === 머티리얼 ===')
if EAL.does_asset_exist(MAT):
    mat = EAL.load_asset(MAT)
    L('B   이미 있다')
else:
    mat = TOOLS.create_asset('M_BurgerHeld', '/Game/BurgerHouse/Materials',
                             unreal.Material, unreal.MaterialFactoryNew())
    tex = EAL.load_asset(tex_path)
    if tex:
        ts = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 0)
        ts.set_editor_property('texture', tex)
        MEL.connect_material_property(ts, '', unreal.MaterialProperty.MP_BASE_COLOR)
    r = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    r.set_editor_property('r', 0.7)
    MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_asset(MAT, only_if_is_dirty=False)
    L('B   생성 — 베이스컬러에 텍스처 · 러프 0.7')

m = EAL.load_asset(mesh_path)
for i in range(len(m.get_editor_property('static_materials'))):
    m.set_material(i, mat)
EAL.save_asset(mesh_path, only_if_is_dirty=False)

# ───────────────────────── 실측
L('B')
L('B === 실측 ===')
b = m.get_bounds()
L('B   %-18s 크기 %.1f x %.1f x %.1f cm · 피벗에서 중심까지 (%.1f %.1f %.1f) · 면 %d · 슬롯 %d'
  % (NAME, b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2,
     b.origin.x, b.origin.y, b.origin.z, m.get_num_triangles(0),
     len(m.get_editor_property('static_materials'))))
# ⚠ 손에 드는 버거는 실물이 10cm 안팎이다. 크게 벗어나면 스케일을 맞춰야 한다.
big = max(b.box_extent.x, b.box_extent.y, b.box_extent.z) * 2
L('B   가장 긴 변 %.1fcm -> 실물 버거(약 10cm) 기준 스케일 %.3f' % (big, 10.0 / big if big else 0))

L('B')
if fails:
    E('B === 실패: %s ===' % fails)
else:
    L('B === 완료 ===')
