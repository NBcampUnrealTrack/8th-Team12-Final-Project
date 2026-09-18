"""워크인 손님이 키오스크 `Play Action` 을 끝낸 직후 레벨의 소리 액터를 울린다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_order_sound.py" -unattended -nopause -nosplash

## 어느 상태에 얹나 — `K_Order`

워크인(기본) 갈래는 이렇게 흐른다.

```
Enter → Kiosk → K_Move (Move To) → K_Act (Play Action Interaction_Reach)
                                       ↓ OnStateCompleted
                                    K_Order (Place Order)  ← 여기에 얹는다
                                       ↓
                                    Wait
```

`K_Act` 의 `Play Action` 이 **완료되어야** `K_Order` 로 전이하므로, `K_Order` 에
들어가는 순간이 곧 「Ordering 의 Play Action 을 완료한 다음」이다.

⚠ 프리필 갈래(`PO_Act` → `PO_Order`)에는 **안 얹는다.** 요청이 워크인이었고,
  `Auto Possess AI` 를 끈 뒤로 프리필은 트리를 타지도 않는다 (§6).

## ⚠⚠ 프로젝트 문서의 「StateTree 는 파이썬으로 못 고친다」는 이제 틀리다

`BURGERHOUSE.md` 가 「태스크 파라미터가 `InstancedStruct` 안에 있어 UE 5.8
파이썬으로는 못 고친다」고 적어 놨는데, `unreal.StateTreeService` 가 `add_task` ·
`set_task_property_value` · `compile_state_tree` · `save_state_tree` 를 준다.

⚠ 그래서 **트리 오타 두 개(`Sit-Eat` · `Interaction_Reach`)도 이제 고칠 수 있다.**
  지금은 `ll_montages.py` 가 별칭으로 막아 두고 있다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

ST = '/Game/BurgerHouse/BP/ST_Customer'

# ⚠⚠ **상태는 전체 경로**여야 한다. `K_Order` 만 주면 `add_task` 가 조용히
#    `False` 를 돌려준다 (예외도 로그도 없다).
STATE = 'Root/Kiosk/K_Order'

# ⚠⚠ **구조체 이름에 `F` 접두사를 붙이지 않는다.** `get_state_tree_info` 의
#    `StructType` 표기와 같아야 한다 — `BurgerHousePlaceOrderTask` 처럼.
#    `FBurgerHousePlaySoundTask` 로 주면 역시 조용히 `False` 다.
TASK = 'BurgerHousePlaySoundTask'

TAG = 'OrderChime'

S = unreal.StateTreeService


def tasks_of(path):
    """상태 하나의 태스크 이름만 뽑는다. `get_state_tree_info` 전체를 찍으면
    로그가 수천 자라 읽을 수가 없다."""
    info = S.get_state_tree_info(ST)
    out = []
    for st in (info.get_editor_property('all_states') if info else []):
        if str(st.get_editor_property('path')) != path:
            continue
        for t in st.get_editor_property('tasks'):
            out.append('%s (%s)' % (t.get_editor_property('name'),
                                    t.get_editor_property('struct_type')))
    return out


L('=== 전 — %s ===' % STATE)
for t in tasks_of(STATE):
    L('  %s' % t)

# ⚠ 이미 있으면 다시 넣지 않는다 (여러 번 돌려도 안전하게).
before = S.get_task_property_names(ST, STATE, TASK) or []
if before:
    L('  %s 가 이미 있다 — 프로퍼티만 맞춘다' % TASK)
else:
    ok = S.add_task(ST, STATE, TASK)
    L('  add_task(%s, %s) -> %s' % (STATE, TASK, ok))
    if not ok:
        E('  태스크를 못 넣었다 — 상태 경로나 구조체 이름을 볼 것')
        raise SystemExit

L('')
L('=== 태스크 프로퍼티 ===')
for p in S.get_task_property_names(ST, STATE, TASK) or []:
    L('  %s' % p)

# ⚠ 태그만 박는다. `Sound` 는 비워 둬서 액터의 AudioComponent 를 그대로 튼다 —
#   레벨에서 소리를 갈아 끼울 수 있게.
L('')
L('  set ActorTag=%s -> %s' % (TAG, S.set_task_property_value(ST, STATE, TASK, 'ActorTag', TAG)))

# ---- 컴파일 · 저장 ----
res = S.compile_state_tree(ST)
L('')
L('  compile -> %s' % res)
saved = S.save_state_tree(ST)
L('  save    -> %s' % saved)
if not saved:
    E('  ⚠⚠ 저장 실패 — 새 프로세스로 검산할 것 (§6)')

L('')
L('=== 후 — %s ===' % STATE)
for t in tasks_of(STATE):
    L('  %s' % t)

L('')
L('=== 남은 것은 레벨에서 (스크립트로 못 하는 것이 아니라, 취향이 걸린 것) ===')
L('  ① AmbientSound 에 **Sound 를 붙인다** — 지금 비어 있다')
L('  ② **Auto Activate 를 끈다** — 켜 두면 레벨이 열리자마자 계속 운다')
L('  ③ 그 액터의 Tags 에 «%s» 를 넣는다' % TAG)
L('  ⚠ ①~③ 중 하나라도 빠지면 소리가 안 나고, 원인은 LogBurgerHouseST 에 찍힌다.')
