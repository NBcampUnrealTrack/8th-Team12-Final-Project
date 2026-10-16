"""머티리얼 노드의 **입력 핀 이름**을 실제로 시도해서 알아낸다.

`connect_material_expressions` 는 핀 이름이 틀리면 예외 없이 False 만 돌려준다.
버전마다 이름이 달라서(빈 문자열 / 'Input' / 'A' …) 추측하면 조용히 깨진다.
임시 머티리얼에 노드를 만들고 후보 이름을 전부 찔러본다.
"""
import unreal

L = unreal.log_warning
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

TMP = '/Game/BurgerHouse/Materials/M_PinProbe_TMP'
if EAL.does_asset_exist(TMP):
    EAL.delete_asset(TMP)
m = TOOLS.create_asset('M_PinProbe_TMP', '/Game/BurgerHouse/Materials',
                       unreal.Material, unreal.MaterialFactoryNew())

src = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 0)
src.set_editor_property('r', 1.0)
src3 = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -800, 200)

CANDIDATES = ['', 'Input', 'A', 'B', 'Alpha', 'UVs', 'Min', 'Max', 'Coordinates', 'Value']

TARGETS = [
    (unreal.MaterialExpressionComponentMask, 'ComponentMask'),
    (unreal.MaterialExpressionAdd, 'Add'),
    (unreal.MaterialExpressionSubtract, 'Subtract'),
    (unreal.MaterialExpressionMultiply, 'Multiply'),
    (unreal.MaterialExpressionDivide, 'Divide'),
    (unreal.MaterialExpressionClamp, 'Clamp'),
    (unreal.MaterialExpressionLinearInterpolate, 'LinearInterpolate'),
    (unreal.MaterialExpressionAppendVector, 'AppendVector'),
    (unreal.MaterialExpressionTextureSampleParameter2D, 'TextureSampleParameter2D'),
]

y = -600
for cls, label in TARGETS:
    ok = []
    for name in CANDIDATES:
        n = MEL.create_material_expression(m, cls, -400, y)
        y += 60
        # 스칼라 소스로 한 번, 벡터 소스로 한 번
        if MEL.connect_material_expressions(src, '', n, name):
            ok.append(name if name else "''(빈문자열)")
        elif MEL.connect_material_expressions(src3, '', n, name):
            ok.append((name if name else "''(빈문자열)") + '(vec3)')
    L('%-28s %s' % (label, ', '.join(ok) if ok else '*** 없음 ***'))

L('')
L('=== LocalPosition 출력 이름 ===')
lp = MEL.create_material_expression(m, unreal.MaterialExpressionLocalPosition, -600, 400)
mk = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -400, 400)
for out in ('', 'RGB', 'XYZ', 'XYZW'):
    for inp in CANDIDATES:
        if MEL.connect_material_expressions(lp, out, mk, inp):
            L("  출력 '%s' -> 입력 '%s'  OK" % (out, inp))

EAL.delete_asset(TMP)
L('')
L('임시 머티리얼 삭제')
