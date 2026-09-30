"""마네퀸 ABP 의 **시퀀스 참조**를 새 애님으로 돌리고 루트모션 플래그를 확인한다.

    에디터를 **끄고** 돌린다.

## ⚠⚠ 일괄 리타겟은 **참조를 안 옮긴다**

`IKRetargetBatchOperation` 이 만든 것은 스켈레톤만 바뀐 껍데기였다 —

```
Manny/AM_Loco_Walk   스켈레톤 SK_Mannequin   하지만 안의 시퀀스는 Mixamo/Walk_Fwd_Manny
Manny/ABP_Customer   스켈레톤 SK_Mannequin   하지만 노드가 무는 시퀀스는 Mixamo/Idle …
```

⚠⚠ **에러가 없다.** 에셋도 생기고 스켈레톤도 맞아서 어디서도 안 걸린다. 화면에서
  손님이 레퍼런스 포즈로 서 있어야만 안다.

몽타주는 `ll_montages.py` 가 다시 만들어 해결했다. ABP 는 그래프 안이라 여기서
노드별로 갈아 끼운다.

## ⚠ 루트모션 플래그

리타게터는 소스의 `enable_root_motion`/`force_root_lock` 을 **그대로 복사한다**
(§9-2 에서 크게 데였다). `RootMotionFromMontagesOnly` 모드에서 이게 켜져 있으면
루트모션이 **`MaxWalkSpeed` 를 덮어써** 손님이 기어간다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ABP = '/Game/BurgerHouse/Anims/Manny/ABP_Customer'
NEW = '/Game/BurgerHouse/Anims/Manny'
fails = []

# ───────────────────────── 1) 루트모션 플래그
L('F === 새 시퀀스의 루트모션 플래그 ===')
fixed = 0
for p in EAL.list_assets(NEW, recursive=False, include_folder=False):
    a = EAL.load_asset(p)
    if not isinstance(a, unreal.AnimSequence):
        continue
    try:
        rm = a.get_editor_property('enable_root_motion')
        lock = a.get_editor_property('force_root_lock')
    except Exception:
        continue
    if rm or lock:
        a.set_editor_property('enable_root_motion', False)
        a.set_editor_property('force_root_lock', False)
        EAL.save_asset(p.split('.')[0], only_if_is_dirty=False)
        L('F   %-26s root_motion %s / lock %s -> 둘 다 끔' % (a.get_name(), rm, lock))
        fixed += 1
L('F   끈 것 %d개 (0 이면 애초에 꺼져 있었다)' % fixed)

# ───────────────────────── 2) ABP 의 시퀀스 참조
L('F')
L('F === ABP 그래프의 시퀀스 참조 ===')
S = unreal.AnimGraphService
L('F   is_anim_blueprint %s' % S.is_anim_blueprint(ABP))
used = S.get_used_anim_sequences(ABP) or []
L('F   쓰는 시퀀스 %d개' % len(used))
for u in used:
    L('F     %s' % u)

# ⚠ 노드 지정은 **GUID** 로 한다. 노드 표시 이름으로는 안 붙는다
#   — set_anim_graph_node_property 가 조용히 False 를 돌려줄 뿐이라 오진하기 쉽다.

# ⚠ `Sitting__1_` 은 지웠다 — 먹는 애님으로 옮겼다 (§9-35). 골반 높이가
#   행동 몽타주와 같아야 한 입 끝날 때마다 튀지 않는다.
JOBS = [('Idle', 'Idle'), ('Walk', 'Walk_Fwd_Manny'), ('Sit', 'Eating_a_Burger')]
B = unreal.BlueprintService
for graph, seq_name in JOBS:
    new_path = '%s/%s.%s' % (NEW, seq_name, seq_name)
    if not EAL.does_asset_exist('%s/%s' % (NEW, seq_name)):
        E('F   %s 의 마네퀸판이 없다' % seq_name)
        fails.append(seq_name)
        continue
    try:
        desc, err = B.describe_graph(ABP, graph)
    except Exception as ex:
        E('F   %s describe 실패 %s' % (graph, str(ex)[:60]))
        fails.append(graph)
        continue
    node_id = None
    for nd in desc.get_editor_property('nodes'):
        cls = str(nd.get_editor_property('node_class'))
        if 'SequencePlayer' in cls:
            node_id = str(nd.get_editor_property('node_id'))
            break
    if not node_id:
        E('F   %s 그래프에 Sequence Player 노드가 없다' % graph)
        fails.append(graph)
        continue
    ok = False
    for prop in ('Sequence', 'AnimSequence', 'Anim Sequence'):
        if S.set_anim_graph_node_property(ABP, node_id, prop, new_path):
            ok = True
            break
    L('F   %-6s %-16s node %s · %s -> %s' % (graph, seq_name, node_id[:8], prop, 'OK' if ok else '<-- 실패'))
    if not ok:
        fails.append('%s 노드 설정' % graph)

# ⚠ **저장이 컴파일보다 먼저** (§6)
bp = EAL.load_asset(ABP)
L('F   save -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
L('F   save(컴파일 후) -> %s'
  % unreal.EditorLoadingAndSavingUtils.save_packages([bp.get_outermost()], only_dirty=False))

L('F')
L('F === 되읽기 ===')
for u in (S.get_used_anim_sequences(ABP) or []):
    p2 = str(u.get_editor_property('sequence_path'))
    bad = '/Anims/Mixamo' in p2
    L('F   %-58s %s' % (p2, '<-- 아직 Mixamo!' if bad else 'OK'))
    if bad:
        fails.append(p2)

L('F')
if fails:
    E('F === 실패: %s ===' % fails)
else:
    L('F === 전부 통과 ===')
