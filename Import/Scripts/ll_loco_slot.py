"""1단계 — `ABP_Customer` AnimGraph 에 로코모션 전용 슬롯을 끼운다.

    에디터를 **끄고**:
      & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "C:\\Project\\Unreal\\Febricula.uproject" -run=pythonscript `
        -script="C:/Project/Unreal/Import/Scripts/ll_loco_slot.py" -unattended -nopause -nosplash

## 전/후

```
전   [Locomotion SM] ──────────────────→ Save cached 'LocoCache' → …
후   [Locomotion SM] → Slot 'LocoSlot' → Save cached 'LocoCache' → …
```

나머지(LayeredBlend · UpperBody · DefaultSlot · Output)는 **건드리지 않는다.**

## 왜 여기인가

`DefaultSlot` 은 그래프의 **맨 끝**에 있어서 행동 몽타주(먹기·Idle_Look)가 최종
포즈를 덮는다. 로코모션 슬롯을 그보다 **앞**에 두어야 그 관계가 유지된다.
뒤에 두면 걷기가 먹는 동작을 덮어 버린다.

## ⚠ 상태 기계를 **남겨 둔다** — 안전망이다

슬롯은 몽타주가 안 걸린 동안 `Source` 포즈를 그대로 통과시킨다. 상태 기계를
지금 지우면 그 Source 가 비어 **T포즈**가 된다. 계획 §3.4 대로 3단계가 PIE 에서
확인된 뒤에 정리한다.

## ⚠ 이름을 `Locomotion` 이 아니라 `LocoSlot` 으로 했다

상태 기계 이름도 `Locomotion`, 그 그래프 이름도 `Locomotion` 이다. 슬롯까지 같은
이름이면 로그에서 「Locomotion 이 셋 중 어느 것인가」가 된다. 계획안과 다르지만
이쪽이 낫다.

## ⚠ 되돌리기

`ABP_Customer.uasset` 은 이미 HEAD 와 다르다(걷기 교체). `git checkout` 으로
되돌리면 **그 작업까지 날아간다.** 되돌리려면 이 스크립트가 찍어 둔 「전」 배선을
보고 손으로 잇거나, `git checkout` 뒤 `ll_walk_apply.py` 를 다시 돌린다.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'
GRAPH = 'AnimGraph'
SLOT = 'LocoSlot'

BPS = unreal.BlueprintService
AGS = unreal.AnimGraphService


def edges():
    return [(str(c.source_node_id), str(c.source_pin_name),
             str(c.target_node_id), str(c.target_pin_name),
             ' '.join(str(c.source_node_title).split()),
             ' '.join(str(c.target_node_title).split()))
            for c in (BPS.get_connections(ABP, GRAPH) or [])]


def show(tag):
    L('--- %s 배선 ---' % tag)
    for s, sp, t, tp, st, tt in edges():
        L('  %-34s .%-12s -> %-34s .%s' % (st, sp, tt, tp))


def find_node(node_type, title_has=None):
    for n in BPS.get_nodes_in_graph(ABP, GRAPH) or []:
        if str(n.node_type) != node_type:
            continue
        if title_has and title_has not in str(n.node_title):
            continue
        return str(n.node_id)
    return None


show('전')

SM = find_node('AnimGraphNode_StateMachine')
CACHE = find_node('AnimGraphNode_SaveCachedPose')
L('')
L('  상태 기계        %s' % SM)
L('  Save cached pose %s' % CACHE)
if not (SM and CACHE):
    E('  기준 노드를 못 찾았다 — 중단한다')
    raise SystemExit

# ---- 이미 있으면 다시 만들지 않는다 (여러 번 돌려도 안전하게) ----
existing = None
for n in BPS.get_nodes_in_graph(ABP, GRAPH) or []:
    if str(n.node_type) == 'AnimGraphNode_Slot' and SLOT in str(n.node_title):
        existing = str(n.node_id)
if existing:
    L('  Slot %s 가 이미 있다 (%s) — 배선만 확인한다' % (SLOT, existing))
    NEW = existing
else:
    NEW = AGS.add_slot_node(ABP, GRAPH, SLOT, 144.0, -400.0)
    L('  Slot %s 추가 -> %s' % (SLOT, NEW))
if not NEW:
    E('  슬롯 노드를 못 만들었다')
    raise SystemExit

# ---- 재배선 ----
#
# ⚠ **끊고 잇는 순서**를 지킨다. 상태 기계 출력이 아직 LocoCache 에 붙어 있는
#   채로 새 슬롯에 또 이으면 포즈 링크가 둘이 되어 컴파일이 깨진다.
L('')
L('--- 재배선 ---')
L('  disconnect  StateMachine.Pose          -> %s' % AGS.disconnect_anim_node(ABP, GRAPH, SM, 'Pose'))
L('  connect     StateMachine.Pose -> %s.Source  -> %s'
  % (SLOT, AGS.connect_anim_nodes(ABP, GRAPH, SM, 'Pose', NEW, 'Source')))
L('  connect     %s.Pose -> LocoCache.Pose      -> %s'
  % (SLOT, AGS.connect_anim_nodes(ABP, GRAPH, NEW, 'Pose', CACHE, 'Pose')))

# ---- 컴파일 ----
#
# ⚠ 그래프 편집은 CDO 변경과 달리 패키지를 확실히 dirty 로 만든다. 그래서
#   `ll_walk_apply.py` 와 같은 「편집 → 컴파일 → 저장」 순서가 여기선 통한다
#   (§6 의 `compile_blueprint` 함정은 **CDO 만** 고쳤을 때의 이야기다).
BPS.compile_blueprint(ABP)
ok = EAL.save_asset(ABP, only_if_is_dirty=False)
L('')
L('  compile + save_asset -> %s' % ok)
if not ok:
    E('  ⚠⚠ 저장 실패 — 아래 배선은 메모리다. 새 프로세스로 검산할 것.')

# ---- 컴파일 상태 ----
d = BPS.describe_graph(ABP, GRAPH)
st = d.get_editor_property('status') if hasattr(d, 'get_editor_property') else None
L('  컴파일 상태: %s' % st)

L('')
show('후')
L('')
L('=== 검산은 ll_loco_slot_verify.py 로 **새 프로세스**에서 ===')
