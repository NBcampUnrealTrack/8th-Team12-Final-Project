"""트레이 위 음식 배치를 **실측 바운즈로** 다시 잡는다 + 카운터 트레이 회전 수정.

    에디터를 **끄고** 돌린다.

## ⚠⚠ 「Fries만 보인다」의 원인 — 손으로 찍은 오프셋

원래 값은 내가 눈대중으로 찍은 것이었다. 실측하니 이렇다 —

| 메시 | 실제 크기(cm) | 바운즈 원점 | 함정 |
|---|---|---|---|
| `Tray` | 30 × 40 × **2.2** | (0,0,1.1) | 상판이 z=2.2 다 |
| `SM_BurgerBox` | **2.5 × 2.6 × 1.3** | (0,0,0) | 원점이 **한가운데** |
| `SM_Fries` | 24.5 × 11.6 × 24.0 | **(4.4,-15.1,12.3)** | 원점이 메시 밖 |
| `Plane`(영수증) | 100 × 100 × 0 | (0,0,0) | 두께 0 |

- **버거박스**: 원점이 한가운데인데 z=2.0 에 놨다. ×5 면 반높이가 3.25 이니
  **바닥이 z=-1.25** — 트레이(0~2.2)를 뚫고 아래로 나가고, 보이는 부분은
  트레이 벽에 가려 거의 안 남는다.
- **영수증**: z=1.2 인데 트레이 상단이 2.2 다. **트레이 안에 묻혔다.**
- **감자**만 원점이 메시 바깥(+z 12.3)이라 우연히 위로 떠서 혼자 보였다.

그래서 「감자만 스폰된 것 같다」로 보였다. 셋 다 스폰돼 있었고, 둘이 묻혀 있었다.

## 고치는 법 — 숫자를 찍지 말고 계산한다

바운딩박스로 역산한다. 회전이 없을 때 —

```
월드 bbox 중심 = loc + origin*scale
월드 bbox 바닥 = loc.z + (origin.z - extent.z)*scale
```

「바닥을 상판(z=2.2)에 놓고, 중심을 (Xc,Yc)에 둔다」를 풀면

```
loc.x = Xc - origin.x*scale
loc.y = Yc - origin.y*scale
loc.z = Zsurf - (origin.z - extent.z)*scale
```

⚠ 앞으로 트레이에 뭘 더 얹을 때도 **이 식으로** 뽑을 것. 눈대중 오프셋은
  메시 원점이 제각각이라 반드시 한 번은 묻힌다.

## ⚠ 카운터 트레이가 서 있었다

`unreal.Rotator(0, 90, 0)` 로 스폰했는데 파이썬 생성자는 **(roll, pitch, yaw)**
순서다. yaw 90 을 준 줄 알았으나 **pitch 90** 이 들어가 트레이가 옆으로 섰다.
읽어 보니 `roll=0 pitch=90 yaw=0` 이었다.
"""
import unreal

L = unreal.log_warning
EAL = unreal.EditorAssetLibrary

TRAY_TOP = 2.2          # Tray 메시 상단 (실측: 높이 2.2, 원점 z 1.1)
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'

# (컴포넌트, 메시, 스케일, 트레이 위 중심 XY)
#   트레이는 X 30 · Y 40 → Y(긴 축) 를 따라 세 가지를 늘어놓는다.
LAYOUT = [
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox',
     unreal.Vector(5.0, 5.0, 5.0),  (0.0, -10.0)),
    ('fries',      '/Game/BurgerHouse/Props/Food/SM_Fries',
     unreal.Vector(0.4, 0.4, 0.4),  (0.0,   3.0)),
    ('receipt',    '/Engine/BasicShapes/Plane',
     unreal.Vector(0.08, 0.10, 1.0), (0.0,  14.0)),
]


def place(mesh_path, scale, xc, yc):
    """bbox 바닥을 상판에 붙이고 중심을 (xc,yc) 에 두는 상대 위치를 푼다."""
    b = EAL.load_asset(mesh_path).get_bounds()
    o, e = b.origin, b.box_extent
    return unreal.Vector(
        xc - o.x * scale.x,
        yc - o.y * scale.y,
        # 두께 0 인 Plane 은 딱 붙으면 z-파이팅이 나니 0.05 띄운다
        TRAY_TOP + 0.05 + (e.z - o.z) * scale.z,
    )


L('=== 트레이 위 배치 재계산 ===')
L('  Tray 상판 z = %.1f (실측 높이 2.2)' % TRAY_TOP)

for bp_path in ('/Game/BurgerHouse/AI/BP_Customer', '/Game/BurgerHouse/AI/BP_CounterTray'):
    bp = EAL.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    L('  --- %s' % bp_path.split('/')[-1])
    for prop, mesh, scale, (xc, yc) in LAYOUT:
        c = cdo.get_editor_property(prop)
        if not c:
            continue
        loc = place(mesh, scale, xc, yc)
        c.set_editor_property('relative_location', loc)
        c.set_editor_property('relative_scale3d', scale)
        b = EAL.load_asset(mesh).get_bounds()
        L('    %-11s loc(%6.2f %6.2f %6.2f) scale %.2f -> 크기 %.1f x %.1f x %.1f · 바닥 z %.2f'
          % (prop, loc.x, loc.y, loc.z, scale.x,
             b.box_extent.x * 2 * scale.x, b.box_extent.y * 2 * scale.y,
             b.box_extent.z * 2 * scale.z,
             loc.z + (b.origin.z - b.box_extent.z) * scale.z))
    # ⚠ 저장이 컴파일보다 **먼저** (§6)
    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

L('')
L('=== 카운터 트레이 회전 — pitch 90 -> yaw 90 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cls = unreal.load_object(None, '/Game/BurgerHouse/AI/BP_CounterTray.BP_CounterTray_C')
for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
    before = a.get_actor_rotation()
    # ⚠ Rotator(roll, pitch, yaw). yaw 만 준다 —
    #   트레이 긴 축(Y 40)을 카운터 길이 방향(X)으로 눕힌다.
    a.set_actor_rotation(unreal.Rotator(0.0, 0.0, 90.0), False)
    after = a.get_actor_rotation()
    L('  %s  (%.0f %.0f %.0f) -> (%.0f %.0f %.0f)'
      % (a.get_actor_label(), before.roll, before.pitch, before.yaw,
         after.roll, after.pitch, after.yaw))

L('  레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))
