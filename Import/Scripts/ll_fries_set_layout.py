"""감자튀김 세트를 트레이에 배치한다 (`FRIES_ASSET.md` 의 넷 + 버거박스 + 영수증).

    에디터를 **끄고** 돌린다. 앞서 `ll_fries_set_import.py` 로 메시를 뽑아 둘 것.

| | |
|---|---|
| `Food_FriesLoose` | **×0.8** |
| `Food_Fries` | **×0.64** · 눕히기 (roll −90) · yaw −180 |
| `Food_Ketchup` | **×0.75** |
| `Food_Coke` | **×0.75** |
| `SM_BurgerBox` | ×5.0 (2인용과 같은 크기로 줄였다 — ×7.5 로는 안 들어간다) |
| 영수증 | 버거박스와 **겹쳐서** 그 아래에 깐다 |

## ⚠⚠ 피벗이 월드 원점이다 — 회전까지 고려한 역산이 필요하다

`Food_*` 는 블렌더에서 배치를 **버텍스에 구워** 내보낸 것이라 피벗이 (0,0,0) 이고
지오메트리만 테이블 위 좌표에 있다 (§`FRIES_ASSET.md` §4).

```
Food_Coke   피벗에서 중심까지 (307.1, −270.7, 82.0)
```

회전이 없으면 `중심 = loc + origin*scale` 이라 `loc = 원하는중심 − origin*scale`
하나면 됐다 (§9-8). **회전이 들어가면 그 origin 도 같이 돈다.**

```
중심  = loc + R·(origin*scale)
바닥  = 중심.z − (회전된 z 반높이)
```

그리고 회전된 축정렬 반높이는 원래 extent 를 축별로 돌려 **절대값을 합**해야 한다.

⚠ 「눕히기」는 roll +90 이다. 오른손 법칙으로 +X 축 둘레라 local **+Z → world −Y** —
  봉투 입구가 트레이 **안쪽**을 향한다. `−90` 으로 바꾸면 바깥을 향한다.

## ⚠ 영수증은 버거박스와 겹친다 — 의도한 것이다

트레이가 좁아 셋 다 따로 놓을 자리가 없다. 영수증을 상판에 깔고 그 **위에**
버거박스를 올린다. 그래서 버거박스만 3mm 띄운다 (z-파이팅 방지).

⚠ 다만 **정확히 겹치면 안 된다** — 영수증(12 × 7.5)이 버거박스(12.6 × 13.1)보다
  작아 통째로 가려진다. 앞쪽으로 **1.75cm** 삐져나오게 중심을 어긋냈다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

FOOD = '/Game/BurgerHouse/Props/Food'

# ## ⚠⚠ 트레이는 **접시**다 — 얹히는 면은 테두리가 아니라 안쪽 바닥이다
#
# `Props/Tray` 는 높이 2.2 인데, 법선이 위를 보는 면이 **둘**이다 —
#
# ```
# z = 0.20   안쪽 바닥   X ±12.3 · Y ±17.3   <- 물건이 얹히는 곳
# z = 2.20   테두리 윗면 X ±14.1 · Y ±19.1
# ```
#
# 전에는 AABB 상단(2.2)을 상판으로 잡아 **전부 2.05cm 떠 있었다.** 큰 것일수록
# 눈에 띄어 「감자봉투가 떠 있다」로 보였다.
#
# ⚠ 쓸 수 있는 넓이도 30 × 40 이 아니라 **24.7 × 34.7** 이다. AABB 크기를 그대로
#   믿으면 안 된다.
TRAY_FLOOR = 0.2
INNER_X = 12.35      # 안쪽 바닥의 반폭
INNER_Y = 17.35
LIFT = 0.05          # 바닥에서 띄우는 기본값
BOX_LIFT = 0.35      # 버거박스는 영수증 **위**에 얹는다

# (컴포넌트, 메시, 스케일, 회전(roll,pitch,yaw), 트레이 위 중심 XY, 띄움)
LAYOUT = [
    # ⚠ 안쪽 바닥이 24.7 × 34.7 이라 예전 자리 그대로는 몇 개가 **밖으로 나간다.**
    #   봉투감자와 흩어진 감자를 조금씩 안쪽으로 당겼다.
    ('fries',       '%s/Food_Fries' % FOOD,                       0.64, (-90, 0, -180), (7.6, -11.2),  LIFT),
    ('burger_box',  '/Game/BurgerHouse/Props/Food/SM_BurgerBox',  5.0,  (0, 0, 0),      (-6.0, -10.0), BOX_LIFT),
    # ⚠⚠ 영수증은 **버거박스를 따라간다** — 박스 중심 + (0.3, +4.5).
    ('receipt',     '/Engine/BasicShapes/Plane',                  None, (0, 0, 0),      (-5.7, -5.5),  LIFT),
    ('coke',        '%s/Food_Coke' % FOOD,                        0.75, (0, 0, 0),      (-7.0, 8.0),   LIFT),
    # ⚠ 케찹과 흩어진 감자는 자리를 맞바꾸고, 둘 다 봉투감자 쪽(−Y)으로 붙였다.
    # ⚠ 셋 다 트레이 **오른쪽(+Y)** 으로 2cm 밀었다. 트레이의 +Y 는 든 사람의
    #   오른쪽이다 (`ll_tray_hands.py` 가 그렇게 축을 잡았다).
    ('fries_loose', '%s/Food_FriesLoose' % FOOD,                  0.8,  (0, 0, 0),      (7.9, 0.0),    LIFT),
    ('ketchup',     '%s/Food_Ketchup' % FOOD,                     0.75, (0, 0, 0),      (8.0, 4.5),    LIFT),
]

# 영수증만 비균등 스케일 (12 × 7.5cm)
RECEIPT_SCALE = unreal.Vector(0.12, 0.075, 1.0)

# 겹쳐도 되는 짝 — 사용자가 허용했다
ALLOW_OVERLAP = {frozenset(('receipt', 'burger_box'))}

fails = []


def rotated_extent(rot, e):
    """회전된 축정렬 반extent — 축별로 돌려 **절대값을 합**한다."""
    t = unreal.Transform(unreal.Vector(0, 0, 0), rot, unreal.Vector(1, 1, 1))
    ax = unreal.MathLibrary.transform_location(t, unreal.Vector(e.x, 0, 0))
    ay = unreal.MathLibrary.transform_location(t, unreal.Vector(0, e.y, 0))
    az = unreal.MathLibrary.transform_location(t, unreal.Vector(0, 0, e.z))
    return unreal.Vector(abs(ax.x) + abs(ay.x) + abs(az.x),
                         abs(ax.y) + abs(ay.y) + abs(az.y),
                         abs(ax.z) + abs(ay.z) + abs(az.z))


def solve(mesh_path, scale, rot, xc, yc, lift):
    """중심을 (xc,yc) 에, 바닥을 상판에 두는 **상대 트랜스폼**과 실제 크기."""
    b = EAL.load_asset(mesh_path).get_bounds()
    o = unreal.Vector(b.origin.x * scale.x, b.origin.y * scale.y, b.origin.z * scale.z)
    e = unreal.Vector(b.box_extent.x * scale.x, b.box_extent.y * scale.y, b.box_extent.z * scale.z)
    t = unreal.Transform(unreal.Vector(0, 0, 0), rot, unreal.Vector(1, 1, 1))
    ro = unreal.MathLibrary.transform_location(t, o)      # 회전된 피벗→중심 벡터
    re = rotated_extent(rot, e)                            # 회전된 반extent
    loc = unreal.Vector(xc - ro.x, yc - ro.y, TRAY_FLOOR + lift + re.z - ro.z)
    return loc, re


L('R === 트레이 배치 ===')
spans = []
for bp_path in ('/Game/BurgerHouse/BP/BP_Customer', '/Game/BurgerHouse/BP/BP_CounterTray'):
    bp = EAL.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    L('R   --- %s' % bp_path.split('/')[-1])
    spans = []
    for prop, mesh_path, sc, rot3, center, lift in LAYOUT:
        c = cdo.get_editor_property(prop)
        if not c:
            E('R     컴포넌트 %s 가 없다 — C++ 빌드를 확인할 것' % prop)
            fails.append(prop)
            continue
        if not EAL.does_asset_exist(mesh_path):
            E('R     메시 %s 가 없다' % mesh_path)
            fails.append(mesh_path)
            continue
        scale = RECEIPT_SCALE if sc is None else unreal.Vector(sc, sc, sc)
        rot = unreal.Rotator(rot3[0], rot3[1], rot3[2])
        xc, yc = center
        loc, re = solve(mesh_path, scale, rot, xc, yc, lift)

        c.set_static_mesh(EAL.load_asset(mesh_path))
        c.set_editor_property('relative_location', loc)
        c.set_editor_property('relative_rotation', rot)
        c.set_editor_property('relative_scale3d', scale)

        spans.append((prop, xc - re.x, xc + re.x, yc - re.y, yc + re.y))
        over = []
        if abs(xc) + re.x > INNER_X + 0.01:
            over.append('X')
        if abs(yc) + re.y > INNER_Y + 0.01:
            over.append('Y')
        L('R     %-12s %5.1f x %5.1f x %5.1f  중심(%5.1f,%6.1f) roll %3.0f  loc(%8.1f %8.1f %6.1f) %s'
          % (prop, re.x * 2, re.y * 2, re.z * 2, xc, yc, rot3[0], loc.x, loc.y, loc.z,
             ('트레이 밖! ' + ''.join(over)) if over else ''))
        if over:
            fails.append('%s 트레이 밖' % prop)

    # 쓰지 않는 컴포넌트는 메시를 비운다 (2인용 전용)
    for prop in ('burger_box2',):
        try:
            c = cdo.get_editor_property(prop)
        except Exception:
            continue
        if c and c.static_mesh:
            L('R     %-12s (2인용 전용) 그대로 둔다' % prop)

    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

L('R')
L('R   겹침 검사 (영수증/버거박스는 허용)')
area = 0.0
for i in range(len(spans)):
    a = spans[i]
    area += (a[2] - a[1]) * (a[4] - a[3])
    for j in range(i + 1, len(spans)):
        b = spans[j]
        hit = (a[1] < b[2] and b[1] < a[2] and a[3] < b[4] and b[3] < a[4])
        if not hit:
            continue
        if frozenset((a[0], b[0])) in ALLOW_OVERLAP:
            L('R     %-12s / %-12s 겹침 — 허용' % (a[0], b[0]))
        else:
            E('R     %-12s / %-12s 겹친다' % (a[0], b[0]))
            fails.append('%s/%s 겹침' % (a[0], b[0]))
INNER_AREA = INNER_X * 2 * INNER_Y * 2
L('R   합계 면적 %.0f / %.0f cm2 = %.0f%% (영수증은 버거박스와 겹치므로 중복 계산)'
  % (area, INNER_AREA, area / INNER_AREA * 100))

L('R')
if fails:
    E('R === 실패: %s ===' % fails)
else:
    L('R === 완료 (검산은 새 프로세스로) ===')
