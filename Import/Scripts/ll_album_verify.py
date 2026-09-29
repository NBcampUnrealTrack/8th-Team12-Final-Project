"""ll_album_cover.py 의 결과를 **새 프로세스**에서 되읽는다.

⚠ 같은 실행 안의 되읽기는 무의미하다 (BURGERHOUSE.md §저장). 파일이 실제로
써졌는지는 새로 띄운 프로세스에서만 확인된다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

TEX = '/Game/BurgerHouse/Textures/T_AlbumCover'
MAT = '/Game/BurgerHouse/Materials/M_AlbumCase'
MI = '/Game/BurgerHouse/Materials/MI_AlbumCover'
LEVEL = '/Game/BurgerHouse/Maps/L_BurgerHouse'

fail = []

L('=== 에셋 ===')
for p in (TEX, MAT, MI):
    ok = EAL.does_asset_exist(p)
    L('  %-50s %s' % (p, 'OK' if ok else '*** 없음 ***'))
    if not ok:
        fail.append(p)

tex = EAL.load_asset(TEX)
if tex:
    L('  텍스처 %dx%d  srgb=%s  압축=%s'
      % (tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
         tex.get_editor_property('srgb'),
         tex.get_editor_property('compression_settings')))

mi = EAL.load_asset(MI)
if mi:
    L('')
    L('=== MI_AlbumCover 파라미터 ===')
    par = mi.get_editor_property('parent')
    L('  parent = %s' % (par.get_path_name() if par else 'None'))
    if not par or par.get_name() != 'M_AlbumCase':
        fail.append('MI 부모')
    t = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mi, 'Cover')
    L('  Cover  = %s' % (t.get_path_name() if t else 'None'))
    if not t:
        fail.append('Cover 파라미터')
    for n in ('FlipU', 'FlipV', 'FrontSign'):
        v = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(mi, n)
        L('  %-6s = %.0f' % (n, v))

L('')
L('=== 레벨의 Album 액터 ===')
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level(LEVEL)

album = None
for a in eas.get_all_level_actors():
    if a.get_actor_label() == 'Album':
        album = a
        break

if not album:
    E('  *** Album 액터가 없다 ***')
    fail.append('Album 액터')
else:
    comp = album.get_component_by_class(unreal.StaticMeshComponent)
    m = comp.get_material(0)
    L('  컴포넌트 슬롯0 = %s' % (m.get_path_name() if m else 'None'))
    if not m or m.get_name() != 'MI_AlbumCover':
        fail.append('액터 머티리얼 배정')
    # ⚠ 엔진 큐브 에셋 자체는 건드리지 않았어야 한다
    sm = comp.static_mesh
    slot0 = sm.static_materials[0].material_interface
    L('  메시 에셋 슬롯0 = %s  (엔진 큐브는 그대로여야 한다)'
      % (slot0.get_path_name() if slot0 else 'None'))
    if slot0 and 'AlbumCover' in slot0.get_name():
        E('  *** 엔진 큐브 에셋을 오염시켰다 ***')
        fail.append('엔진 큐브 오염')

    scl = album.get_actor_transform().scale3d
    L('  크기 = %.1f x %.1f x %.1f cm (앞면 %.1f x %.1f, 비율 %.3f)'
      % (abs(scl.x) * 100, abs(scl.y) * 100, abs(scl.z) * 100,
         abs(scl.x) * 100, abs(scl.z) * 100, abs(scl.x) / abs(scl.z)))

L('')
if fail:
    E('=== 실패: %s ===' % ', '.join(fail))
else:
    L('=== 전부 통과 ===')
