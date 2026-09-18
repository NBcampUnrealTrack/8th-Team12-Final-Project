"""버거집에서 **폰과 NPC 이동속도를 350 으로** 고정한다.

    에디터를 **끄고** 돌린다.

## 두 쪽을 다르게 다룬다

| | 어디에 | 왜 |
|---|---|---|
| 플레이어 폰 | `AFebriculaCharacter::LevelWalkSpeedOverrides` (C++ 기본값) | 다른 레벨에서는 **500 을 유지**해야 한다 |
| 손님 `BP_Customer` | CDO 의 `MaxWalkSpeed` | 버거집에만 있는 액터라 레벨 분기가 필요 없다 |

⚠ 플레이어 쪽은 이 스크립트가 아니라 **C++ 기본값**이 정한다. 여기서는 확인만
  한다 — 값을 두 군데 두면 어긋난다.

## 가감속은 손님 쪽 비율(속도 ×4 / ×6)을 유지한다

§9 가 정한 관계다 (100→400/600, 183→732/1098, 500→2000/3000). 350 이면
**1400 / 2100**.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

CUST = '/Game/BurgerHouse/AI/BP_Customer'
PLAYER = '/Game/MetaHumans/MHC_Character_Adam/Cheon/BP_CHeon'
LEVEL = 'L_BurgerHouse'
TARGET = 350.0
ANIM_SPEED = 183.0

# ---- 플레이어 쪽은 **확인만** ----
L('=== 플레이어 (C++ 기본값이 정한다) ===')
pc = unreal.get_default_object(unreal.load_object(None, '%s.BP_CHeon_C' % PLAYER))
over = pc.get_editor_property('level_walk_speed_overrides')
L('  WalkSpeed(기본)          %.1f' % pc.get_editor_property('walk_speed'))
L('  LevelWalkSpeedOverrides  %s' % {str(k): over[k] for k in over.keys()})
L('  SprintBlockedLevels      %s'
  % [str(x) for x in pc.get_editor_property('sprint_blocked_levels')])

got = over.get(unreal.Name(LEVEL))
if got is None or abs(got - TARGET) > 0.01:
    E('  ⚠ %s 오버라이드가 %s 다 (기대 %.1f) — C++ 빌드를 볼 것' % (LEVEL, got, TARGET))
else:
    L('  -> %s 에서 플레이어는 %.1f 로 걷는다' % (LEVEL, got))

# ---- 손님 ----
L('')
L('=== 손님 BP_Customer ===')
bp = EAL.load_asset(CUST)
cdo = unreal.get_default_object(bp.generated_class())
mv = cdo.get_editor_property('character_movement')

WANT = {
    'max_walk_speed': TARGET,
    'max_acceleration': TARGET * 4.0,
    'braking_deceleration_walking': TARGET * 6.0,
}
for p, v in WANT.items():
    before = mv.get_editor_property(p)
    mv.set_editor_property(p, v)
    L('  %-30s %8.1f -> %8.1f' % (p, before, mv.get_editor_property(p)))

# ⚠ 저장이 **컴파일보다 먼저**다 (§6).
ok = unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
L('  저장(컴파일 전) -> %s' % ok)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

L('')
L('=== 이 값이 뜻하는 것 ===')
speed = mv.get_editor_property('max_walk_speed')
accel = mv.get_editor_property('max_acceleration')
L('  최고 속도까지 %.2fs · %.0fcm' % (speed / accel, speed ** 2 / (2 * accel)))
L('  애님 대비 %.2f배 어긋남 (애님 %.1f / 캡슐 %.1f)' % (speed / ANIM_SPEED, ANIM_SPEED, speed))
L('    500 일 때 2.73배 · 915 일 때 5.00배였다 — **가장 가까워졌다**')
L('    완전히 맞추려면 AM_Loco_Walk 재생 속도 %.2f (안 건드렸다)' % (speed / ANIM_SPEED))

# ⚠ Locomotion 전이 임계값 아래로 내려가면 Idle 포즈로 미끄러진다 (§9)
loco = cdo.get_editor_property('loco_walk_speed')
L('')
L('  Loco 전이 임계값 %.1f — %.1f 는 여유롭게 넘는다' % (loco, speed))
