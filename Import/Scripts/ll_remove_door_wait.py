"""워크인 손님이 **문 앞에서 서 있는 시간**을 없앤다.

    에디터를 **끄고** 돌린다.

## 대기 시간이 어디서 나왔나 — **고정값이 아니라 몽타주 길이**다

`Enter` 상태에 `Play Action Idle_Look` 이 있고 전이가 `OnStateCompleted` 다.
`Play Action` 은 **몽타주 길이만큼 그 상태에 머문다** — 그래서 문 앞 대기 시간이
곧 `Idle_Look` 변형의 길이였다.

```
AM_Idle              8.33s  w=1.0
AM_Telling_A_Secret 10.90s  w=0.6
AM_Texting          18.53s  w=0.3     -> 기대값 10.8초 · 최대 18.5초
```

C++ 에 박힌 `WaitSeconds` 같은 것은 없다. `FallbackSeconds` 는 0.5 인데 그건
**몽타주를 못 찾았을 때만** 쓰인다.

## 어떻게 없애나

```
전   Enter ▸ Play Action Idle_Look   ·  전이 OnStateCompleted -> Kiosk
후   Enter ▸ (태스크 없음)            ·  전이 OnTick           -> Kiosk
```

`P_Ordering` 이 이미 `OnTick -> Kiosk` 로 즉시 나가는 본보기다.

⚠⚠ **태스크를 남긴 채 전이만 OnTick 으로 바꾸면 안 된다.** `Play Action` 이
  이미 `Idle_Look` 몽타주를 `DefaultSlot` 에 걸어 버렸고, `DefaultSlot` 은
  `LocoSlot` 보다 **뒤**라 걷는 동안 그 몽타주가 로코모션을 덮는다 (§9-2) —
  손님이 문 앞에서 폰을 보며 미끄러져 온다.

⚠ `Enter` 는 **지우면 안 된다.** `PE_Seat` · `PW_Wait` · `PO_Kiosk` 의
  `OnStateFailed` 가 전부 여기를 가리킨다 (자리를 못 잡았을 때의 회수 지점).
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

ST = '/Game/BurgerHouse/AI/ST_Customer'
STATE = 'Root/Enter'
TASK = 'BurgerHouseActionTask'
S = unreal.StateTreeService


def show(tag):
    info = S.get_state_tree_info(ST)
    for st in info.get_editor_property('all_states'):
        if str(st.get_editor_property('path')) != STATE:
            continue
        tks = [str(t.get_editor_property('struct_type')) for t in st.get_editor_property('tasks')]
        trs = [(i, str(t.get_editor_property('trigger')),
                str(t.get_editor_property('target_state_name')))
               for i, t in enumerate(st.get_editor_property('transitions'))]
        L('  %s  태스크 %s' % (tag, tks or '(없음)'))
        for i, trig, tgt in trs:
            L('       전이[%d] %s -> %s' % (i, trig, tgt))


show('전')

# ① 태스크 제거
L('')
L('  remove_task -> %s' % S.remove_task(ST, STATE, TASK))

# ② 전이를 OnTick 으로
#
# ⚠ `update_transition` 은 **인덱스**로 고른다. Enter 에는 전이가 하나뿐이라 0.
L('  update_transition(0, OnTick) -> %s'
  % S.update_transition(ST, STATE, 0, trigger='OnTick'))

# ---- 저장 ----
#
# ## ⚠⚠ `save_state_tree` 가 **컴파일 뒤에는 `False`** 를 준다
#
# 블루프린트(§6 `compile_blueprint`)와 **같은 함정**이다. 컴파일은 성공하고
# 같은 실행 안의 되읽기도 통과하는데 **파일이 안 써진다.** 실제로 당했다 —
# `compile -> success:True` 뒤에 `save -> False` 였고, 파일 수정 시각이 29분 전
# 그대로였다.
#
# ⚠ `ll_order_sound.py` 는 `add_task` 만 해서 우연히 통과했다. `remove_task` ·
#   `update_transition` 이 섞이면 재현된다.
#
# → **컴파일보다 먼저 저장**하고, 그래도 안 되면 패키지로 직접 쓴다.
EAL = unreal.EditorAssetLibrary
pkg = EAL.load_asset(ST).get_outermost()

L('')
L('  [컴파일 전] save_state_tree -> %s' % S.save_state_tree(ST))
L('  [컴파일 전] save_packages   -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

res = S.compile_state_tree(ST)
L('  compile -> %s' % res)

# ⚠ 컴파일이 만든 **런타임 데이터**도 디스크에 있어야 한다. 한 번 더 쓴다.
ok = unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False)
L('  [컴파일 후] save_packages   -> %s' % ok)
if not ok:
    E('  ⚠⚠ 안 써졌다 — 파일 수정 시각으로 확인할 것. 되읽기는 메모리다.')

L('')
show('후')

L('')
L('⚠ `Idle_Look` 자체는 그대로 둔다 — 줄에서 기다리는 `W_Idle` · `PW_Idle` 이')
L('  여전히 쓴다. 문 앞에서만 안 쓰는 것이다.')
