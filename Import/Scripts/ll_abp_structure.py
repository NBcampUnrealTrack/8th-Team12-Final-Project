"""`ABP_Customer` 의 그래프 구조를 훑는다 — 몽타주 전환 계획용. 안 고친다.

보는 것 — 변수 · 상태와 전이 규칙 · 슬롯(몇 개인가) · 레이어드 블렌드 유무.
"""
import unreal

L = unreal.log_warning
E = unreal.log_error
EAL = unreal.EditorAssetLibrary

ABP = '/Game/BurgerHouse/Anims/Mixamo/ABP_Customer'

L('=== 변수 ===')
try:
    for v in unreal.BlueprintService.list_variables(ABP) or []:
        L('  %s' % v)
except Exception as ex:
    E('  list_variables 실패: %s' % str(ex)[:90])
    L('  BlueprintService 메서드: %s'
      % [m for m in dir(unreal.BlueprintService) if 'var' in m.lower()])

L('')
L('=== 상태와 전이 ===')
for m in unreal.AnimGraphService.list_state_machines(ABP) or []:
    mn = ' '.join(str(m.machine_name).split())
    L('  [%s]' % mn)
    for s in unreal.AnimGraphService.list_states_in_machine(ABP, m.machine_name) or []:
        L('    %-28s %s' % (' '.join(str(s.state_name).split()), s.state_type))
    try:
        for t in unreal.AnimGraphService.get_state_transitions(ABP, m.machine_name) or []:
            L('      전이 %s' % t)
    except Exception as ex:
        E('      get_state_transitions 실패: %s' % str(ex)[:80])

L('')
L('=== AnimGraph 노드 인벤토리 ===')
# ⚠ 노드 열거 API 이름이 판본마다 다르다 — 있는 것을 찾아 쓴다
cands = [m for m in dir(unreal.AnimGraphService)
         if any(k in m for k in ('node', 'slot', 'graph', 'info')) and not m.startswith('_')]
L('  AnimGraphService 후보 메서드: %s' % cands)
for fn in ('get_anim_graph_info', 'list_anim_graph_nodes', 'get_graph_info'):
    if not hasattr(unreal.AnimGraphService, fn):
        continue
    try:
        L('  %s -> %s' % (fn, getattr(unreal.AnimGraphService, fn)(ABP)))
    except Exception as ex:
        E('  %s 실패: %s' % (fn, str(ex)[:80]))

L('')
L('=== 슬롯 — 스켈레톤에 등록된 것 ===')
sk = EAL.load_asset('/Game/BurgerHouse/Anims/Mixamo/Hip_Hop_Dancing_Skeleton')
if sk:
    try:
        for g in sk.get_editor_property('slot_groups'):
            L('  그룹 %s: %s' % (g.get_editor_property('group_name'),
                                [str(s) for s in g.get_editor_property('slot_names')]))
    except Exception as ex:
        E('  slot_groups 실패: %s' % str(ex)[:90])

L('')
L('=== 몽타주가 실제로 쓰는 슬롯 ===')
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for d in reg.get_assets_by_path('/Game/BurgerHouse/Anims/Montages', recursive=True):
    if str(d.asset_class_path.asset_name) != 'AnimMontage':
        continue
    m = EAL.load_asset(str(d.package_name))
    tr = m.get_editor_property('slot_anim_tracks')
    L('  %-30s %s' % (m.get_name(),
                      [str(t.get_editor_property('slot_name')) for t in tr]))
