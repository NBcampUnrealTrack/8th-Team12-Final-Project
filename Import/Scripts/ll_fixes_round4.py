"""라운드 4 — 순간이동의 **진짜 원인** · 트레이 위치 반영 · 영수증 세로 3배.

    에디터를 **끄고** 돌린다.

## ⚠⚠ 「자리로 순간이동한다」의 진짜 원인은 둘이었다

도착 판정(§9-9)도 좌석 보간(§9-11)도 **증상 완화**였다. 헤드리스 PIE 를 돌려
`LogStateTree Verbose` 로 전이를 찍고서야 보였다.

### 원인 A — 알림음 태스크가 `Pickup` 상태를 통째로 끝냈다 (C++ 에서 고침)

```
Enter  Root/Pickup ; P_Move
       Task 'Claim Slot'.EnterState()
       Task 'Play Sound At Tagged Actor'.EnterState()   <- Succeeded 를 돌려줬다
       Task '이동'.EnterState()
       State Completed Succeeded                         <- **같은 프레임**
       Transition  Pickup -> Seat
```

`Root/Pickup` 의 완료 정책이 `Any` 라, 내가 §9-3 에서 붙인 연출용 태스크가
`Succeeded` 를 돌려주는 순간 **픽업 갈래 전체**(`P_Move`·`P_Act`·`P_Take`)가
건너뛰어졌다. 손님은 픽업대로 걸어가지도, 트레이를 받지도 않고 카운터에서
좌석으로 **4.5m** 를 날아갔다.

⚠⚠ 에러가 없다. 손님은 멀쩡히 앉고 먹고 나간다 — **중간이 빠졌을 뿐**이다.

### 원인 B — `S_Move` 가 즉시 `Failed`

```
Enter  Root/Seat ; S_Move
       Task '이동'.EnterState()
       State Completed **Failed**      <- 같은 프레임
       Transition  S_Move -> S_Eat
```

좌석의 사용 지점은 **의자 위**다 (§9.2 「좌석 지점 ≠ 의자 원점」). 의자는
내비메시 구멍이라 그 좌표는 내비 **밖**이고, `bRequireNavigableEndLocation`
이 켜져 있으면 `MoveTo` 는 **경로를 만들기도 전에 실패**한다.

⚠ `S_Move → S_Eat` 전이가 성공/실패를 안 가려서, 실패해도 그냥 앉았다.

고치는 법: 좌석 이동만 `bRequireNavigableEndLocation = false`. 그러면 내비가
닿는 데까지 걸어가고(`bAllowPartialPath` 는 이미 켜져 있다), **남은 몇 cm 만**
`SitAt` 의 보간이 메운다.

⚠ 키오스크·픽업·대기열은 사용 지점이 바닥 위라 내비 안이다 — 그래서 그쪽만
  멀쩡했고, 좌석만 유독 순간이동했다.
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

# ───────────────────────── 원인 B — 좌석 이동
L('=== B) S_Move — 내비 밖 목적지를 허용한다 ===')
for st in ('Root/Seat/S_Move',):
    r = S.set_task_property_value(ST, st, 'StateTreeMoveToTask',
                                  'bRequireNavigableEndLocation', 'false')
    L('  %-22s bRequireNavigableEndLocation = false -> %s' % (st.split('/')[-1], r))
    if not r:
        fails.append(st)
L('  ⚠ 키오스크·픽업·대기열은 **건드리지 않는다** — 그쪽 목적지는 내비 안이고,')
L('    끄면 도달 못 하는 자리로도 걸어가려 해서 손님이 벽에 붙는다.')

pkg = EAL.load_asset(ST).get_outermost()
L('  [컴파일 전] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
L('  compile -> %s' % S.compile_state_tree(ST).get_editor_property('success'))
L('  [컴파일 후] save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

# ───────────────────────── 영수증 세로 3배 + 재배치
L('')
L('=== 영수증 세로 3배 ===')
# (컴포넌트, 메시, 스케일, 트레이 위 중심 XY)
#
# ⚠ 영수증이 4 x 5 -> 4 x **15** 가 되면서 셋을 Y 한 줄로 못 세운다
#   (19.6 + 7.0 + 15.0 = 41.6 > 트레이 Y 40). 감자와 영수증을 **X 로 나란히**
#   놓는다.
LAYOUT = [
    ('burger_box', '/Game/BurgerHouse/Props/Food/SM_BurgerBox',
     unreal.Vector(7.5, 7.5, 7.5), (0.0, -10.0)),
    ('fries', '/Game/BurgerHouse/Props/Food/SM_Fries',
     unreal.Vector(0.6, 0.6, 0.6), (-5.0, 7.0)),
    ('receipt', '/Engine/BasicShapes/Plane',
     unreal.Vector(0.04, 0.15, 1.0), (9.5, 8.0)),      # Y 0.05 -> 0.15 (3배)
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
    L('  --- %s' % bp_path.split('/')[-1])
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
        L('    %-11s %.1f x %.1f  중심(%.1f, %.1f)  X[%.1f..%.1f] Y[%.1f..%.1f] %s'
          % (prop, sx, sy, xc, yc, xc - sx / 2, xc + sx / 2, yc - sy / 2, yc + sy / 2,
             ('트레이 밖! ' + ''.join(over)) if over else ''))
        if over:
            fails.append('%s.%s 트레이 밖' % (bp_path.split('/')[-1], prop))
    unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

# ⚠ 서로 겹치면 한쪽이 다른 쪽을 뚫고 나온다 — 눈으로 보기 전에 여기서 잡는다.
L('')
L('  겹침 검사 (XY 사각형)')
for i in range(len(spans)):
    for j in range(i + 1, len(spans)):
        a, b = spans[i], spans[j]
        if a[1] < b[2] and b[1] < a[2] and a[3] < b[4] and b[3] < a[4]:
            L('    %s 와 %s 가 겹친다' % (a[0], b[0]))
            fails.append('%s/%s 겹침' % (a[0], b[0]))
        else:
            L('    %-11s / %-11s 안 겹침' % (a[0], b[0]))

# ───────────────────────── 카운터 트레이 — 사용자가 옮긴 자리를 **기준으로 삼는다**
L('')
L('=== 카운터 트레이 — 에디터에서 옮긴 자리 확인 ===')
# ⚠⚠ 여기서 **위치를 쓰지 않는다.** 사용자가 에디터에서 옮긴 값이 정답이고,
#    스크립트가 다시 계산해 덮으면 그 손질이 매번 날아간다. 읽어서 찍기만 한다.
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
cls = unreal.load_object(None, '/Game/BurgerHouse/AI/BP_CounterTray.BP_CounterTray_C')
for a in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
    l = a.get_actor_location()
    r = a.get_actor_rotation()
    L('  %s  loc(%.0f, %.0f, %.0f)  yaw %.0f  — 이 값이 기준이다 (스크립트가 안 건드린다)'
      % (a.get_actor_label(), l.x, l.y, l.z, r.yaw))

L('')
if fails:
    E('=== 실패 %d건: %s ===' % (len(fails), fails))
else:
    L('=== 적용 (검산은 새 프로세스 + PIE 로) ===')
