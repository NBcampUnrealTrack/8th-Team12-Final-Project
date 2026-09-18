"""카운터 트레이를 만들어 픽업대 위에 놓는다.

    에디터를 **끄고** 돌린다.

⚠ 메시·오프셋은 손님 트레이(§9-6)와 **같은 값**을 쓴다. 한쪽만 고치면
  카운터 위의 것과 손에 든 것이 달라 보인다.

⚠ 이것은 **장식**이다 — 예약하지 않는다. 준비된 주문이 있으면 보이고 없으면
  숨는다 (`ACounterTray::Refresh`).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

BP_PATH = '/Game/BurgerHouse/BP'
BP_NAME = 'BP_CounterTray'
FULL = '%s/%s' % (BP_PATH, BP_NAME)
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'

# 손님 트레이와 **같은 구성** (§9-6)
ITEMS = [
    ('tray', '/Game/BurgerHouse/Props/Tray', (0, 0, 0), (1.0, 1.0, 1.0)),
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox', (-6.0, 0.0, 2.0), (5.0, 5.0, 5.0)),
    ('fries', '/Game/BurgerHouse/Props/Food/SM_Fries', (7.0, -5.0, 2.0), (0.4, 0.4, 0.4)),
    ('receipt', '/Engine/BasicShapes/Plane', (7.0, 7.0, 1.2), (0.08, 0.15, 1.0)),
]

L('=== BP_CounterTray ===')
if EAL.does_asset_exist(FULL):
    L('  이미 있다')
    bp = EAL.load_asset(FULL)
else:
    fac = unreal.BlueprintFactory()
    fac.set_editor_property('parent_class', unreal.CounterTray)
    bp = TOOLS.create_asset(BP_NAME, BP_PATH, unreal.Blueprint, fac)
    L('  생성 -> %s' % (bp.get_name() if bp else 'None'))

if not bp:
    E('  못 만들었다'); raise SystemExit

cdo = unreal.get_default_object(bp.generated_class())
for prop, path, loc, scl in ITEMS:
    c = cdo.get_editor_property(prop)
    m = EAL.load_asset(path)
    if not c or not m:
        E('  %s 실패 (comp=%s mesh=%s)' % (prop, bool(c), bool(m))); continue
    c.set_editor_property('static_mesh', m)
    if prop != 'tray':
        c.set_editor_property('relative_location', unreal.Vector(*loc))
    c.set_editor_property('relative_scale3d', unreal.Vector(*scl))
    L('  %-12s <- %s' % (prop, m.get_name()))

ok = unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
L('  저장 -> %s' % ok)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ---- 레벨에 배치 ----
L('')
L('=== 픽업대 위에 배치 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cls = unreal.load_object(None, '%s.%s_C' % (FULL, BP_NAME))
placed = unreal.GameplayStatics.get_all_actors_of_class(world, cls) if cls else []
if placed:
    L('  이미 %d개 놓여 있다' % len(placed))
else:
    pickups = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
               if str(s.get_editor_property('kind')) == 'Pickup']
    for i, p in enumerate(pickups):
        loc = p.get_actor_location()
        # ⚠ 카운터 상판 높이는 **추정**이다 (z+95). 에디터에서 맞출 것.
        where = unreal.Vector(loc.x, loc.y + 60.0, loc.z + 95.0)
        a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, where, unreal.Rotator(0, 0, 0))
        a.set_actor_label('CounterTray_%d' % i)
        L('  %s @ %s (픽업 %s 기준)' % (a.get_actor_label(), where, p.get_name()))
    ok = unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False)
    L('  레벨 저장 -> %s' % ok)

L('')
L('⚠ 상판 높이 z+95 와 앞으로 60 은 추정이다 — 에디터에서 눈으로 맞출 것.')
