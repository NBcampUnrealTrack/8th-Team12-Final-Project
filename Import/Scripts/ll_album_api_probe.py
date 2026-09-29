"""머티리얼 노드 API 가 이 엔진 버전에 있는지 확인한다.

큐브의 내장 UV 를 믿지 않고 **로컬 위치**로 UV 를 만들 작정이다. 그래야
- 어느 면이 앞면인지를 법선이 아니라 위치로 고를 수 있고 (음수 스케일로
  법선이 뒤집혀도 안전하다),
- 엔진 큐브의 UV 레이아웃을 몰라도 된다.

그러려면 `MaterialExpressionLocalPosition` 이 파이썬에 노출돼 있어야 한다.
"""
import unreal

L = unreal.log_warning

CANDIDATES = [
    'MaterialExpressionLocalPosition',
    'MaterialExpressionObjectLocalBounds',
    'MaterialExpressionWorldPosition',
    'MaterialExpressionTextureCoordinate',
    'MaterialExpressionComponentMask',
    'MaterialExpressionTextureSampleParameter2D',
    'MaterialExpressionScalarParameter',
    'MaterialExpressionVectorParameter',
    'MaterialExpressionLinearInterpolate',
    'MaterialExpressionIf',
    'MaterialExpressionAdd',
    'MaterialExpressionMultiply',
    'MaterialExpressionDivide',
    'MaterialExpressionConstant',
    'MaterialExpressionConstant2Vector',
    'MaterialExpressionAppendVector',
    'MaterialExpressionOneMinus',
    'MaterialInstanceConstant',
    'MaterialInstanceConstantFactoryNew',
]
for c in CANDIDATES:
    L('  %-45s %s' % (c, 'OK' if hasattr(unreal, c) else '*** 없음 ***'))

L('')
L('=== MaterialEditingLibrary 함수 ===')
for f in ('create_material_expression', 'connect_material_property',
          'connect_material_expressions', 'recompile_material',
          'set_material_instance_texture_parameter_value',
          'set_material_instance_scalar_parameter_value',
          'set_material_instance_vector_parameter_value',
          'update_material_instance'):
    L('  %-50s %s' % (f, 'OK' if hasattr(unreal.MaterialEditingLibrary, f) else '*** 없음 ***'))

L('')
L('=== LocalPosition 노드 속성 ===')
if hasattr(unreal, 'MaterialExpressionLocalPosition'):
    for p in ('include_shadow_reflection_offset', 'include_world_position_offset'):
        L('  %s' % p)
