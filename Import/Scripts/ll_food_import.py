"""트레이에 올릴 음식 에셋과 알림음을 임포트한다.

    에디터를 **끄고** 돌린다.

## 들여오는 것

```
~/Downloads/_extract/burger/box/box.obj          -> Props/Food/SM_BurgerBox
~/Downloads/_extract/fries/french frice.fbx      -> Props/Food/SM_Fries
~/Downloads/Chime_1.mp3 · Chime_2.mp3            -> Audio/SW_Chime_1 · SW_Chime_2
```

⚠ 원본은 압축이었다 (`box.7z` · `french frice.zip`). `tar.exe`(Windows 내장)와
  `unzip` 으로 `~/Downloads/_extract/` 에 풀어 두었다.

## ⚠ 이름을 바꿔 들여온다

원본 파일명이 `box` · `french frice` 다 — 공백이 있고 철자도 틀렸다(`frice`).
에셋 이름은 `SM_BurgerBox` · `SM_Fries` 로 고정한다.

## ⚠ 스케일은 **임포트 뒤에 재서** 정한다

Sketchfab 류 모델은 단위가 제각각이다. 여기서는 그대로 들여오고, 실제 크기를
재서 트레이에 올릴 때 맞춘다 (`ll_food_verify.py`).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

DL = 'C:/Users/jesse/Downloads'
MESH_DIR = '/Game/BurgerHouse/Props/Food'
AUDIO_DIR = '/Game/BurgerHouse/Audio'


def make_mesh_task(src, dest_dir, name):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', src)
    t.set_editor_property('destination_path', dest_dir)
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)       # 대화상자 없이
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)

    opt = unreal.FbxImportUI()
    opt.set_editor_property('import_mesh', True)
    opt.set_editor_property('import_textures', True)
    opt.set_editor_property('import_materials', True)
    opt.set_editor_property('import_as_skeletal', False)
    opt.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)

    sm = opt.static_mesh_import_data
    # ⚠ **콜리전을 만들지 않는다.** 트레이 위에 얹을 장식이라 콜리전이 있으면
    #   손님 캡슐·트레이와 부딪히고, 최악의 경우 손에 든 음식이 손님을 밀어낸다.
    sm.set_editor_property('auto_generate_collision', False)
    sm.set_editor_property('combine_meshes', True)
    t.set_editor_property('options', opt)
    return t


def make_sound_task(src, dest_dir, name):
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', src)
    t.set_editor_property('destination_path', dest_dir)
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)
    return t


tasks = [
    make_mesh_task('%s/_extract/burger/box/box.obj' % DL, MESH_DIR, 'SM_BurgerBox'),
    make_mesh_task('%s/_extract/fries/french frice.fbx' % DL, MESH_DIR, 'SM_Fries'),
    make_sound_task('%s/Chime_1.mp3' % DL, AUDIO_DIR, 'SW_Chime_1'),
    make_sound_task('%s/Chime_2.mp3' % DL, AUDIO_DIR, 'SW_Chime_2'),
]

L('=== 임포트 ===')
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for t in tasks:
    made = list(t.get_editor_property('imported_object_paths') or [])
    src = str(t.get_editor_property('filename')).split('/')[-1]
    if made:
        L('  %-24s -> %s' % (src, made))
    else:
        E('  %-24s -> **실패** (경로·형식을 볼 것)' % src)

# ---- 되읽기 ----
L('')
L('=== 되읽기 ===')
for p, cls in ((MESH_DIR + '/SM_BurgerBox', 'StaticMesh'),
               (MESH_DIR + '/SM_Fries', 'StaticMesh'),
               (AUDIO_DIR + '/SW_Chime_1', 'SoundWave'),
               (AUDIO_DIR + '/SW_Chime_2', 'SoundWave')):
    if not EAL.does_asset_exist(p):
        E('  %-40s 없다' % p.replace('/Game/', ''))
        continue
    a = EAL.load_asset(p)
    line = '  %-40s %s' % (p.replace('/Game/', ''), a.get_class().get_name())
    if isinstance(a, unreal.StaticMesh):
        b = a.get_bounds().box_extent
        # ⚠ BoxExtent 는 **반지름**이다. 실제 크기는 2배.
        line += '  크기 %.1f × %.1f × %.1f cm · 머티리얼 %d개' % (
            b.x * 2, b.y * 2, b.z * 2, a.get_num_sections(0))
    elif isinstance(a, unreal.SoundWave):
        line += '  %.2f초' % a.get_editor_property('duration')
    L(line)

L('')
L('⚠ 크기가 터무니없으면 원본 단위가 다른 것이다 — 트레이에 얹을 때 스케일로 맞춘다.')
