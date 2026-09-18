"""워크인 손님 ① 이동속도 5배 ② 키오스크에서 시계 15도 틀어 Play Action.

    에디터를 **끄고** 돌린다.

## ① 속도 5배

`MaxWalkSpeed` 183 → **915** (cm/s). 가감속도 같은 비율로 올린다 — 안 올리면
6×11m 실내에서 **최고 속도에 도달하지도 못하고** 문에 닿는다.

⚠⚠ **발이 미끄러진다.** 걷기 애님(`Walk_Fwd_Manny`)의 실측 보행 속도는
183cm/s 다. 캡슐만 5배로 가면 애님과 **5배** 어긋난다 — §9 가 「600 vs 97.5 =
6배로 달려 발이 미끄러지고 목표를 지나쳐 되돌아왔다」로 적어 둔 그 상태다.

→ 맞추려면 `AM_Loco_Walk` 의 재생 속도를 **5.0** 으로 올리면 된다. 발은 따라오지만
  빨리감기처럼 보인다. 어느 쪽이 나은지는 눈으로 정할 일이라 **안 건드렸다.**

## ② 키오스크에서 15도

`K_Act` 의 `Play Action` 에 `FaceSlotKind=Kiosk` · `FaceYawOffset=15` 를 넣는다.

⚠ `UseYawOffset`(슬롯 쪽)이 아니라 **태스크 쪽**에 넣는다. 슬롯 값은 도착 시
  아무도 안 읽기 때문이다 — `ClaimSlot` 이 `UseRotation` 을 내놓지만 트리가 그걸
  쓰지 않는다 (§9 「남은 부작용」). 태스크가 직접 돌린다.

⚠ **양수가 시계 방향**이다 (UE 는 X 정면 · Y 오른쪽).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

BP = '/Game/BurgerHouse/AI/BP_Customer'
ST = '/Game/BurgerHouse/AI/ST_Customer'
K_ACT = 'Root/Kiosk/K_Act'
ACTION_TASK = 'BurgerHouseActionTask'

ANIM_SPEED = 183.0          # Walk_Fwd_Manny 실측
FACTOR = 5.0

S = unreal.StateTreeService

# ---- ① 속도 ----
L('=== ① BP_Customer 이동 값 ===')
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
mv = cdo.get_editor_property('character_movement')

for p in ('max_walk_speed', 'max_acceleration', 'braking_deceleration_walking'):
    before = mv.get_editor_property(p)
    mv.set_editor_property(p, before * FACTOR)
    L('  %-30s %8.1f -> %8.1f' % (p, before, mv.get_editor_property(p)))

speed = mv.get_editor_property('max_walk_speed')
accel = mv.get_editor_property('max_acceleration')

# ⚠ 저장이 **컴파일보다 먼저**다 (§6 — 컴파일 뒤에는 안 써진다).
ok = unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False)
L('  저장(컴파일 전) -> %s' % ok)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)

L('')
L('  -- 이 값이 뜻하는 것 --')
L('  최고 속도까지  %.2fs · %.0fcm' % (speed / accel, speed ** 2 / (2 * accel)))
L('    -> 홀이 6×11m 라 가감속을 같이 안 올렸으면 최고 속도를 못 찍는다')
L('  애님 대비     %.1f배 어긋남 (애님 %.1f / 캡슐 %.1f cm/s)'
  % (speed / ANIM_SPEED, ANIM_SPEED, speed))
L('    -> 맞추려면 AM_Loco_Walk 재생 속도를 %.1f 로 (안 건드렸다)' % (speed / ANIM_SPEED))

# ---- ② 키오스크 회전 ----
L('')
L('=== ② K_Act 의 Play Action ===')
props = {str(p.get_editor_property('name')): str(p.get_editor_property('current_value'))
         for p in (S.get_task_property_names(ST, K_ACT, ACTION_TASK) or [])}
if 'FaceSlotKind' not in props:
    E('  FaceSlotKind 프로퍼티가 없다 — C++ 빌드가 반영 안 됐다')
    raise SystemExit

L('  전: ActionName=%s · FaceSlotKind=%s · FaceYawOffset=%s'
  % (props.get('ActionName'), props.get('FaceSlotKind'), props.get('FaceYawOffset')))

L('  set FaceSlotKind=Kiosk  -> %s'
  % S.set_task_property_value(ST, K_ACT, ACTION_TASK, 'FaceSlotKind', 'Kiosk'))
L('  set FaceYawOffset=15    -> %s'
  % S.set_task_property_value(ST, K_ACT, ACTION_TASK, 'FaceYawOffset', '15.0'))

res = S.compile_state_tree(ST)
L('  compile -> %s' % res)
L('  save    -> %s' % S.save_state_tree(ST))

L('')
L('=== 검산은 ll_walkin_tuning_verify.py 로 **새 프로세스**에서 ===')
