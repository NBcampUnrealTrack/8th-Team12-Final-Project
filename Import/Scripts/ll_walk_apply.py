"""`ABP_Customer` 의 Walk 상태 애님을 갈아 끼우고 `MaxWalkSpeed` 를 맞춘다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_walk_apply.py" -unattended -nopause -nosplash

`ll_walk_retarget.py` 를 먼저 돌려 `Walk_Fwd_Manny` 가 있어야 한다.

## ⚠⚠ 속도를 같이 안 바꾸면 발이 미끄러진다

`MaxWalkSpeed` 는 애님의 보행 속도와 맞아야 한다 (§9). 지금 값 100 은 기존
`Walking` 의 97.5 cm/s 에 맞춘 것이다. 새 애님은 **183.0 cm/s** 라 그대로 두면
캡슐이 애님보다 1.8배 느리게 가고, 발이 땅을 긁으며 앞으로 밀린다.

    마네퀸 원본 A_WalkFwd_Loop   168.18 cm/s  (root 본에 179.39cm / 1.067s)
    손님 체격으로 스케일          183.0  cm/s  (골반 Z 101.8 / 93.6 = 1.088배)

⚠ 속도만 올리고 가감속을 그대로 두면 출발·정지가 굼떠 보인다. 원래 비율
  (100 : 400 : 600) 을 유지해 같이 올린다.

## ⚠ 커맨드릿에서 저장 — `True` 를 믿지 말 것

레벨과 달리 블루프린트는 `save_asset(only_if_is_dirty=False)` 로 써진다. 그래도
**검산은 새 프로세스**에서 한다 (`ll_walk_verify.py`). 같은 실행 안의 되읽기는
메모리를 보는 것이라 의미가 없다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
BP = '/Game/BurgerHouse/AI/BP_Customer'
NEW_ANIM = '/Game/BurgerHouse/Anims/Mixamo/Anims/Walk_Fwd_Manny'
STATE = 'Walk'

# 새 애님 실측 (`ll_walk_retarget.py` 의 MEASURED_SPEED)
NEW_SPEED = 183.0
OLD_SPEED = 100.0                      # 기존 값 — 비율 유지의 기준
RATIO = NEW_SPEED / OLD_SPEED

# ⚠ `MaxWalkSpeed` 를 10 밑으로 내리지 말 것 — Locomotion 의 Idle<->Walk 전이가
#   `Speed > 10` 이라 그 밑에서는 Idle 포즈로 미끄러진다 (§9). 올리는 쪽은 무해.
MOVEMENT = [
    ('max_walk_speed',               NEW_SPEED),
    ('max_acceleration',             400.0 * RATIO),
    ('braking_deceleration_walking', 600.0 * RATIO),
]

if not EAL.does_asset_exist(NEW_ANIM):
    E('%s 가 없다 — ll_walk_retarget.py 를 먼저 돌릴 것' % NEW_ANIM)
    raise SystemExit

# ---- ① Walk 상태 애님 교체 ----
L('=== ABP_Customer > Walk 상태 ===')
machines = unreal.AnimGraphService.list_state_machines(ABP) or []
if not machines:
    E('  상태 기계를 못 찾았다')
    raise SystemExit

# ⚠ 상태 기계 이름은 **노드 제목**에서 온다. 하드코딩하지 말고 찾아 쓴다.
target_machine = None
for m in machines:
    states = [str(s.state_name) for s in
              (unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or [])]
    L('  [%s] %s' % (m.machine_name, states))
    if STATE in states:
        target_machine = m.machine_name

if target_machine is None:
    E('  «%s» 상태를 가진 기계가 없다' % STATE)
    raise SystemExit

ok = unreal.AnimGraphService.set_state_animation(ABP, target_machine, STATE, NEW_ANIM, True)
L('  set_state_animation(%s, %s) -> %s' % (target_machine, STATE, ok))

res = unreal.AnimGraphService.validate_state_machine(ABP, target_machine)
L('  validate_state_machine -> %s' % res)

unreal.BlueprintService.compile_blueprint(ABP)
EAL.save_asset(ABP, only_if_is_dirty=False)
L('  컴파일 · 저장 완료')

# ---- ② 이동 속도 ----
L('')
L('=== BP_Customer > CharacterMovement ===')
bp = EAL.load_asset(BP)
cdo = unreal.get_default_object(bp.generated_class())
mv = cdo.get_editor_property('character_movement')
for prop, val in MOVEMENT:
    before = mv.get_editor_property(prop)
    mv.set_editor_property(prop, val)
    L('  %-30s %7.1f -> %7.1f' % (prop, before, mv.get_editor_property(prop)))
EAL.save_asset(BP, only_if_is_dirty=False)
L('  저장 완료')

L('')
L('=== 적용 끝 — 검산은 ll_walk_verify.py 로 **새 프로세스**에서 ===')
