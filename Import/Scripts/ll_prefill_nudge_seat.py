"""벽에 붙은 프리필 손님이 **실제로 앉는 자리**도 같이 −Y 로 30cm 옮긴다.

    에디터를 **끄고** 돌린다. `ll_prefill_nudge.py` 다음. 끝나면
    `ll_table_tray.py` 를 다시 돌릴 것.

## ⚠⚠ 스폰 지점을 옮겨도 **앉는 자리는 안 바뀐다**

`ACustomerCharacter::ApplyStartRole` 의 `Eating` 갈래는 이렇게 돈다 —

```cpp
ASlotPoint* Seat = Store->ClaimNearestSlot(this, Seat);   // 스폰 지점에서 **가장 가까운** 의자
SitAt(Seat->GetUseTransform());                           // 그 의자 자리로 앉힌다
```

즉 `PrefillPlacements` 의 좌표는 **어느 의자를 고를지 정하는 값**일 뿐이고, 실제
위치는 `ASlotPoint` 가 정한다. 스폰만 30cm 옮기면 태어나자마자 원래 자리로
0.5초 미끄러져 돌아간다 (`SeatBlendTime`).

그래서 **좌석 슬롯도 같은 만큼 옮긴다.**

## 안전한가 — 소파 위를 벗어나지 않는다

```
Sofa_Unit           48 × 103.8
소파 #4             (24, −722.9)   Y 범위 −774.8 ~ −671.0
SLOT_Seat_33 (전)   (24, −722.9)   소파 한가운데
SLOT_Seat_33 (후)   (24, −752.9)   여전히 소파 안 (아래 여유 22cm)
```

⚠ 옆 소파 #5 는 (24, −827.6) 이라 75cm 떨어져 있다 — 겹치지 않는다.

⚠ 이 슬롯은 **긴 소파 위**라 Y 로 미는 것이 자연스럽다. 등받이 의자였다면
  의자 밖으로 나가므로 같은 수를 쓰면 안 된다.

## ⚠ 한 번만 돌리는 스크립트다

상대 이동이라 두 번 돌리면 60cm 가 된다. 시작 위치를 **확인하고** 아니면 멈춘다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

MAP = '/Game/BurgerHouse/Maps/L_BurgerHouse'
SEAT = 'SLOT_Seat_33'
EXPECT_BEFORE = unreal.Vector(24.0, -722.9, 0.0)
NUDGE = unreal.Vector(0.0, -30.0, 0.0)

# 소파 #4 의 Y 범위 (실측) — 벗어나면 멈춘다
SOFA_Y_MIN, SOFA_Y_MAX = -774.8, -671.0

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

target = None
for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint):
    if s.get_actor_label() == SEAT:
        target = s
        break
if not target:
    E('S  %s 가 없다' % SEAT)
    raise SystemExit

now = target.get_actor_location()
L('S === %s ===' % SEAT)
L('S   현재 (%.1f %.1f %.1f)' % (now.x, now.y, now.z))

# ⚠ 한 번만 — 이미 옮겼으면 멈춘다
if abs(now.y - EXPECT_BEFORE.y) > 1.0:
    E('S   시작 위치가 기대(%.1f)와 다르다. 이미 옮겼거나 누가 손댔다 — 멈춘다' % EXPECT_BEFORE.y)
    raise SystemExit

after = unreal.Vector(now.x + NUDGE.x, now.y + NUDGE.y, now.z + NUDGE.z)
if not (SOFA_Y_MIN <= after.y <= SOFA_Y_MAX):
    E('S   옮긴 자리 %.1f 가 소파 범위 %.1f~%.1f 밖이다 — 멈춘다'
      % (after.y, SOFA_Y_MIN, SOFA_Y_MAX))
    raise SystemExit

target.set_actor_location(after, False, False)
u = target.get_use_transform()
L('S   -> (%.1f %.1f %.1f)   앉는 지점 use (%.1f %.1f) yaw %.0f'
  % (after.x, after.y, after.z, u.translation.x, u.translation.y, u.rotation.rotator().yaw))
L('S   소파 여유  위 %.1fcm · 아래 %.1fcm' % (after.y - SOFA_Y_MIN, SOFA_Y_MAX - after.y))

# ⚠ 스폰 지점이 여전히 **이 의자를 최근접으로** 고르는지 확인한다. 아니면 손님이
#   엉뚱한 자리에 앉는다.
L('S')
L('S === 스폰 지점이 아직 이 의자를 고르나 ===')
seats = [s for s in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SlotPoint)
         if str(s.get_editor_property('kind')) == 'Seat']
for sp in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CustomerSpawner):
    for i, e in enumerate(sp.get_editor_property('prefill_placements')):
        t = e.get_editor_property('transform').translation
        best, bd = None, 1e9
        for s in seats:
            d = (s.get_actor_location() - t).length()
            if d < bd:
                best, bd = s, d
        L('S   [%d] (%7.1f %8.1f) -> %-16s %.0fcm' % (i, t.x, t.y, best.get_actor_label(), bd))

L('S   레벨 저장 -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], only_dirty=False))
L('S')
L('S ⚠ 이어서 `ll_table_tray.py` 를 다시 돌릴 것 — 공용 트레이 중점이 움직였다.')
