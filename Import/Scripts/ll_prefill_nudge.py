"""3인 일행 중 **벽에 붙은** 프리필 손님을 −Y 로 30cm 옮긴다.

    에디터를 **끄고** 돌린다. 끝나면 `ll_table_tray.py` 를 **다시** 돌릴 것.

## 어느 것이 벽에 붙었나 — 숫자로 고른다

```
[0] party=6  (130.5, −717.1)   벽까지 X 141
[1] party=6  ( 24.0, −704.9)   벽까지 X  34   <- 이것
[2] party=6  (126.3, −764.1)   벽까지 X 136
[3] party=4  (343.7, −419.6)
[4] party=4  (343.5, −516.1)
```

`party_index` 6 이 3명짜리 일행이고, 그중 `[1]` 만 벽(내비 경계 X −10)에서 34cm 다.
나머지 둘은 136~141cm 다.

⚠ 「벽에 가깝다」를 눈으로 고르지 않고 **내비 경계까지의 거리**로 고른다. 레벨이
  바뀌면 다시 계산되도록.

## ⚠⚠ 옮기면 **앉는 의자가 바뀔 수 있다**

프리필은 `ClaimNearestSlot` 으로 **가장 가까운** 좌석을 잡는다 (`ApplyStartRole`).
지금 `[1]` 은 `SLOT_Seat_33` 에서 18cm 인데, 30cm 를 옮기면 다른 의자가 더
가까워질 수 있다. 그러면 손님이 엉뚱한 자리에 앉는다.

그래서 옮긴 **뒤에 최근접 좌석을 다시 찍고**, 바뀌면 크게 경고한다.

## ⚠ 공용 트레이 짝도 다시 잡아야 한다

`[0]`·`[1]` 이 마주보는 짝이라 `TableTray_0` 이 그 중점에 놓여 있다 (§9-23).
`[1]` 이 움직이면 중점도 움직인다 — `ll_table_tray.py` 를 다시 돌릴 것.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
NUDGE = unreal.Vector(0.0, -30.0, 0.0)

# 내비 경계 (로그 실측) — 「벽까지 거리」의 기준
NAV_MIN_X, NAV_MAX_X = -10.0, 610.0
NAV_MIN_Y, NAV_MAX_Y = -1140.0, 0.0

fails = []

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

seats = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
         if str(s.get_editor_property('kind')) == 'Seat']


def nearest_seat(p):
    best, bd = None, 1e9
    for s in seats:
        d = (s.get_actor_location() - p).length()
        if d < bd:
            best, bd = s, d
    return best, bd


spawner = None
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    spawner = sp
    break
if not spawner:
    E('N  스포너가 없다')
    raise SystemExit

# ⚠ 배열 원소는 **복사본**으로 돌아온다. 고친 뒤 배열을 통째로 되넣어야 반영된다.
places = list(spawner.get_editor_property('prefill_placements'))

L('N === 일행별 인원 ===')
by_party = {}
for e in places:
    by_party.setdefault(int(e.get_editor_property('party_index')), []).append(e)
for k in sorted(by_party):
    L('N   party %-3d %d명' % (k, len(by_party[k])))

targets = [k for k, v in by_party.items() if len(v) == 3]
if not targets:
    E('N   3명짜리 일행이 없다')
    raise SystemExit
L('N   3명짜리 일행: %s' % targets)

L('N')
L('N === 벽에서 가장 가까운 사람 고르기 ===')
pick = None
for k in targets:
    scored = []
    for e in by_party[k]:
        t = e.get_editor_property('transform').translation
        wall = min(t.x - NAV_MIN_X, NAV_MAX_X - t.x, t.y - NAV_MIN_Y, NAV_MAX_Y - t.y)
        scored.append((wall, e))
        L('N   party %-3d (%7.1f %8.1f)  벽까지 %5.0f' % (k, t.x, t.y, wall))
    scored.sort(key=lambda a: a[0])
    pick = scored[0][1]
    # ⚠ 1등과 2등이 비슷하면 「벽에 붙은 하나」라는 전제가 안 선다
    if len(scored) > 1 and (scored[1][0] - scored[0][0]) < 40.0:
        E('N   ⚠ 1등(%.0f)과 2등(%.0f)이 비슷하다 — 어느 것인지 애매하다'
          % (scored[0][0], scored[1][0]))
        fails.append('벽 판정 애매')
    L('N   -> 고른 것: (%7.1f %8.1f) 벽까지 %.0f'
      % (pick.get_editor_property('transform').translation.x,
         pick.get_editor_property('transform').translation.y, scored[0][0]))

L('N')
L('N === 옮기기 ===')
t = pick.get_editor_property('transform')
before = t.translation
s_before, d_before = nearest_seat(before)
after = unreal.Vector(before.x + NUDGE.x, before.y + NUDGE.y, before.z + NUDGE.z)
s_after, d_after = nearest_seat(after)

L('N   전  (%7.1f %8.1f)  최근접 %-16s %.0fcm' % (before.x, before.y, s_before.get_actor_label(), d_before))
L('N   후  (%7.1f %8.1f)  최근접 %-16s %.0fcm' % (after.x, after.y, s_after.get_actor_label(), d_after))

if s_before.get_actor_label() != s_after.get_actor_label():
    E('N   ⚠⚠ **앉는 의자가 바뀐다** (%s -> %s). `ClaimNearestSlot` 이 최근접을 잡으므로 '
      '손님이 엉뚱한 자리에 앉는다' % (s_before.get_actor_label(), s_after.get_actor_label()))
    fails.append('좌석 바뀜')
else:
    L('N   좌석 그대로 — OK')

# ⚠ 내비 밖으로 나가면 그 손님은 이후 이동이 전부 실패한다
nav = unreal.NavigationSystemV1.get_navigation_system(world)
L('N   옮긴 자리 내비 안? (경계 X %.0f~%.0f · Y %.0f~%.0f) -> %s'
  % (NAV_MIN_X, NAV_MAX_X, NAV_MIN_Y, NAV_MAX_Y,
     (NAV_MIN_X <= after.x <= NAV_MAX_X and NAV_MIN_Y <= after.y <= NAV_MAX_Y)))

t.translation = after
pick.set_editor_property('transform', t)

# ⚠ 배열을 통째로 되넣는다 (위 주석)
spawner.set_editor_property('prefill_placements', places)

# 되읽기
L('N')
L('N === 되읽기 ===')
for i, e in enumerate(spawner.get_editor_property('prefill_placements')):
    tr = e.get_editor_property('transform').translation
    ns, nd = nearest_seat(tr)
    L('N   [%d] party=%s (%7.1f %8.1f)  최근접 %-16s %.0fcm'
      % (i, e.get_editor_property('party_index'), tr.x, tr.y, ns.get_actor_label(), nd))

L('N   레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))

L('N')
if fails:
    E('N === 확인 필요: %s ===' % fails)
else:
    L('N === 완료 — 이어서 `ll_table_tray.py` 를 다시 돌릴 것 ===')
