"""폰이 **귀에 닿도록** `PhoneHandOffset` 을 실측으로 푼다.

    에디터를 **끄고** 돌린다. `ll_phone_import.py` 다음.

## 어느 손인가 — 숫자로 고른다

`Talking_On_A_Cell_Phone` 에서 머리까지의 거리 —

```
hand_r  최소 26.5 · 평균 37.7      <- 이쪽이 귀로 간다
hand_l  최소 40.5 · 평균 65.1
```

## 어떻게 놓나

손 **로컬 공간**에서 머리가 어디인지 재고, 그 방향으로 폰을 세운다. 애님 내내
거의 안 움직인다 (실측 t=9.3/18.6/27.9 에서 (−22.1, −11.7, 9.2)~(−22.6, −11.3,
11.2)) — 고정 오프셋으로 충분하다.

```
화면(폰 −Y)  머리 쪽을 본다
긴 축(폰 +Z) 머리 쪽으로 기울여 세운다
```

⚠ `Prop_Phone` 은 긴 축이 **Z**(404), 얇은 축이 **Y**(31.5) 다. 축을 잘못 잡으면
  폰이 옆으로 눕는다.

⚠⚠ 피벗이 메시 중심이 아니다 — 바운즈 중심이 (22.3, 5.9, 261.5) 다. 그래서
  **중심을 원하는 자리에 두는** 역산이 필요하다 (§9-27 과 같은 식).

## ⚠ 크기

본체가 404cm 다. 실물 15cm 로 맞추면 스케일 **0.0371**.
"""
import unreal
import math

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
AL = unreal.AnimationLibrary

ANIM = '/Game/BurgerHouse/Anims/Manny/Talking_On_A_Cell_Phone'
MESH = '/Game/BurgerHouse/Props/Phone/Prop_Phone'
BP = '/Game/BurgerHouse/BP/BP_Customer'
HAND = 'hand_r'

PHONE_LEN = 15.0        # 실물 길이(cm)
EAR_GAP = 3.0           # 귀에서 이만큼 띄운다 (머리 반지름을 대충 뺀 값)

anim = EAL.load_asset(ANIM)
mesh = EAL.load_asset(MESH)
if not anim or not mesh:
    E('H  애님(%s) 또는 메시(%s) 가 없다' % (ANIM, MESH))
    raise SystemExit


def comp(bone, t):
    """컴포넌트 공간 — 로컬을 루트부터 합성한다 (§9-21)."""
    acc = unreal.Transform()
    for b in reversed(list(AL.find_bone_path_to_root(anim, bone))):
        acc = unreal.MathLibrary.compose_transforms(
            AL.get_bone_pose_for_time(anim, str(b), t, False), acc)
    return acc


def norm(v):
    n = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
    return unreal.Vector(v.x / n, v.y / n, v.z / n) if n > 1e-6 else unreal.Vector(1, 0, 0)


def cross(a, b):
    return unreal.Vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)


ln = anim.get_play_length()
L('H === 손 로컬 기준 머리 위치 ===')
rel = []
for k in (4, 6, 8):
    t = ln * (k / 12.0)
    h = comp(HAND, t)
    hd = comp('head', t).translation
    up_world = unreal.MathLibrary.inverse_transform_direction(h, unreal.Vector(0, 0, 1))
    r = unreal.MathLibrary.inverse_transform_location(h, hd)
    rel.append((r, up_world))
    L('H   t=%5.2f  머리 (%6.2f %6.2f %6.2f) · 손 기준 월드위 (%5.2f %5.2f %5.2f)'
      % (t, r.x, r.y, r.z, up_world.x, up_world.y, up_world.z))

avg = unreal.Vector(sum(r[0].x for r in rel) / len(rel),
                    sum(r[0].y for r in rel) / len(rel),
                    sum(r[0].z for r in rel) / len(rel))
up = norm(unreal.Vector(sum(r[1].x for r in rel) / len(rel),
                        sum(r[1].y for r in rel) / len(rel),
                        sum(r[1].z for r in rel) / len(rel)))
