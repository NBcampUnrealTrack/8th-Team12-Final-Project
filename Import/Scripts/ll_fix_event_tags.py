"""트리의 이벤트 태그 접두사를 `Burgeria.` → `BurgerHouse.` 로 고친다.

    에디터를 **끄고** 돌린다.

## ⚠⚠ 손님이 `Wait` 에서 영원히 안 나가던 원인

```
C++ 가 쏘는 것       BurgerHouse.Order.Ready      (BurgerHouseTags.cpp)
트리가 기다리는 것    Burgeria.Order.Ready         ← 옛 이름
```

프로젝트가 `Burgeria` → `BurgerHouse` 로 개명될 때 **C++ 태그만 바뀌고 StateTree
에셋의 `RequiredEventTag` 는 옛 이름 그대로** 남았다.

`UStoreSubsystem::PushEvent` 는 트리를 찾아 이벤트를 **정상적으로 보낸다.** 트리가
그 태그를 안 기다리고 있을 뿐이라 —

- **에러가 없다.** 전송도 성공하고 로그도 안 남는다
- 증상은 「손님이 줄에서 안 나간다」 하나뿐이다
- §9.5 가 「대기는 스스로 끝나지 않는다. `Order.Ready` 이벤트가 빼낸다」고 적어
  둔 바로 그 지점이라, 이벤트 배선을 의심하기 전까지는 보이지 않는다

죽어 있던 전이는 셋이다 —

| 태그 | 죽으면 |
|---|---|
| `Order.Ready` | 주문이 나와도 **픽업하러 안 간다** (지금 증상) |
| `Queue.Advanced` | 앞사람이 빠져도 **줄이 안 당겨진다** |
| `Order.Lost` | 주문이 사라져도 **안 나간다** |

⚠ 실측 로그: `주문 1 준비됨` 뒤 100초간 아무 일도 없었다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary
S = unreal.StateTreeService

ST = '/Game/BurgerHouse/BP/ST_Customer'
OLD, NEW = 'Burgeria.', 'BurgerHouse.'

info = S.get_state_tree_info(ST)
states = info.get_editor_property('all_states')

L('=== 고칠 전이 찾기 ===')
todo = []
for st in states:
    path = str(st.get_editor_property('path'))
    for i, t in enumerate(st.get_editor_property('transitions')):
        tag = str(t.get_editor_property('required_event_tag'))
        if not tag or not tag.startswith(OLD):
            continue
        new_tag = NEW + tag[len(OLD):]
        todo.append((path, i, tag, new_tag,
                     str(t.get_editor_property('target_state_name'))))
        L('  %-28s 전이[%d]  %-28s -> %-32s (→ %s)'
          % (path.split('/')[-1], i, tag, new_tag,
             t.get_editor_property('target_state_name')))

if not todo:
    L('  고칠 것이 없다 — 이미 %s 다' % NEW)
else:
    L('')
    L('=== 고치기 ===')
    for path, idx, old, new, target in todo:
        # ⚠ `update_transition` 은 **인덱스**로 고른다. 다른 필드는 비워 두면
        #    그대로 유지된다 — 트리거·타깃을 다시 안 적어도 된다.
        r = S.update_transition(ST, path, idx, event_tag=new)
        L('  %-28s [%d] -> %s  %s' % (path.split('/')[-1], idx, new, r))
        if not r:
            E('     실패 — 인덱스나 태그 이름을 볼 것')

    # ⚠ **저장이 컴파일보다 먼저**다 (§6). 컴파일 산출물도 써야 하니 뒤에 한 번 더.
    pkg = EAL.load_asset(ST).get_outermost()
    L('')
    L('  [컴파일 전] save -> %s'
      % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))
    L('  compile -> %s' % S.compile_state_tree(ST))
    L('  [컴파일 후] save -> %s'
      % unreal.EditorLoadingAndSavingUtils.save_packages([pkg], only_dirty=False))

# ---- 되읽기 ----
L('')
L('=== 되읽기 (같은 프로세스 — 검산은 새 프로세스로) ===')
info2 = S.get_state_tree_info(ST)
left = []
for st in info2.get_editor_property('all_states'):
    for t in st.get_editor_property('transitions'):
        tag = str(t.get_editor_property('required_event_tag'))
        if tag and tag.startswith(OLD):
            left.append('%s: %s' % (st.get_editor_property('path'), tag))
if left:
    E('  아직 옛 태그가 남았다: %s' % left)
else:
    L('  %s 로 시작하는 태그가 하나도 없다 — OK' % OLD)
