"""`ABP_Customer` AnimGraph 의 **현재 배선을 기록한다.** 안 고친다.

⚠ 다음 단계가 그래프 수술이다. 되돌리려면 지금 배선을 알아야 한다.
  `ABP_Customer.uasset` 은 이미 HEAD 와 다르므로(걷기 교체) `git checkout` 으로
  되돌리면 **그 작업까지 날아간다.** 복구는 여기 기록 + `ll_walk_apply.py` 재실행.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'

L('=== 그래프 목록 ===')
try:
    for g in unreal.AnimGraphService.list_graphs(ABP) or []:
        L('  %s' % g)
except Exception as ex:
    E('  list_graphs 실패: %s' % str(ex)[:90])

L('')
L('=== Output Pose 와 거기 연결된 것 ===')
for fn in ('get_output_pose_node_id', 'get_anim_graph_output_connection'):
    try:
        L('  %-34s -> %s' % (fn, getattr(unreal.AnimGraphService, fn)(ABP)))
    except Exception as ex:
        E('  %-34s 실패: %s' % (fn, str(ex)[:80]))

L('')
L('=== 노드별 애님 에셋 (get_node_animation_asset) ===')
# ⚠ 노드 id 를 모르면 못 묻는다. 상태 안의 노드는 상태 이름으로 우회한다.
for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
    for s in unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or []:
        if str(s.state_type) != 'State':
            continue
        nm = ' '.join(str(s.state_name).split())
        try:
            info = unreal.AnimGraphService.get_state_info(ABP, m.machine_name, nm)
            L('  %-10s %s' % (nm, info))
        except Exception as ex:
            E('  %-10s get_state_info 실패: %s' % (nm, str(ex)[:70]))

L('')
L('=== add_slot_node / connect_anim_nodes 시그니처 ===')
import inspect
for fn in ('add_slot_node', 'connect_anim_nodes', 'disconnect_anim_node',
           'get_anim_graph_output_connection', 'set_anim_graph_node_property',
           'bind_anim_node_property'):
    f = getattr(unreal.AnimGraphService, fn, None)
    if not f:
        continue
    doc = (f.__doc__ or '').strip().splitlines()
    L('  %s' % fn)
    for line in doc[:4]:
        L('      %s' % line.strip())
