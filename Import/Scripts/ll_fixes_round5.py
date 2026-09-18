"""라운드 5 — 영수증 비율 · 키오스크 몸짓 속도/횟수.

    에디터를 **끄고** 돌린다.

| | |
|---|---|
| 영수증 | 세로 **절반**, 가로 **3배** → 4 × 15 에서 **12 × 7.5** |
| `K_Act` | `Reaching_Out` 을 **2배 속도로 3번** |

## ⚠ 영수증이 가로로 커지면 감자와 부딪힌다

예전 배치는 감자와 영수증을 **X 로 나란히** 놨다 (§9-16). 영수증이 4 → 12 로
넓어지면 그 자리에 안 들어간다. 그래서 다시 **Y 한 줄**로 돌린다 —

```
19.6(버거박스) + 7.0(감자) + 7.5(영수증) = 34.1 < 트레이 Y 40   들어간다
```

⚠ 스크립트가 겹침을 사각형으로 검사한다. 눈으로 보기 전에 잡으려고.

## `Plays` 와 `RandomExtraRepeats` 는 다르다

`RandomExtraRepeats` 는 **무작위**(0~N)라 「정확히 3번」을 못 만든다. 이번에
`Plays`(총 재생 횟수)를 새로 만들었다.

⚠ `PlayRate` 를 올리면 **머무는 시간도 그만큼 짧아진다** — `PlayAction` 이
  속도를 나눈 실제 재생 시간을 돌려주기 때문이다. 4초짜리를 2배속 3번이면
  4/2 × 3 = **6초**다 (예전 4초).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/AI/ST_Customer'
TRAY_TOP = 2.2
fails = []

# ───────────────────────── 영수증 비율 + 재배치
L('R === 영수증 — 세로 1/2 · 가로 3배 ===')
# (컴포넌트, 메시, 스케일, 트레이 위 중심 XY)
#
# ⚠ 영수증 X 0.04 -> 0.12 (3배) · Y 0.15 -> 0.075 (절반) = 12 x 7.5cm
LAYOUT = [
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox',
     unreal.Vector(7.5, 7.5, 7.5), (0.0, -9.0)),
    ('fries', '/Game/BurgerHouse/Props/Food/SM_Fries',
     unreal.Vector(0.6, 0.6, 0.6), (0.0, 5.0)),
    ('receipt', '/Engine/BasicShapes/Plane',
     unreal.Vector(0.12, 0.075, 1.0), (0.0, 13.0)),
]


def place(mesh_path, scale, xc, yc):
    """bbox 바닥을 상판에 붙이고 중심을 (xc,yc) 에 두는 상대 위치 (§9-8 의 식)."""
    b = EAL.load_asset(mesh_path).get_bounds()
    o, e = b.origin, b.box_extent
    return unreal.Vector(xc - o.x * scale.x,
                         yc - o.y * scale.y,
                         TRAY_TOP + 0.05 + (e.z - o.z) * scale.z)


spans = []
for bp_path in ('/Game/BurgerHouse/AI/BP_Customer', '/Game/BurgerHouse/AI/BP_CounterTray'):
    bp = EAL.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    L('R   --- %s' % bp_path.split('/')[-1])
    spans = []
    for prop, mesh, scale, center in LAYOUT:
        xc, yc = center
        c = cdo.get_editor_property(prop)
        loc = place(mesh, scale, xc, yc)
        c.set_editor_property('relative_location', loc)
        c.set_editor_property('relative_scale3d', scale)
        b = EAL.load_asset(mesh).get_bounds()
        sx = b.box_extent.x * 2 * scale.x
        sy = b.box_extent.y * 2 * scale.y
        spans.append((prop, xc - sx / 2, xc + sx / 2, yc - sy / 2, yc + sy / 2))
        over = []
        if abs(xc) + sx / 2 > 15.0:
            over.append('X')
        if abs(yc) + sy / 2 > 20.0:
            over.append('Y')
        L('R     %-11s %.1f x %.1f  중심(%.1f, %.1f)  X[%.1f..%.1f] Y[%.1f..%.1f] %s'
          % (prop, sx, sy, xc, yc, xc - sx / 2, xc + sx / 2, yc - sy / 2, yc + sy / 2,
             ('트레이 밖! ' + ''.join(over)) if over else ''))
        if over:
            fails.append('%s.%s 트레이 밖' % (bp_path.split('/')[-1], prop))
    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

L('R   겹침 검사')
for i in range(len(spans)):
    for j in range(i + 1, len(spans)):
        a, b = spans[i], spans[j]
        hit = (a[1] < b[2] and b[1] < a[2] and a[3] < b[4] and b[3] < a[4])
        L('R     %-11s / %-11s %s' % (a[0], b[0], '겹친다!' if hit else '안 겹침'))
        if hit:
            fails.append('%s/%s 겹침' % (a[0], b[0]))

# ───────────────────────── 키오스크 몸짓
L('R')
L('R === K_Act — Reaching_Out 2배속 3회 ===')
for prop, val in (('ActionName', 'Interaction_Reach'), ('PlayRate', '2.0'), ('Plays', '3')):
    r = S.set_task_property_value(ST, 'Root/Kiosk/K_Act', 'BurgerHouseActionTask', prop, val)
    L('R   %-12s = %-20s %s' % (prop, val, r))
    if not r:
        fails.append('K_Act.%s' % prop)

d = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
     for p in (S.get_task_property_names(ST, 'Root/Kiosk/K_Act', 'BurgerHouseActionTask') or [])}
L('R   되읽기 Action=%s Plays=%s PlayRate=%s RandomExtraRepeats=%s'
  % (d.get('ActionName'), d.get('Plays'), d.get('PlayRate'), d.get('RandomExtraRepeats')))

# 머무는 시간 계산 — 눈으로 확인할 수 있게 찍는다
m = EAL.load_asset('/Game/BurgerHouse/Anims/Manny/Montages/AM_Reaching_Out')
if m:
    one = m.get_play_length()
    L('R   AM_Reaching_Out %.2fs · 2배속이면 %.2fs · 3번이면 **%.2fs**'
      % (one, one / 2.0, one / 2.0 * 3))

pkg = EAL.load_asset(ST).get_outermost()
L('R   save %s / compile %s / save %s' % (
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False),
    S.compile_state_tree(ST).get_editor_property('success'),
    unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)))

L('R')
if fails:
    E('R === 실패: %s ===' % fails)
else:
    L('R === 적용 (검산은 새 프로세스 + PIE 로) ===')
