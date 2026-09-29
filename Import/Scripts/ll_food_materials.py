"""음식 메시에 텍스처가 붙은 머티리얼을 만들어 꽂는다.

## ⚠⚠ 임포터가 만든 것은 **머티리얼이 아니라 인스턴스**다

`material_0` · `french_fries` 는 `MaterialInstanceConstant` 다. 그래서
`MaterialEditingLibrary.create_material_expression` 이 거부한다 —

    TypeError: Cannot nativize 'MaterialInstanceConstant' as 'Material'

인스턴스에 텍스처를 꽂으려면 **부모의 파라미터 이름**을 알아야 하는데, 임포터가
무엇을 부모로 삼았는지에 따라 달라진다. 그 의존을 피하려고 **자체 머티리얼**을
만들어 메시 슬롯에 직접 꽂는다.

⚠ 임포터가 만든 빈 인스턴스는 남겨 둔다 — 지우면 메시가 참조를 잃는다.
  새 머티리얼을 슬롯에 덮어쓰는 것으로 충분하다.
"""
import unreal
L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
TEX = '/Game/BurgerHouse/Props/Food/Textures'
MAT_DIR = '/Game/BurgerHouse/Materials'


def build(name, bc, nrm, mesh_path):
    path = '%s/%s' % (MAT_DIR, name)
    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
        L('  %s 이미 있다' % name)
    else:
        m = TOOLS.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        for tex_name, prop, y in ((bc, unreal.MaterialProperty.MP_BASE_COLOR, 0),
                                  (nrm, unreal.MaterialProperty.MP_NORMAL, 300)):
            t = EAL.load_asset('%s/%s' % (TEX, tex_name))
            if not t:
                E('    %s 텍스처가 없다' % tex_name); continue
            n = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSample, -400, y)
            n.set_editor_property('texture', t)
            # ⚠ `RGB` 핀을 명시한다. 빈 문자열이면 기본 출력이라 노멀에서 틀린다.
            MEL.connect_material_property(n, 'RGB', prop)
        MEL.recompile_material(m)
        EAL.save_asset(path, only_if_is_dirty=False)
        L('  %s 생성' % name)

    mesh = EAL.load_asset(mesh_path)
    if mesh:
        mesh.set_material(0, m)
        EAL.save_asset(mesh_path, only_if_is_dirty=False)
        L('    -> %s 슬롯0 에 꽂았다' % mesh_path.split('/')[-1])
    return m

L('=== 음식 머티리얼 ===')
build('M_BurgerBox', 'T_BurgerBox_BC', 'T_BurgerBox_N',
      '/Game/BurgerHouse/Props/Food/SM_BurgerBox')
build('M_Fries', 'T_Fries_BC', 'T_Fries_N',
      '/Game/BurgerHouse/Props/Food/SM_Fries')

L('')
L('=== 되읽기 ===')
for p in ('/Game/BurgerHouse/Props/Food/SM_BurgerBox','/Game/BurgerHouse/Props/Food/SM_Fries'):
    m = EAL.load_asset(p)
    L('  %-16s 슬롯0 = %s' % (p.split('/')[-1], m.get_material(0).get_name()))
