"""여섯 건 중 스크립트로 되는 다섯 건.

    에디터를 **끄고** 돌린다.

| | |
|---|---|
| ① | `OrderChime` 볼륨 0.2 → **0.05** |
| ② | 카운터 트레이 **세로 2개 → 가로 1개**, 픽업 슬롯 중점으로 |
| ③④ | 음식 텍스처 임포트 + 머티리얼 연결, 영수증 흰 머티리얼 |
| ⑤ | `S_Move`·`P_Move`·`W_Move` 의 도착 판정을 `K_Move` 와 같게 |

⑥(테이블 중복)은 C++ 에서 고쳤다 — `UStoreSubsystem::ResolvePartyTable`.

## ⚠⚠ ③④ 텍스처가 왜 없었나

`ll_food_import.py` 가 `import_textures=True` 로 들여왔는데 **텍스처가 하나도 안
따라왔다.** Food 폴더에 메시 2개와 **빈 머티리얼 2개**뿐이었다.

원인은 원본 구조다 — `.obj`/`.fbx` 와 텍스처가 **다른 폴더**에 있었고
(`source/` vs `textures/`), 임포터는 파일 옆만 본다. 그래서 텍스처를 **따로**
들여와 손으로 연결한다.

⚠ 머티리얼은 생기고 메시에 꽂히기까지 한다 — 그래서 「임포트 성공」으로 보인다.
  회색으로 나오는 것을 눈으로 봐야만 안다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/AI/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
DL = 'C:/Users/jesse/Downloads'
TEX_DIR = '/Game/BurgerHouse/Props/Food/Textures'

# ───────────────────────── ① 볼륨
L('=== ① OrderChime 볼륨 ===')
L('  K_Order ▸ VolumeMultiplier = 0.05 -> %s'
  % S.set_task_property_value(ST, 'Root/Kiosk/K_Order',
                              'BurgerHousePlaySoundTask', 'VolumeMultiplier', '0.05'))

# ───────────────────────── ⑤ 도착 판정
L('')
L('=== ⑤ 도착 판정 (자리로 순간이동하던 것) ===')
# ⚠ `SitAt` 은 좌석 트랜스폼으로 **스냅**한다 (TeleportPhysics). 걸어서 가까이
#   오지 않으면 그 스냅이 「순간이동」으로 보인다.
#   `bReachTestIncludesAgentRadius=true` 면 **캡슐 가장자리**가 닿을 때 도착으로
#   쳐서 최대 39cm 떨어져 멈춘다 — 그만큼이 통째로 순간이동이 된다.
for st in ('Root/Seat/S_Move', 'Root/Pickup/P_Move', 'Root/Wait/W_Move'):
    r = S.set_task_property_value(ST, st, 'StateTreeMoveToTask',
                                  'bReachTestIncludesAgentRadius', 'false')
    L('  %-22s IncludesAgentRadius = false -> %s' % (st.split('/')[-1], r))
L('  -> 도착 오차 39cm → 5cm. 스냅 거리가 그만큼 줄어든다')

pkg = EAL.load_asset(ST).get_outermost()
L('  [컴파일 전] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
L('  compile -> %s' % S.compile_state_tree(ST))
L('  [컴파일 후] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

# ───────────────────────── ③④ 텍스처
L('')
L('=== ③④ 음식 텍스처 ===')
TEXTURES = [
    ('%s/_extract/burger/box/textures/box.jpg' % DL, 'T_BurgerBox_BC', False),
    ('%s/_extract/burger/box/textures/normals.jpg' % DL, 'T_BurgerBox_N', True),
    ('%s/_extract/fries/french frice_AlbedoTransparency1.png' % DL, 'T_Fries_BC', False),
    ('%s/_extract/fries/french frice_fries_Normal.png' % DL, 'T_Fries_N', True),
]
tasks = []
for src, name, is_normal in TEXTURES:
    t = unreal.AssetImportTask()
    t.set_editor_property('filename', src)
    t.set_editor_property('destination_path', TEX_DIR)
    t.set_editor_property('destination_name', name)
    t.set_editor_property('automated', True)
    t.set_editor_property('replace_existing', True)
    t.set_editor_property('save', True)
    tasks.append(t)
TOOLS.import_asset_tasks(tasks)

for src, name, is_normal in TEXTURES:
    p = '%s/%s' % (TEX_DIR, name)
    if not EAL.does_asset_exist(p):
        E('  %-18s 임포트 실패' % name)
        continue
    tex = EAL.load_asset(p)
    if is_normal:
        # ⚠ 노멀맵은 **압축 설정을 바꿔야** 한다. 안 바꾸면 sRGB 로 읽혀 표면이
        #   번들거리고 방향이 틀어진다.
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property('srgb', False)
    L('  %-18s OK %s' % (name, '(노멀)' if is_normal else ''))


# ⚠ 텍스처 연결은 `ll_food_materials.py` 가 한다 — 임포터가 만든 것은
#   MaterialInstanceConstant 라 노드를 못 넣는다.

# 영수증 — 흰 종이
L('')
L('=== ④ 영수증 머티리얼 ===')
RECEIPT = '/Game/BurgerHouse/Materials/M_Receipt'
if EAL.does_asset_exist(RECEIPT):
    L('  이미 있다')
    rm = EAL.load_asset(RECEIPT)
else:
    rm = TOOLS.create_asset('M_Receipt', '/Game/BurgerHouse/Materials',
                            unreal.Material, unreal.MaterialFactoryNew())
    c = MEL.create_material_expression(rm, unreal.MaterialExpressionConstant3Vector, -400, 0)
    c.set_editor_property('constant', unreal.LinearColor(0.93, 0.92, 0.88, 1.0))
    MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_BASE_COLOR)
    r = MEL.create_material_expression(rm, unreal.MaterialExpressionConstant, -400, 200)
    r.set_editor_property('r', 0.85)
    MEL.connect_material_property(r, '', unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(rm)
    EAL.save_asset(RECEIPT, only_if_is_dirty=False)
    L('  생성 — 살짝 누런 흰색 · 러프 0.85')

# 손님 · 카운터 트레이 양쪽의 영수증에 물린다
for bp_path, cls_suffix in (('/Game/BurgerHouse/AI/BP_Customer', 'BP_Customer_C'),
                            ('/Game/BurgerHouse/AI/BP_CounterTray', 'BP_CounterTray_C')):
    bp = EAL.load_asset(bp_path)
    if not bp:
        continue
    cdo = unreal.get_default_object(bp.generated_class())
    rc = cdo.get_editor_property('receipt')
    if rc:
        rc.set_material(0, rm)
        L('  %s 의 Receipt <- M_Receipt' % bp_path.split('/')[-1])
    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── ② 카운터 트레이 재배치
L('')
L('=== ② 카운터 트레이 — 가로 1개 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cls = unreal.load_object(None, '/Game/BurgerHouse/AI/BP_CounterTray.BP_CounterTray_C')
old = unreal.GameplayStatics.get_all_actors_of_class(world, cls) if cls else []
for a in old:
    L('  기존 %s 제거' % a.get_actor_label())
    unreal.EditorLevelLibrary.destroy_actor(a)

pickups = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
           if str(s.get_editor_property('kind')) == 'Pickup']
if pickups:
    # ⚠ 픽업 슬롯 **두 개의 중점**에 놓는다 — 문서에 `Cnt_POS`·`Cnt_Trays` 좌표가
    #   없어서(`LL_PROJECT.md` 에 `Cnt_Counter` 만 있다) 이것이 가장 근거 있는 값이다.
    xs = [p.get_actor_location().x for p in pickups]
    cx = (min(xs) + max(xs)) / 2.0
    # 카운터 상판: 블렌더 `Cnt_Counter` z 0~1.050 -> UE z 105
    where = unreal.Vector(cx, pickups[0].get_actor_location().y - 70.0, 105.0)
    # ⚠ **가로로** 놓는다. 트레이 메시가 30(X) × 40(Y) 라 그대로 두면 세로로 선다.
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, where, unreal.Rotator(0, 90, 0))
    a.set_actor_label('CounterTray')
    L('  %s @ %s · yaw 90 (가로)' % (a.get_actor_label(), where))
    L('  근거: 픽업 슬롯 x %s 의 중점 · 카운터 상판 z 105 (Cnt_Counter 0~1.050m)' % xs)

L('  레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))

L('')
L('⚠ 카운터 위 위치와 상판 높이는 여전히 **추정**이다 — 눈으로 맞출 것.')
