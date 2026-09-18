"""라운드 3 — 알림음·트레이 위치·음식 크기·바라보는 방향·Telling_A_Secret 제거.

    에디터를 **끄고** 돌린다.

| | |
|---|---|
| ① | `OrderChime` 볼륨 0.05 → **0.02** |
| ② | 카운터 트레이를 카운터 쪽(−Y)으로 **30** 더 |
| ③ | 버거박스·감자 **×1.5**, 영수증 **×0.5** (바닥은 상판에 계속 붙는다) |
| ④ | 키오스크·픽업·대기열에서 **바라보는 방향** |
| ⑤ | `Telling_A_Secret` 을 프로젝트에서 **완전히 제거** |

## ⚠⚠ ④ — 애니메이션 문제가 **아니었다**

「주문할 때도 대기할 때도 출입문을 본다」의 원인은 둘이고, **둘 다 좌표 설정**이다.

### 키오스크 — 슬롯 yaw 가 **가구의 yaw 그대로**였다

```
키오스크 가구    (570, −275)  yaw 60     <- 블렌더 rot_z −60 을 y반전한 값
슬롯 LocalOffset (0, 60, 0) 로컬        -> 손님은 (518, −245) 에 선다
슬롯 UseYawOffset 0                     -> 손님도 yaw 60 을 본다
```

손님이 선 자리에서 키오스크까지의 방향은 `atan2(−275+245, 570−518)` = **−30°**
인데 **60°** 를 보고 있었다. 정확히 **90도** 어긋난다 — 그리고 60° 는 +Y 쪽,
즉 출입문(420, −55)이 있는 쪽이다.

⚠ `UseYawOffset` 이 바로 이것을 위한 손잡이다 (`ASlotPoint` 주석: 「가구 정면이
  캐릭터 정면(+X)과 다르면 yaw 를 더 돌려야 한다」). 아무도 안 채워 넣었을 뿐이다.

⚠ 오프셋이 **로컬 +Y** 니까 바라볼 방향은 **로컬 −Y** — 그래서 정확히 −90 이다.
  눈대중이 아니라 계산으로 나오는 값이다.

### 대기열 — 방향을 **아무도 안 잡아 줬다**

`W_Idle` 의 `FaceSlotKind` 가 비어 있었다. 대기 자리는 `ASlotPoint` 가 아니라
`QueueStart + QueueStep*index` 로 **계산**되는 좌표라 슬롯으로는 못 잡는다.

그래서 손님은 `bOrientRotationToMovement` 가 남긴 **마지막 진행 방향** 그대로 섰다.

```
QueueStart (220, −960)   QueueStep (0, +70)   -> 줄은 +Y, 즉 문 쪽으로 뻗는다
픽업 카운터 y −1110                           -> 봐야 하는 쪽은 −Y = yaw −90
```

C++ 에 `bFaceWorldYaw`/`FaceWorldYaw` 를 새로 만들어 절대 각도를 준다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/AI/ST_Customer'
MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
TRAY_TOP = 2.2

fails = []


def sett(state, struct, prop, val):
    r = S.set_task_property_value(ST, state, struct, prop, val)
    if not r:
        fails.append('%s : %s' % (state, prop))
    return r


# ───────────────────────── ① 알림음
L('=== 1) OrderChime 0.05 -> 0.02 ===')
L('  %s' % sett('Root/Kiosk/K_Order', 'BurgerHousePlaySoundTask', 'VolumeMultiplier', '0.02'))

# ───────────────────────── ④ 바라보는 방향 (트리 쪽)
L('')
L('=== 4) 바라보는 방향 — 트리 ===')
# 키오스크: 워크인(K_Act)은 이미 FaceSlotKind=Kiosk 다. 프리필(PO_Act)은 비어 있었다.
L('  PO_Act  FaceSlotKind=Kiosk   %s'
  % sett('Root/P_Ordering/PO_Kiosk/PO_Act', 'BurgerHouseActionTask', 'FaceSlotKind', 'Kiosk'))
# 픽업: 손님이 Pickup 슬롯을 잡고 있고 그 슬롯 yaw(−90)가 이미 카운터를 본다.
L('  P_Act   FaceSlotKind=Pickup  %s'
  % sett('Root/Pickup/P_Act', 'BurgerHouseActionTask', 'FaceSlotKind', 'Pickup'))
# 대기열: 슬롯이 없으므로 절대 각도.
for st in ('Root/Wait/W_Idle', 'Root/P_Waiting/PW_Wait/PW_Idle'):
    ok1 = sett(st, 'BurgerHouseActionTask', 'bFaceWorldYaw', 'true')
    ok2 = sett(st, 'BurgerHouseActionTask', 'FaceWorldYaw', '-90.0')
    L('  %-10s WorldYaw −90 (카운터 쪽)  %s %s' % (st.split('/')[-1], ok1, ok2))

pkg = EAL.load_asset(ST).get_outermost()
L('  [컴파일 전] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
L('  compile -> %s' % S.compile_state_tree(ST))
L('  [컴파일 후] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

# ───────────────────────── ③ 음식 크기
L('')
L('=== 3) 음식 크기 — 버거박스·감자 x1.5 · 영수증 x0.5 ===')
LAYOUT = [
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox',
     unreal.Vector(7.5, 7.5, 7.5), (0.0, -9.0)),           # 5.0 x 1.5
    ('fries', '/Game/BurgerHouse/Props/Food/SM_Fries',
     unreal.Vector(0.6, 0.6, 0.6), (0.0, 6.0)),            # 0.4 x 1.5
    ('receipt', '/Engine/BasicShapes/Plane',
     unreal.Vector(0.04, 0.05, 1.0), (0.0, 15.0)),         # (0.08,0.10) x 0.5
]


def place(mesh_path, scale, xc, yc):
    """bbox 바닥을 상판에 붙이고 중심을 (xc,yc) 에 두는 상대 위치 (§9-8 의 식)."""
    b = EAL.load_asset(mesh_path).get_bounds()
    o, e = b.origin, b.box_extent
    return unreal.Vector(xc - o.x * scale.x,
                         yc - o.y * scale.y,
                         TRAY_TOP + 0.05 + (e.z - o.z) * scale.z)


for bp_path in ('/Game/BurgerHouse/AI/BP_Customer', '/Game/BurgerHouse/AI/BP_CounterTray'):
    bp = EAL.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    L('  --- %s' % bp_path.split('/')[-1])
    for prop, mesh, scale, center in LAYOUT:
        xc, yc = center
        c = cdo.get_editor_property(prop)
        loc = place(mesh, scale, xc, yc)
        c.set_editor_property('relative_location', loc)
        c.set_editor_property('relative_scale3d', scale)
        b = EAL.load_asset(mesh).get_bounds()
        sx = b.box_extent.x * 2 * scale.x
        sy = b.box_extent.y * 2 * scale.y
        # ⚠ 트레이는 X 30 x Y 40. 넘치면 음식이 허공에 뜬다 — 소리 내서 잡는다.
        over = []
        if abs(xc) + sx / 2 > 15.0:
            over.append('X %.1f' % (abs(xc) + sx / 2))
        if abs(yc) + sy / 2 > 20.0:
            over.append('Y %.1f' % (abs(yc) + sy / 2))
        L('    %-11s x%.2f -> %.1f x %.1f x %.1f  중심(%.0f,%.0f) %s'
          % (prop, scale.x, sx, sy, b.box_extent.z * 2 * scale.z, xc, yc,
             ('트레이 밖! ' + ' '.join(over)) if over else ''))
        if over:
            fails.append('%s.%s 트레이 밖' % (bp_path.split('/')[-1], prop))

    # ───────────── ⑤ Telling_A_Secret 을 Idle_Look 에서 뺀다 (BP_Customer 만)
    if bp_path.endswith('BP_Customer'):
        acts = cdo.get_editor_property('actions')
        act = acts['Idle_Look']
        keep = [v for v in act.get_editor_property('variants')
                if v.get_editor_property('montage')
                and 'Telling_A_Secret' not in v.get_editor_property('montage').get_name()]
        act.set_editor_property('variants', keep)
        acts['Idle_Look'] = act
        cdo.set_editor_property('actions', acts)
        L('    Idle_Look 변형 -> %s'
          % [v.get_editor_property('montage').get_name() for v in keep])

    # ⚠ **저장이 컴파일보다 먼저** (§6)
    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ───────────────────────── ⑤ 에셋 삭제
L('')
L('=== 5) Telling_A_Secret 삭제 ===')
for p in ('/Game/BurgerHouse/Anims/Montages/AM_Telling_A_Secret',
          '/Game/BurgerHouse/Anims/Mixamo/Anims/Telling_A_Secret'):
    if not EAL.does_asset_exist(p):
        L('  %-52s 이미 없다' % p)
        continue
    # ⚠ 참조가 남아 있으면 삭제해도 **다시 로드된다.** 먼저 누가 쓰는지 찍는다.
    refs = EAL.find_package_referencers_for_asset(p, False) or []
    L('  %-52s 참조 %s' % (p, [str(r).split('/')[-1] for r in refs] or '없음'))
    L('     delete -> %s' % EAL.delete_asset(p))

# ───────────────────────── ② 카운터 트레이 · ④ 슬롯 yaw (레벨)
L('')
L('=== 2)4) 레벨 ===')
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

cls = unreal.load_object(None, '/Game/BurgerHouse/AI/BP_CounterTray.BP_CounterTray_C')
for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
    l = a.get_actor_location()
    # 카운터는 −Y 쪽이다 (픽업 슬롯 y −1040, 카운터 조명 y −1142)
    a.set_actor_location(unreal.Vector(l.x, l.y - 30.0, l.z), False, False)
    L('  CounterTray  y %.0f -> %.0f (카운터 쪽 −Y 로 30)' % (l.y, a.get_actor_location().y))

for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint):
    if str(s.get_editor_property('kind')) != 'Kiosk':
        continue
    before = s.get_use_transform().rotation.rotator().yaw
    # ⚠ 손님은 슬롯 **로컬 +Y** 에 서므로 바라볼 방향은 로컬 −Y = yaw −90.
    s.set_editor_property('use_yaw_offset', -90.0)
    after = s.get_use_transform().rotation.rotator().yaw
    L('  %-16s 바라보는 yaw %.1f -> %.1f' % (s.get_actor_label(), before, after))

L('  레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))

L('')
if fails:
    E('=== 실패 %d건: %s ===' % (len(fails), fails))
else:
    L('=== 전부 적용 (검산은 새 프로세스로) ===')
