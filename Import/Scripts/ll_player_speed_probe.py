"""플레이어 캐릭터의 이동속도와 레벨별 게임모드를 훑는다. 안 고친다.

보는 것 —
  ① 게임모드 -> DefaultPawnClass -> 그 폰의 WalkSpeed / SprintSpeed / MaxWalkSpeed
  ② 레벨마다 World Settings 의 게임모드 오버라이드
  ③ 스프린트 입력 배선 (IA_Sprint · IMC)
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

GM_DEFAULT = '/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode'


def cdo_of(path):
    a = EAL.load_asset(path)
    if not a:
        return None
    try:
        return unreal.get_default_object(a.generated_class())
    except Exception:
        return None


def dump_pawn(path, label):
    c = cdo_of(path)
    if not c:
        E('  %-22s %s 를 못 읽는다' % (label, path))
        return
    L('  %s  %s' % (label, path.replace('/Game/', '')))
    for p in ('walk_speed', 'sprint_speed'):
        try:
            L('      %-16s %s' % (p, c.get_editor_property(p)))
        except Exception:
            L('      %-16s (없음 — AFebriculaCharacter 파생이 아니다)' % p)
    try:
        mv = c.get_editor_property('character_movement')
        for p in ('max_walk_speed', 'max_acceleration', 'braking_deceleration_walking',
                  'min_analog_walk_speed'):
            L('      %-16s %s' % (p, mv.get_editor_property(p)))
    except Exception as ex:
        E('      CharacterMovement 를 못 읽음: %s' % str(ex)[:60])


L('=== ① 기본 게임모드 ===')
gm = cdo_of(GM_DEFAULT)
pawn_path = None
if gm:
    for p in ('default_pawn_class', 'player_controller_class'):
        try:
            v = gm.get_editor_property(p)
            L('  %-24s %s' % (p, v))
            if p == 'default_pawn_class' and v:
                pawn_path = v.get_path_name().split('.')[0]
        except Exception as ex:
            E('  %-24s %s' % (p, str(ex)[:60]))

L('')
L('=== 플레이어 폰 ===')
if pawn_path:
    dump_pawn(pawn_path, 'DefaultPawn')

# C++ 기본값도 같이 (BP 가 덮었는지 보려고)
L('')
L('  -- C++ AFebriculaCharacter 기본값 --')
try:
    cpp = unreal.get_default_object(unreal.FebriculaCharacter.static_class())
    L('      walk_speed       %s' % cpp.get_editor_property('walk_speed'))
    L('      sprint_speed     %s' % cpp.get_editor_property('sprint_speed'))
    L('      max_walk_speed   %s'
      % cpp.get_editor_property('character_movement').get_editor_property('max_walk_speed'))
except Exception as ex:
    E('      %s' % str(ex)[:90])

# ---- ② 레벨별 게임모드 오버라이드 ----
L('')
L('=== ② 레벨별 World Settings 게임모드 ===')
maps = [str(d.package_name) for d in REG.get_assets_by_path('/Game', recursive=True)
        if str(d.asset_class_path.asset_name) == 'World'
        and not str(d.package_name).startswith('/Game/Plugins')]
for m in sorted(maps):
    if 'UltraDynamicSky' in m or 'Variant_' in m:
        continue
    try:
        unreal.EditorLoadingAndSavingUtils.load_map(m)
        w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        ws = w.get_world_settings()
        over = ws.get_editor_property('default_game_mode')
        L('  %-46s %s' % (m.replace('/Game/', ''), over if over else '(오버라이드 없음)'))
    except Exception as ex:
        E('  %-46s 못 읽음: %s' % (m.replace('/Game/', ''), str(ex)[:50]))

L('')
L('=== ③ 스프린트 입력 ===')
for d in REG.get_assets_by_path('/Game/Input', recursive=True):
    L('  %-14s %s' % (str(d.asset_class_path.asset_name), str(d.package_name).replace('/Game/', '')))

L('')
L('=== 끝 — 아무것도 안 고쳤다 ===')