to_head = norm(avg)
dist = math.sqrt(avg.x ** 2 + avg.y ** 2 + avg.z ** 2)
L('H   평균 머리 방향 (%.3f %.3f %.3f) · 거리 %.1fcm · 손 기준 위 (%.3f %.3f %.3f)'
  % (to_head.x, to_head.y, to_head.z, dist, up.x, up.y, up.z))

# ───────────────────────── 축을 세운다
# ⚠ 화면(폰 −Y)이 머리를 본다 -> 폰 +Y = 머리 **반대**
y_ax = unreal.Vector(-to_head.x, -to_head.y, -to_head.z)
# 긴 축(+Z)은 「위」에서 y 성분을 뺀 것 — y 와 직각이어야 회전이 성립한다
d = up.x * y_ax.x + up.y * y_ax.y + up.z * y_ax.z
z_ax = norm(unreal.Vector(up.x - d * y_ax.x, up.y - d * y_ax.y, up.z - d * y_ax.z))
x_ax = norm(cross(y_ax, z_ax))          # X = Y × Z (§9-21 의 그 순서)
rot = unreal.MathLibrary.make_rotation_from_axes(x_ax, y_ax, z_ax)

# ───────────────────────── 위치 — **중심**을 귀 앞에 둔다
b = mesh.get_bounds()
scale = PHONE_LEN / (b.box_extent.z * 2)
L('H')
L('H === 폰 ===')
L('H   원본 %.2f x %.2f x %.2f · 스케일 %.4f -> %.2f x %.2f x %.2f'
  % (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2, scale,
     b.box_extent.x * 2 * scale, b.box_extent.y * 2 * scale, b.box_extent.z * 2 * scale))

want_centre = unreal.Vector(to_head.x * (dist - EAR_GAP),
                            to_head.y * (dist - EAR_GAP),
                            to_head.z * (dist - EAR_GAP))
# ⚠ 피벗이 중심이 아니다. `중심 = loc + R·(origin*scale)` 을 뒤집는다 (§9-27).
o = unreal.Vector(b.origin.x * scale, b.origin.y * scale, b.origin.z * scale)
tr = unreal.Transform(unreal.Vector(0, 0, 0), rot, unreal.Vector(1, 1, 1))
ro = unreal.MathLibrary.transform_location(tr, o)
loc = unreal.Vector(want_centre.x - ro.x, want_centre.y - ro.y, want_centre.z - ro.z)

offset = unreal.Transform(loc, rot, unreal.Vector(scale, scale, scale))
L('H   PhoneHandOffset  loc(%.2f %.2f %.2f) rot(%.1f %.1f %.1f) scale %.4f'
  % (loc.x, loc.y, loc.z, rot.roll, rot.pitch, rot.yaw, scale))

# ───────────────────────── BP 에 꽂는다
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
ph = cdo.get_editor_property('phone')
if not ph:
    E('H   Phone 컴포넌트가 없다 — C++ 빌드를 확인할 것')
    raise SystemExit
ph.set_static_mesh(mesh)
cdo.set_editor_property('phone_socket', HAND)
cdo.set_editor_property('phone_hand_offset', offset)
L('H   PhoneSocket = %s · 메시 = %s' % (HAND, mesh.get_name()))
L('H   save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── 되짚기 — 정말 귀 옆인가
L('H')
L('H === 되짚기: 폰 중심이 머리에서 얼마나 떨어져 있나 ===')
for k in (4, 6, 8):
    t = ln * (k / 12.0)
    h = comp(HAND, t)
    hd = comp('head', t).translation
    world = unreal.MathLibrary.compose_transforms(offset, h)
    c = unreal.MathLibrary.transform_location(world, unreal.Vector(b.origin.x, b.origin.y, b.origin.z))
    d2 = math.sqrt((c.x - hd.x) ** 2 + (c.y - hd.y) ** 2 + (c.z - hd.z) ** 2)
    L('H   t=%5.2f  폰 중심(%6.1f %6.1f %6.1f) · 머리(%6.1f %6.1f %6.1f) · 거리 %.1fcm'
      % (t, c.x, c.y, c.z, hd.x, hd.y, hd.z, d2))
