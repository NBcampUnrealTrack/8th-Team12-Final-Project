"""감자튀김 세트 4개를 **셸에서 떼어내** 개별 스태틱메시로 들여온다.

    에디터를 **끄고** 돌린다.

## ⚠⚠ 이 넷은 원래 **셸에 합쳐져** 들어온다

`ll_import.py` 는 `LL_Shell.fbx` 를 `combine_static_meshes_behavior =
ALL_MESHES_INTO_ONE` 으로 굽는다. 즉 `Food_Coke`·`Food_Fries`·`Food_FriesLoose`·
`Food_Ketchup` 은 레벨 지오메트리의 **일부**이지 따로 쓸 수 있는 에셋이 아니다.
`LL_placement.json` 의 `singles` 항목은 메타데이터일 뿐 아무 에셋도 안 만든다.

트레이에 얹으려면 **개별 메시**가 필요하다. 그래서 같은 FBX 를 **합치지 않고**
한 번 더 굽고, 그중 넷만 남기고 나머지는 지운다.

⚠ 셸 쪽(`/Game/BurgerHouse/Shell/LL_Shell`)은 **안 건드린다.** 레벨이 그것을
  쓰고 있으므로 둘은 따로 존재한다 — 테이블 위에도 있고 트레이 위에도 있게 된다.

## ⚠⚠ 피벗이 **월드 원점**에 있다

블렌더에서 배치를 **버텍스에 구워** 내보냈기 때문에(`FRIES_ASSET.md` §4) 각
오브젝트의 트랜스폼은 항등이고 정점만 월드 좌표에 있다. 따로 구우면 —

```
피벗   (0, 0, 0)
지오메트리  Table_C1 위 (x 약 290, y 약 −270, z 약 70) 근처
```

그래서 컴포넌트에 그냥 꽂으면 **트레이에서 3m 떨어진 곳**에 나타난다.

⚠ 배치 계산에서 `bounds.origin` 을 빼 주면 수학적으로는 맞는다 (아래 `place`).
  다만 에디터에서 상대 위치가 −290 같은 값으로 보여 **사람이 손으로 만지기 나쁘다.**
  제대로 하려면 블렌더에서 **피벗을 각자 중심으로 옮겨 다시 내보내는** 것이 맞다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

FBX = 'C:/Project/Unreal/Import/FBX/LL_Shell.fbx'
TMP = '/Game/BurgerHouse/Props/_FriesSplit'
DEST = '/Game/BurgerHouse/Props/Food'
KEEP = ('Food_Fries', 'Food_Coke', 'Food_FriesLoose', 'Food_Ketchup')


def no_combine():
    """합치지 않는 열거값 — 이름이 버전마다 달라 방어적으로 찾는다."""
    En = unreal.InterchangeCombineStaticMeshesBehavior
    for n in ('DO_NOT_COMBINE', 'DONT_COMBINE', 'NONE'):
        if hasattr(En, n):
            return getattr(En, n)
    for n in dir(En):
        if n.isupper() and 'DO_NOT' in n:
            return getattr(En, n)
    return None


p = unreal.InterchangeGenericAssetsPipeline()
mp = p.get_editor_property('mesh_pipeline')
mp.set_editor_property('import_static_meshes', True)
c = no_combine()
if c is None:
    E('F  DO_NOT_COMBINE 열거값을 못 찾았다')
    raise SystemExit
mp.set_editor_property('combine_static_meshes_behavior', c)
# ⚠ 콜리전은 안 만든다. 트레이 위 장식이라 켜면 손님 캡슐을 민다 (§9-6).
mp.set_editor_property('collision', False)
mp.set_editor_property('force_collision_primitive_generation', False)
# ⚠ 머티리얼·텍스처는 이미 들어와 있다. 다시 구우면 기존 것을 덮어쓴다.
try:
    p.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    p.get_editor_property('material_pipeline').get_editor_property('texture_pipeline') \
        .set_editor_property('import_textures', False)
except Exception as ex:
    L('F  머티리얼/텍스처 끄기 건너뜀: %s' % str(ex)[:70])

L('F === 합치지 않고 임시 폴더에 굽기 ===')
t = unreal.AssetImportTask()
t.set_editor_property('filename', FBX)
t.set_editor_property('destination_path', TMP)
t.set_editor_property('automated', True)
t.set_editor_property('replace_existing', True)
t.set_editor_property('save', True)
t.set_editor_property('options', p)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])

made = EAL.list_assets(TMP, recursive=True, include_folder=False)
L('F   %d개 생겼다' % len(made))

L('F')
L('F === 넷만 남기고 옮기기 ===')
moved = []
for name in KEEP:
    src = '%s/%s' % (TMP, name)
    dst = '%s/%s' % (DEST, name)
    if not EAL.does_asset_exist(src):
        E('F   %s 가 안 나왔다 — FBX 안의 이름을 확인할 것' % name)
        continue
    if EAL.does_asset_exist(dst):
        EAL.delete_asset(dst)
    L('F   %-18s -> %s  %s' % (name, dst, EAL.rename_asset(src, dst)))
    moved.append(dst)

L('F')
L('F === 임시 폴더 치우기 ===')
n = 0
for a in EAL.list_assets(TMP, recursive=True, include_folder=False):
    EAL.delete_asset(a.split('.')[0])
    n += 1
EAL.delete_directory(TMP)
L('F   %d개 삭제' % n)

L('F')
L('F === 실측 ===')
mat = EAL.load_asset('/Game/BurgerHouse/Materials/M_Fries')
for pth in moved:
    m = EAL.load_asset(pth)
    if not m:
        continue
    b = m.get_bounds()
    # ⚠ 머티리얼 슬롯이 비어 있으면 회색으로 나온다. `M_Fries` 를 물린다.
    if mat:
        for i in range(len(m.get_editor_property('static_materials'))):
            m.set_material(i, mat)
    EAL.save_asset(pth, only_if_is_dirty=False)
    L('F   %-18s 크기 %6.1f x %6.1f x %6.1f  · 피벗에서 중심까지 (%7.1f %7.1f %6.1f) · 면 %d'
      % (m.get_name(), b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2,
         b.origin.x, b.origin.y, b.origin.z, m.get_num_triangles(0)))

L('F')
L('F ⚠ 피벗이 월드 원점이라 중심까지의 거리가 크다 — 배치 계산에서 빼 줘야 한다.')
