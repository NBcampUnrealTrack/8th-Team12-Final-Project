"""2인용 테이블 트레이 + `L_Gone` 에 디스폰 태스크.

    에디터를 **끄고** 돌린다.

## ① 마주보고 앉으면 트레이가 겹친다

프리필 손님은 각자 자기 앞 `TrayTableOffset`(45cm) 에 트레이를 놓는다. 마주보는
짝은 테이블 한가운데서 만난다 — 실측 —

```
C_3 트레이 (344.80, −464.60)
C_4 트레이 (343.32, −471.08)     6.5cm 떨어져 있었다
C_0 트레이 ( 85.53, −716.56)
C_1 트레이 ( 69.00, −704.88)    20.2cm
C_2 트레이 ( 81.27, −763.42)    가장 가까운 남의 것이 47cm — 혼자다
```

그래서 짝마다 **2인용 트레이 액터 하나**를 그 중점에 놓는다. 손님 쪽은 C++ 이
`SharedTrayRadius`(40cm) 안에 트레이 액터가 있으면 **자기 것을 안 놓는다.**

⚠ 크기는 1인용과 **같다**. 담는 것만 버거박스 2 · 감자 1 · 영수증 1 이다.

## ② `L_Gone` 에 디스폰 태스크가 **안 붙어 있었다**

`FBurgerHouseDespawnTask`(`Despawn Customer`)는 C++ 에 있는데 트리의 `L_Gone`
상태에는 아무 태스크도 없었다. 그래서 손님이 출구에 닿아도 안 사라진다.

⚠ `Root/Leave` 가 그 자리에서 끝나던 것은 C++ 에서 고쳤다 (`Get Store Point` 가
  `Running` 을 돌려주도록). 그 둘이 같이 고쳐져야 퇴장이 완성된다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/BP/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
BP_DIR = '/Game/BurgerHouse/BP'
BP_PATH = '%s/BP_TableTray' % BP_DIR

# ⚠⚠ 트레이는 **접시**다 — 얹히는 면은 테두리(2.2)가 아니라 **안쪽 바닥(0.2)** 이다.
#    쓸 수 있는 넓이도 30 × 40 이 아니라 24.7 × 34.7 이다 (§9-29).
FOOD = '/Game/BurgerHouse/Props/Food'
TRAY_FLOOR = 0.2
INNER_X = 12.35
INNER_Y = 17.35
LIFT = 0.05
ON_RECEIPT = 0.35     # 영수증 위에 얹히는 것
fails = []

# ───────────────────────── ② L_Gone 에 디스폰
L('T === L_Gone 에 Despawn Customer ===')
r = S.add_task(ST, 'Root/Leave/L_Gone', 'BurgerHouseDespawnTask')
L('T   add_task -> %s' % r)
if not r:
    # 이미 있을 수도 있다 — 확인
    d = S.get_task_property_names(ST, 'Root/Leave/L_Gone', 'BurgerHouseDespawnTask')
    if d:
        L('T   이미 붙어 있다')
    else:
        E('T   붙이기 실패')
        fails.append('L_Gone Despawn')
# ⚠ Customer 바인딩이 있어야 한다 — 없으면 "바인딩이 비었다" 경고만 찍고 안 죽는다.
rb = S.bind_task_property_to_context(ST, 'Root/Leave/L_Gone', 'BurgerHouseDespawnTask',
                                     'Customer', 'Actor')
L('T   Customer <- 컨텍스트 Actor : %s' % rb)

pkg = EAL.load_asset(ST).get_outermost()
L('T   save %s / compile %s / save %s' % (
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False),
    S.compile_state_tree(ST).get_editor_property('success'),
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)))

# ───────────────────────── ① 2인용 트레이 블루프린트
L('T')
L('T === BP_TableTray ===')
if EAL.does_asset_exist(BP_PATH):
    bp = EAL.load_asset(BP_PATH)
    L('T   이미 있다')
else:
    f = unreal.BlueprintFactory()
    f.set_editor_property('parent_class', unreal.CounterTray)
    bp = TOOLS.create_asset('BP_TableTray', BP_DIR, unreal.Blueprint, f)
    L('T   생성 %s' % (bp is not None))
if not bp:
    E('T   BP 생성 실패')
    raise SystemExit

cdo = unreal.get_default_object(bp.generated_class())
# ⚠ 파이썬은 bool UPROPERTY 의 앞 b 를 뗀다 — bTwoPersonSet -> two_person_set
cdo.set_editor_property('two_person_set', True)
cdo.set_editor_property('bind_to_store', False)

# 메시·머티리얼은 카운터 트레이와 같은 것을 쓴다
src = unreal.get_default_object(EAL.load_asset('/Game/BurgerHouse/BP/BP_CounterTray').generated_class())

# 2인용 배치 — 트레이 X 30 × Y 40 안에서 버거박스 둘이 **가로로 나란히**
#
# ⚠ 1인용 버거박스(×7.5, 18.8 넓이) 둘은 X 30 에 안 들어간다. ×5.0(12.5)으로
#   줄여야 둘이 들어간다. 「크기는 같지만 담는 것이 다르다」의 대가다.
# ⚠⚠ 배치는 **두 사람을 잇는 축을 따라** 나눈다. 버거박스를 나란히 두면 둘 다
#   한쪽 사람 앞에 몰려서 1인용 트레이에 버거만 둘인 그림이 된다.
#
#   트레이 긴 축(Y 40)을 마주보는 선에 맞추고(아래 yaw), 버거박스를 **양 끝**에
#   하나씩. 감자·영수증은 가운데에 나눠 놓는다.
# ## ⚠⚠ 「배치는 그대로」가 **기하학적으로 불가능**했다
#
# 버거박스 둘을 양 끝(±10.8)에 두면 남는 곳이 이것뿐이다 —
#
# ```
# 가운데 띠   깊이  8.50
# 양옆 띠     폭    6.05
# ```
#
# 1인용과 같은 크기의 봉투감자는 **9.3 × 12.2** 라 둘 다 못 들어간다. 콜라(8.0 ×
# 6.7)도 옆 띠에 안 들어간다. 그래서 버거박스 둘을 **왼쪽 열**로 모으고 오른쪽을
# 음식 열로 썼다.
#
# ⚠ 대신 마주보는 두 사람이 각자 자기 앞에서 버거를 집는 그림은 약해진다.
#   「버거를 양 끝에」가 더 중요하면 감자 세트 쪽을 줄여야 한다.
#
# ⚠ 영수증은 **버거박스가 아니라 봉투감자 밑**에 깐다 (요청). 그래서 띄움도
#   봉투감자가 받는다.
#
# (컴포넌트, 메시, 스케일, 회전(roll,pitch,yaw), 중심 XY, 띄움)
LAYOUT = [
    ('burger_box',  '/Game/BurgerHouse/Props/Food/SM_BurgerBox', unreal.Vector(5.0, 5.0, 5.0),   (0, 0, 0),      (-6.0, -10.8), LIFT),
    ('burger_box2', '/Game/BurgerHouse/Props/Food/SM_BurgerBox', unreal.Vector(5.0, 5.0, 5.0),   (0, 0, 0),      (-6.0, 10.8),  LIFT),
    ('receipt',     '/Engine/BasicShapes/Plane',                 unreal.Vector(0.12, 0.075, 1.0), (0, 0, 0),     (6.35, -5.0),  LIFT),
    ('fries',       '%s/Food_Fries' % FOOD,                      unreal.Vector(0.64, 0.64, 0.64), (-90, 0, -180), (7.2, -10.0), ON_RECEIPT),
    ('coke',        '%s/Food_Coke' % FOOD,                       unreal.Vector(0.75, 0.75, 0.75), (0, 0, 0),      (7.5, 4.0),   LIFT),
    ('fries_loose', '%s/Food_FriesLoose' % FOOD,                 unreal.Vector(0.8, 0.8, 0.8),   (0, 0, 0),      (7.5, 10.5),  LIFT),
    ('ketchup',     '%s/Food_Ketchup' % FOOD,                    unreal.Vector(0.75, 0.75, 0.75), (0, 0, 0),     (7.5, 15.3),  LIFT),
]


def rotated_extent(rot, e):
    """회전된 축정렬 반extent — 축별로 돌려 절대값을 합한다 (§9-27)."""
    t = unreal.Transform(unreal.Vector(0, 0, 0), rot, unreal.Vector(1, 1, 1))
    ax = unreal.MathLibrary.transform_location(t, unreal.Vector(e.x, 0, 0))
    ay = unreal.MathLibrary.transform_location(t, unreal.Vector(0, e.y, 0))
    az = unreal.MathLibrary.transform_location(t, unreal.Vector(0, 0, e.z))
    return unreal.Vector(abs(ax.x) + abs(ay.x) + abs(az.x),
                         abs(ax.y) + abs(ay.y) + abs(az.y),
                         abs(ax.z) + abs(ay.z) + abs(az.z))


def solve(mesh_path, scale, rot, xc, yc, lift):
    """중심을 (xc,yc) 에, 바닥을 **안쪽 바닥**에 두는 상대 트랜스폼과 실제 크기."""
    b = EAL.load_asset(mesh_path).get_bounds()
    o = unreal.Vector(b.origin.x * scale.x, b.origin.y * scale.y, b.origin.z * scale.z)
    e = unreal.Vector(b.box_extent.x * scale.x, b.box_extent.y * scale.y, b.box_extent.z * scale.z)
    t = unreal.Transform(unreal.Vector(0, 0, 0), rot, unreal.Vector(1, 1, 1))
    ro = unreal.MathLibrary.transform_location(t, o)
    re = rotated_extent(rot, e)
    return unreal.Vector(xc - ro.x, yc - ro.y, TRAY_FLOOR + lift + re.z - ro.z), re


tray_c = cdo.get_editor_property('tray')
src_tray = src.get_editor_property('tray')
tray_c.set_static_mesh(src_tray.static_mesh)
L('T   Tray 메시 %s' % src_tray.static_mesh.get_name())

spans = []
for prop, mesh_path, scale, rot3, center, lift in LAYOUT:
    xc, yc = center
    c = cdo.get_editor_property(prop)
    if not c:
        E('T   컴포넌트 %s 가 없다 — C++ 빌드를 확인할 것' % prop)
        fails.append(prop)
        continue
    m = EAL.load_asset(mesh_path)
    if not m:
        E('T   메시 %s 가 없다' % mesh_path)
        fails.append(mesh_path)
        continue
    rot = unreal.Rotator(rot3[0], rot3[1], rot3[2])
    loc, re = solve(mesh_path, scale, rot, xc, yc, lift)
    c.set_static_mesh(m)
    c.set_editor_property('relative_location', loc)
    c.set_editor_property('relative_rotation', rot)
    c.set_editor_property('relative_scale3d', scale)
    ref = src.get_editor_property('burger_box' if 'burger' in prop else prop)
    if ref and ref.get_material(0):
        c.set_material(0, ref.get_material(0))
    spans.append((prop, xc - re.x, xc + re.x, yc - re.y, yc + re.y))
    over = []
    if abs(xc) + re.x > INNER_X + 0.01:
        over.append('X')
    if abs(yc) + re.y > INNER_Y + 0.01:
        over.append('Y')
    L('T   %-12s %5.1f x %5.1f  중심(%5.1f,%6.1f) roll %4.0f  X[%6.1f..%6.1f] Y[%6.1f..%6.1f] %s'
      % (prop, re.x * 2, re.y * 2, xc, yc, rot3[0], xc - re.x, xc + re.x, yc - re.y, yc + re.y,
         ('트레이 밖! ' + ''.join(over)) if over else ''))
    if over:
        fails.append('%s 트레이 밖' % prop)

L('T   겹침 검사')
for a in range(len(spans)):
    for b2 in range(a + 1, len(spans)):
        p, q = spans[a], spans[b2]
        hit = (p[1] < q[2] and q[1] < p[2] and p[3] < q[4] and q[3] < p[4])
        if hit:
            # ⚠ 영수증/봉투감자는 **일부러** 겹친다 (영수증을 그 밑에 깐다)
            if {p[0], q[0]} == {'receipt', 'fries'}:
                L('T     receipt / fries 겹침 — 허용 (영수증을 봉투감자 밑에 깔았다)')
            else:
                E('T     %s / %s 겹친다' % (p[0], q[0]))
                fails.append('%s/%s 겹침' % (p[0], q[0]))
L('T     겹침 %d건' % sum(1 for f in fails if '겹침' in f))

unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── 마주보는 짝 찾아 배치
L('T')
L('T === 마주보는 프리필 짝 찾기 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

spawner = None
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    spawner = sp
    break
if not spawner:
    E('T   스포너가 없다')
    raise SystemExit

cust_cdo = unreal.get_default_object(
    EAL.load_asset('/Game/BurgerHouse/BP/BP_Customer').generated_class())
offset = cust_cdo.get_editor_property('tray_table_offset')
L('T   TrayTableOffset %s' % offset)

# 프리필이 앉을 좌석 = 배치 지점에서 **가장 가까운** Seat 슬롯 (ClaimNearestSlot 과 같은 규칙)
seats = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
         if str(s.get_editor_property('kind')) == 'Seat']

spots = []
for e in spawner.get_editor_property('prefill_placements'):
    if str(e.get_editor_property('role')) != 'Eating':
        continue
    t = e.get_editor_property('transform')
    best, bd = None, 1e9
    for s in seats:
        # ⚠ `a - b` 가 이미 Vector 다. `unreal.Vector(...)` 로 감싸면 TypeError.
        d = (s.get_actor_location() - t.translation).length()
        if d < bd:
            best, bd = s, d
    if not best:
        continue
    use = best.get_use_transform()
    spots.append((best.get_actor_label(),
                  unreal.MathLibrary.transform_location(use, offset)))
    L('T   %-16s 좌석 %-16s (최근접 %.0fcm) -> 트레이 %s'
      % (str(e.get_editor_property('role')), best.get_actor_label(), bd,
         spots[-1][1]))

PAIR = 40.0
used = set()
pairs = []
for a in range(len(spots)):
    if a in used:
        continue
    for b2 in range(a + 1, len(spots)):
        if b2 in used:
            continue
        d = (spots[a][1] - spots[b2][1]).length()
        if d <= PAIR:
            pairs.append((a, b2, d))
            used.add(a)
            used.add(b2)
            break
L('T   짝 %d쌍 · 혼자 %d명' % (len(pairs), len(spots) - len(used)))

cls = unreal.load_object(None, '%s.BP_TableTray_C' % BP_PATH)
existing = list(unreal.GameplayStatics.get_all_actors_of_class(world, cls)) if cls else []

# ## ⚠⚠ 이미 놓인 트레이는 **위치를 안 건드린다**
#
# 사람이 에디터에서 눈으로 맞춘 자리가 자동 계산보다 정확하다. 실제로 5.3cm 를
# 옮겨 놓으셨는데 예전 판은 그것을 지우고 다시 스폰해 **매번 날려 버렸다**.
#
# ⚠ 짝 수가 바뀌었을 때만 새로 만든다. 그때는 자연히 위치가 초기화된다.
if len(existing) == len(pairs) and existing:
    L('T   기존 트레이 %d개 — **위치 유지** (사람이 맞춘 값이 이긴다)' % len(existing))
    for a in existing:
        l = a.get_actor_location()
        L('T     %-14s (%.1f %.1f %.1f) yaw %.0f'
          % (a.get_actor_label(), l.x, l.y, l.z, a.get_actor_rotation().yaw))
else:
    for a in existing:
        L('T   기존 %s 제거 (짝 수가 %d -> %d 로 바뀜)'
          % (a.get_actor_label(), len(existing), len(pairs)))
        unreal.EditorLevelLibrary.destroy_actor(a)

    for idx, (a, b2, d) in enumerate(pairs):
        pa, pb = spots[a][1], spots[b2][1]
        mid = unreal.Vector((pa.x + pb.x) / 2, (pa.y + pb.y) / 2, (pa.z + pb.z) / 2)
        # ⚠ 트레이 긴 축(**+Y**, 40)을 두 사람을 잇는 선에 맞춘다. 액터 yaw 는 +X
        #   기준이니 **−90** 을 더해야 +Y 가 그 선을 향한다.
        import math
        yaw = math.degrees(math.atan2(pb.y - pa.y, pb.x - pa.x)) - 90.0
        act = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, mid, unreal.Rotator(0, 0, yaw))
        act.set_actor_label('TableTray_%d' % idx)
        L('T   TableTray_%d @ (%.0f %.0f %.0f) yaw %.0f  (두 트레이 %.1fcm 사이)'
          % (idx, mid.x, mid.y, mid.z, yaw, d))

L('T   레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))

L('T')
if fails:
    E('T === 실패: %s ===' % fails)
else:
    L('T === 완료 (검산은 PIE 로) ===')
