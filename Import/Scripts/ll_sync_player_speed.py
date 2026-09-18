"""손님 이동속도를 **플레이어 걷기 속도**에 맞춘다.

    에디터를 **끄고** 돌린다.

## 기준 — 플레이어는 500

```
BP_ThirdPersonGameMode > DefaultPawnClass = BP_CHeon
  WalkSpeed    500     ← 이 값에 맞춘다
  SprintSpeed 1000
  MaxWalkSpeed 500 · MaxAcceleration 2048 · Braking 2000
```

⚠ BP 가 C++ 기본값(`AFebriculaCharacter`)을 안 덮고 있다 — 둘 다 500/1000 이다.

⚠ **스프린트(1000)가 아니라 걷기(500)에 맞춘다.** 손님이 플레이어의 전력질주를
  따라갈 이유가 없다.

## 가감속은 손님 쪽 비율을 유지한다

§9 가 정한 관계가 `가속 = 속도 × 4` · `제동 = 속도 × 6` 이다 (100→400/600,
183→732/1098). 500 이면 **2000 / 3000**.

⚠ 플레이어 값(2048 / 2000)과 비슷하지만 **같게 맞추지 않았다.** 손님은 좁은
  실내에서 여럿이 움직여서 §9 가 일부러 제동을 세게 잡아 뒀다 — 「급출발·급정지가
  앞뒤 흔들림으로 보인다」.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/AI/BP_Customer'
PLAYER = '/Game/MetaHumans/MHC_Character_Adam/Cheon/BP_CHeon'
ANIM_SPEED = 183.0

# ---- 기준값을 코드에 박지 않고 **플레이어에서 읽는다** ----
#
# ⚠ 박아 두면 플레이어 속도를 바꿨을 때 조용히 어긋난다. 「동기화」가 목적이므로
#   원본을 읽어 쓰는 것이 맞다.
pc = unreal.get_default_object(unreal.load_object(None, '%s.BP_CHeon_C' % PLAYER))
target = pc.get_editor_property('walk_speed')
sprint = pc.get_editor_property('sprint_speed')
L('=== 기준 (플레이어) ===')
L('  BP_CHeon  WalkSpeed %.1f · SprintSpeed %.1f' % (target, sprint))

L('')
L('=== BP_Customer 맞추기 ===')
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
mv = cdo.get_editor_property('character_movement')

WANT = {
    'max_walk_speed': target,
    'max_acceleration': target * 4.0,
    'braking_deceleration_walking': target * 6.0,
}
for p, v in WANT.items():
    before = mv.get_editor_property(p)
    mv.set_editor_property(p, v)
    L('  %-30s %8.1f -> %8.1f' % (p, before, mv.get_editor_property(p)))

# ⚠ 저장이 **컴파일보다 먼저**다 (§6).
ok = unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
L('  저장(컴파일 전) -> %s' % ok)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

speed = mv.get_editor_property('max_walk_speed')
accel = mv.get_editor_property('max_acceleration')
L('')
L('=== 이 값이 뜻하는 것 ===')
L('  최고 속도까지 %.2fs · %.0fcm' % (speed / accel, speed ** 2 / (2 * accel)))
L('  애님 대비 %.2f배 어긋남 (애님 %.1f / 캡슐 %.1f)' % (speed / ANIM_SPEED, ANIM_SPEED, speed))
L('    -> 915 일 때의 5.00배보다는 낫지만 **여전히 미끄러진다**')
L('    -> 맞추려면 AM_Loco_Walk 재생 속도를 %.2f 로 (안 건드렸다)' % (speed / ANIM_SPEED))
